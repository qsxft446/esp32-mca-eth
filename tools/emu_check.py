"""Проверка эмуляции MCA по сети (протокол shproto на TCP-порту 8234) с ПК.

    python tools/emu_check.py <ip> [порт]
    python tools/emu_check.py --selftest      # только кодек, без прибора

Подключается так же, как BecqMoni («AtomSpectra TCP»), шлёт команды
текстом в пакете 0x03 с нулём на конце и разбирает ответы по правилам
эталонной реализации протокола:
  - пакет 0xFF 0xFE cmd данные CRC16(lo,hi) 0xA5, CRC16 Modbus;
    экранирование 0xFE/0xA5/0xFD через 0xFD и инверсию;
  - поток: ещё до первой команды каждую секунду идут статус и весь спектр
    (BecqMoni 2.5 с слушает поток и по времени в статусе решает, идёт ли
    набор); статус приходит ДО конца прохода, проход - смещения 0, 64, ...
    без пропусков до 8192;
  - ответ -inf: ключи VERSION ... PileUpThr; -cal: 40 строк по 8 hex,
    CRC32 строк 0..9 в строке 10, серийник в строке 39;
  - ответы на команды - как их ждёт BecqMoni: "-ok" на -sto, -sta
    (в том числе посреди прохода по спектру - за 1 с);
  - после -sto время набора в статусе стоит, после -sta - растёт;
  - второе подключение вытесняет первое.
ВНИМАНИЕ: проверка останавливает и запускает набор (-sto/-sta) и в конце
возвращает его в прежнее состояние; спектр не сбрасывает.
"""
import socket
import struct
import sys
import time

START, ESC, FINISH = 0xFE, 0xFD, 0xA5
CMD_HIST, CMD_TEXT, CMD_STAT = 0x01, 0x03, 0x04
CHANNELS = 8192


def crc16(data, crc=0xFFFF):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame(cmd, payload):
    body = bytes([cmd]) + bytes(payload)
    c = crc16(body)
    out = bytearray([0xFF, START])
    for b in body + bytes([c & 0xFF, c >> 8]):
        if b in (START, FINISH, ESC):
            out += bytes([ESC, (~b) & 0xFF])
        else:
            out.append(b)
    out.append(FINISH)
    return bytes(out)


class Parser:
    """Как shproto_byte_received из эталона."""

    def __init__(self):
        self.buf, self.started, self.esc = bytearray(), False, False
        self.packets, self.dropped = [], 0

    def feed(self, data):
        for b in data:
            if b == START:
                self.buf, self.started, self.esc = bytearray(), True, False
                continue
            if not self.started:
                continue
            if b == ESC:
                self.esc = True
                continue
            if b == FINISH:
                self.started = False
                if len(self.buf) >= 3 and crc16(self.buf) == 0:
                    self.packets.append((self.buf[0], bytes(self.buf[1:-2])))
                else:
                    self.dropped += 1
                continue
            if self.esc:
                b, self.esc = (~b) & 0xFF, False
            self.buf.append(b)


def crc32_std(s):
    import zlib
    return zlib.crc32(s.encode()) & 0xFFFFFFFF


def selftest():
    bad = 0
    ok = crc16(b"123456789") == 0x4B37
    print(("OK    " if ok else "ПРОВАЛ"), "CRC16 Modbus эталонное значение")
    bad += not ok
    payload = bytes([0xFE, 0xA5, 0xFD, 0xFF, 0x00, 0x41]) * 20
    p = Parser()
    p.feed(b"\x12\x34" + frame(CMD_TEXT, payload) + b"\xFF")
    ok = p.packets == [(CMD_TEXT, payload)] and p.dropped == 0
    print(("OK    " if ok else "ПРОВАЛ"), "кадр с экранируемыми байтами туда-обратно")
    bad += not ok
    f = bytearray(frame(CMD_TEXT, b"-inf\0"))
    f[4] ^= 0x01
    p = Parser()
    p.feed(bytes(f))
    ok = not p.packets and p.dropped == 1
    print(("OK    " if ok else "ПРОВАЛ"), "битый кадр отбрасывается")
    bad += not ok
    return bad


class Link:
    """TCP-подключение к прибору с разбором потока."""

    def __init__(self, host, port):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.sock.settimeout(0.05)
        self.p = Parser()
        self.closed = False

    def pump(self, sec):
        t_end = time.time() + sec
        while time.time() < t_end:
            try:
                d = self.sock.recv(65536)
                if not d:
                    self.closed = True
                    return
                self.p.feed(d)
            except socket.timeout:
                pass
            except OSError:
                self.closed = True
                return

    def send(self, text):
        self.sock.sendall(frame(CMD_TEXT, text.encode() + b"\0"))

    def answer(self, text, timeout):
        """Как BecqMoni: ПЕРВЫЙ текстовый пакет после команды, обрезанный по
        первому CR. Возвращает (строка, секунд)."""
        self.p.packets.clear()
        t0 = time.time()
        self.send(text)
        while time.time() - t0 < timeout:
            self.pump(0.05)
            for c, d in self.p.packets:
                if c == CMD_TEXT:
                    return d.decode("ascii", errors="replace").split("\r")[0], time.time() - t0
        return None, timeout

    def cmd_text(self, text, wait=0.8):
        self.p.packets.clear()
        self.send(text)
        self.pump(wait)
        return "".join(d.decode(errors="replace") for c, d in self.p.packets if c == CMD_TEXT)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def stat_times(packets):
    return [struct.unpack_from("<I", d)[0] for c, d in packets if c == CMD_STAT and len(d) >= 4]


