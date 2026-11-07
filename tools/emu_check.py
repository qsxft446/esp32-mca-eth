"""Проверка эмуляции MCA по сети (протокол shproto на TCP-порту 8234) с ПК.

    python tools/emu_check.py <ip> [порт] [--cal-write]
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
  - второе подключение вытесняет первое;
  - калибровка в -cal разбирается ровно как в BecqMoni
    (AtomSpectraTcpCalibrationReader.Parse) и совпадает с калибровкой
    прибора со страницы (/calib), пересчитанной на 8192 канала;
  - с --cal-write: пробная калибровка записывается, как пишет BecqMoni
    (-cal N hhhhhhhh, регистр 10 последним), прибор делает из неё свою
    (/calib), отдаёт её обратно бит в бит; калибровка с дырой отвергается.
    Затем прежняя калибровка прибора ВОЗВРАЩАЕТСЯ (POST /calib).
ВНИМАНИЕ: проверка останавливает и запускает набор (-sto/-sta) и в конце
возвращает его в прежнее состояние; спектр не сбрасывает.
"""
import json
import math
import socket
import struct
import sys
import time
import urllib.request

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


# ---------------- калибровка по энергии в -cal ----------------
def becq_parse(answer):
    """Разбор ответа -cal как в BecqMoni (AtomSpectraTcpCalibrationReader.Parse):
    строки по \r\n, коэффициент - пара строк (старшая первой) как 64-битный
    double, пустой - FFFFFFFFFFFFFFFF, CRC32 строк 0..9 - в строке 10.
    -> (ошибка или None, коэффициенты)."""
    parts = answer.split("\r\n")
    if len(parts) < 11:
        return "Malformed", None
    crc_in = "".join(parts[:10])
    try:
        crc = int(parts[10].strip(), 16)
    except ValueError:
        return "Malformed", None
    if crc32_std(crc_in) != crc:
        return "BadCrc", None
    vals, empty = [], False
    for i in range(5):
        h = parts[2 * i] + parts[2 * i + 1]
        if h.upper() == "F" * 16:
            empty = True
            continue
        if empty:
            return "GapInCoefficients", None
        v = struct.unpack("<d", struct.pack("<Q", int(h, 16)))[0]
        if not math.isfinite(v):
            return "InvalidNumber", None
        vals.append(v)
    while vals and vals[-1] == 0.0:
        vals.pop()
    if len(vals) < 2:
        return "EmptyCoefficients", None
    return None, vals


def cal_regs(coef):
    """Коэффициенты -> 11 строк регистров 0..10, как их пишет BecqMoni."""
    regs = []
    for i in range(5):
        w = struct.unpack("<Q", struct.pack("<d", coef[i]))[0] if i < len(coef) else (1 << 64) - 1
        regs += ["%08X" % (w >> 32), "%08X" % (w & 0xFFFFFFFF)]
    regs.append("%08X" % crc32_std("".join(regs)))
    return regs


def poly_affine(q, alpha, beta):
    """p(y) = q(alpha*y + beta) - пересчёт многочлена при замене переменной."""
    n = len(q)
    return [sum(q[i] * math.comb(i, j) * alpha ** j * beta ** (i - j) for i in range(j, n))
            for j in range(n)]


def http_json(host, path, body=None):
    req = urllib.request.Request("http://%s%s" % (host, path), data=body,
                                 method="POST" if body is not None else "GET")
    with urllib.request.urlopen(req, timeout=8) as r:
        return r.read().decode("utf-8")


def device_cal(host):
    """Калибровка прибора, как её видит страница: (коэффициенты в каналах
    прибора или None, растяжение mul, текст /calib)."""
    raw = http_json(host, "/calib")
    cfg = json.loads(http_json(host, "/cfg"))
    freq = json.loads(http_json(host, "/stat"))["freq"]
    # подпись настроек - как calSig() страницы
    sig = ",".join(str(cfg[k]) for k in ("algo", "int_rise", "int_fall", "trap_L", "trap_G",
                                         "flat_avg", "cpc", "polarity")) + "," + str(freq)
    nch = cfg["nch"] if cfg["nch"] in (2048, 4096) else 8192
    mul = 8192 // nch
    try:
        j = json.loads(raw)
    except ValueError:
        j = {}
    c = j.get("c") if isinstance(j, dict) else None
    ok = bool(c) and len(c) >= 2 and j.get("sig") == sig
    return (c if ok else None), mul, raw, sig


def close(a, b, rel=1e-9):
    return len(a) == len(b) and all(abs(x - y) <= rel * max(abs(x), abs(y), 1e-30) for x, y in zip(a, b))


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
    # калибровка: свои регистры разбираются правилами BecqMoni обратно
    coef = [-3.25, 0.371, 1.2e-6]
    regs = cal_regs(coef)
    err, got = becq_parse("\r\n".join(regs + ["FFFFFFFF"] * 29) + "\r\n")
    ok = err is None and got == coef
    print(("OK    " if ok else "ПРОВАЛ"), "калибровка: регистры -> разбор BecqMoni", err or got)
    bad += not ok
    err, _ = becq_parse("\n".join(regs))
    ok = err == "Malformed"
    print(("OK    " if ok else "ПРОВАЛ"), "калибровка: строки только с \\n BecqMoni не разбирает", err)
    bad += not ok
    for mul in (1, 2, 4):
        mid = (mul - 1) / 2
        k = poly_affine(coef, 1 / mul, -mid / mul)          # прибор -> 8192
        back = poly_affine(k, mul, mid)                     # 8192 -> прибор
        x = 700
        ok = close(back, coef) and abs(sum(k[i] * (x * mul + mid) ** i for i in range(3)) -
                                       sum(coef[i] * x ** i for i in range(3))) < 1e-9
        print(("OK    " if ok else "ПРОВАЛ"), f"калибровка: пересчёт каналов x{mul} туда-обратно")
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


def check_cal_read(k, host, check, what, expect=None):
    """-cal разбирается, как в BecqMoni, и совпадает с калибровкой прибора
    (или с expect - коэффициентами в 8192 каналах)."""
    ans = k.cmd_text("-cal")
    err, got = becq_parse(ans)
    dev, mul, _, _ = device_cal(host)
    if expect is None:
        expect = poly_affine(dev, 1 / mul, -(mul - 1) / 2 / mul) if dev else None
    if expect is None:
        check(f"-cal: {what} - пусто, BecqMoni скажет «нет калибровки»",
              err == "EmptyCoefficients", f"разбор: {err}, каналов x{mul}")
    else:
        check(f"-cal: {what} - BecqMoni получит её (8192 канала)",
              err is None and close(got, expect),
              f"разбор: {err or ['%.6g' % v for v in got]}, ждали {['%.6g' % v for v in expect]}, каналов x{mul}")
    return got


def check_cal_write(host, port, check):
    """Запись калибровки, как это делает BecqMoni, и возврат прежней."""
    _, mul, saved, sig = device_cal(host)
    k = Link(host, port)
    k.pump(0.5)
    try:
        coef = [-3.25, 0.371 / mul, 1.2e-6 / mul / mul]     # в 8192 каналах
        regs = cal_regs(coef)
        answers = [k.answer("-cal %d %s" % (i, regs[i]), 1.5)[0] for i in range(11)]
        check("запись калибровки: -ok на каждый регистр 0..10", answers == ["-ok"] * 11, str(answers))
        j = json.loads(http_json(host, "/calib"))
        mid = (mul - 1) / 2
        want = poly_affine(coef, mul, mid)                  # в каналах прибора
        check("запись калибровки: стала калибровкой прибора (/calib)",
              close(j.get("c", []), want) and j.get("sig") == sig and j.get("iso") == "BecqMoni",
              f"c={j.get('c')} ждали {want}, sig {'совпала' if j.get('sig') == sig else 'другая'}")
        got = check_cal_read(k, host, check, "записанная", expect=coef)
        check("запись калибровки: обратно бит в бит", got == coef, str(got))
        # дыра в коэффициентах: a0 есть, a1 пустой, a2 есть - отказ
        bad = cal_regs([1.0, 2.0, 3.0])
        bad[2] = bad[3] = "FFFFFFFF"
        for i in range(10):
            k.answer("-cal %d %s" % (i, bad[i]), 1.5)
        a, _ = k.answer("-cal 10 %s" % bad[10], 1.5)
        j2 = json.loads(http_json(host, "/calib"))
        check("запись калибровки с дырой: отказ, калибровка прибора не тронута",
              a == "-err invalid calibration" and close(j2.get("c", []), want), repr(a))
    finally:
        # вернуть калибровку прибора как была
        body = saved.encode() if saved.strip() else b"{}"
        http_json(host, "/calib", body)
        k.close()
    after = http_json(host, "/calib")
    check("калибровка прибора возвращена как была", json.loads(after or "{}") == json.loads(saved or "{}"),
          after[:80])


def stat_times(packets):
    return [struct.unpack_from("<I", d)[0] for c, d in packets if c == CMD_STAT and len(d) >= 4]


def run(host, port, cal_write=False):
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
    check_cal_read(k, host, check, "калибровка прибора")
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

    if cal_write:
        k2.close()
        check_cal_write(host, port, check)
        k2 = Link(host, port)
        k2.pump(0.5)

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
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    sys.exit(1 if run(args[0], int(args[1]) if len(args) > 1 else 8234,
                      cal_write="--cal-write" in sys.argv) else 0)