def run(host, port):
    fails = []

    def check(name, cond, detail=""):
        print(("OK    " if cond else "ПРОВАЛ"), name, ("-- " + detail) if detail else "")
        if not cond:
            fails.append(name)

    k = Link(host, port)
    # 1. поток до первой команды - как проба BecqMoni (ProbeMs 2.5 с)
    k.pump(3.2)
    pk = list(k.p.packets)
    st = [d for c, d in pk if c == CMD_STAT]
    hist = [(struct.unpack_from("<H", d)[0], (len(d) - 2) // 4) for c, d in pk if c == CMD_HIST]
    check("поток без команд: статус каждую секунду", len(st) >= 2, f"статусов за 3.2 с: {len(st)}")
    starts = [i for i, (o, n) in enumerate(hist) if o == 0]
    ok_sweep = any(i + CHANNELS // 64 <= len(hist) and
                   all(o == j * 64 and n == 64 for j, (o, n) in enumerate(hist[i:i + CHANNELS // 64]))
                   for i in starts)
    check("поток без команд: полный проход 0, 64, ... 8192", ok_sweep, f"пакетов спектра {len(hist)}")
    si = [i for i, (c, d) in enumerate(pk) if c == CMD_STAT]
    ends = [i for i, (c, d) in enumerate(pk)
            if c == CMD_HIST and struct.unpack_from("<H", d)[0] + 64 >= CHANNELS]
    check("статус приходит до конца прохода", bool(si and ends and si[0] < ends[-1]))
    if st and len(st[0]) >= 10:
        t, load, cps = struct.unpack_from("<IHI", st[0])
        print(f"       время {t} с, загрузка {load} %, CPS {cps}")
    times = stat_times(pk)
    was_running = len(times) >= 2 and times[-1] > times[0]
    print(f"       набор до проверки: {'идёт' if was_running else 'стоит'} (время {times})")

    # 2. команды и ответы
    a, dt = k.answer("-sto", 1.0)
    check("-sto: ответ -ok за 1 с", a == "-ok", f"{a!r} за {dt:.2f} с")
    k.p.packets.clear()
    k.pump(2.5)
    t2 = stat_times(k.p.packets)
    check("после -sto время в статусе стоит", len(t2) >= 2 and t2[-1] == t2[0], str(t2))
    inf = k.cmd_text("-inf")
    check("-inf: ответ с VERSION и PileUpThr", "VERSION " in inf and "PileUpThr " in inf,
          inf[:90].replace("\r\n", " | "))
    cal = k.cmd_text("-cal")
    lines = [l for l in cal.replace("\r", "").split("\n") if l]
    hexok = all(len(l) == 8 and all(ch in "0123456789ABCDEFabcdef" for ch in l) for l in lines)
    check("-cal: 40 строк по 8 hex", len(lines) == 40 and hexok, f"строк {len(lines)}")
    if len(lines) == 40:
        check("-cal: CRC32 строк 0..9 в строке 10", crc32_std("".join(lines[:10])) == int(lines[10], 16))
        check("-cal: серийник 8 hex и не FFFFFFFF", lines[39].upper() != "FFFFFFFF", lines[39])
    stt = k.cmd_text("-stt")
    check("-stt: stopped после -sto", stt.strip() == "stopped", repr(stt))
    a, dt = k.answer("-sta", 1.0)
    check("-sta: ответ -ok за 1 с", a == "-ok", f"{a!r} за {dt:.2f} с")
    k.p.packets.clear()
    k.pump(2.5)
    t3 = stat_times(k.p.packets)
    check("после -sta время в статусе растёт", len(t3) >= 2 and t3[-1] > t3[0], str(t3))
    stt = k.cmd_text("-stt")
    check("-stt: collecting во время набора", "collecting" in stt, repr(stt))
    # ответ посреди прохода по спектру: дождаться начала прохода
    k.p.packets.clear()
    t0 = time.time()
    while time.time() - t0 < 1.5 and not any(c == CMD_HIST for c, d in k.p.packets):
        k.pump(0.02)
    a, dt = k.answer("-sto", 1.0)
    check("-sto посреди прохода: -ok за 1 с", a == "-ok", f"{a!r} за {dt:.2f} с")
    a, _ = k.answer("-spd 600000", 1.0)
    check("-spd: -ok, по сети ничего не меняет", a == "-ok", repr(a))
    a, _ = k.answer("-ris 8", 1.0)
    check("заводская команда: честный отказ", a == "-err not supported", repr(a))

    # 3. второе подключение вытесняет первое
    k2 = Link(host, port)
    k.pump(1.5)
    k2.pump(2.0)
    check("второе подключение: первое закрыто прибором", k.closed)
    check("второе подключение: поток идёт новому клиенту",
          any(c == CMD_STAT for c, d in k2.p.packets))
    k.close()

    # вернуть набор как был
    a, _ = k2.answer("-sta" if was_running else "-sto", 1.0)
    print(f"       набор возвращён: {'идёт' if was_running else 'стоит'} ({a!r})")
    k2.close()
    print(f"\nбитых кадров: {k.p.dropped + k2.p.dropped}")
    print("Все проверки пройдены" if not fails else f"ПРОВАЛЕНО: {len(fails)}")
    return len(fails)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        sys.exit(1 if selftest() else 0)
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(1 if run(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 8234) else 0)
