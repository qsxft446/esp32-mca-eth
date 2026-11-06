"""Окно быстрого CPS и мощности дозы - построчный порт main/cps_win.c
(накопление до 30 с + детектор изменений CUSUM).

    python tools/cpswin.py              модель: отклик на скачки и разброс в покое
    python tools/cpswin.py <ip> [сек]   сверка с прибором: прибор отдаёт в /cps
                                        номер отсчёта быстрой истории (hi), длину
                                        окна в интервалах (cpwn) и событий в нём
                                        (cpsn); модель, прогнанная по той же
                                        истории /hist?f=1, должна дать те же
                                        целые числа
"""
import math, sys

K, R, H = 300, 1.5, 8.0                 # как CPSW_K, CPSW_R, CPSW_H


class Win:
    def __init__(self):
        self.c, self.d = [], []         # последние K интервалов: события, мкс
        self.n = 0
        self.su = self.sd = 0.0
        self.zu = self.zd = 0

    def sums(self):
        c = self.c[-self.n:] if self.n else []
        d = self.d[-self.n:] if self.n else []
        return sum(c), sum(d)

    def step(self, dc, dd):
        if self.n > 0:
            sc, sdur = self.sums()
            lam = sc * dd / sdur if sdur else 0.0
            if lam <= 0.0:
                if dc > 0:
                    self.n = 0
                    self.su = self.sd = 0.0
                    self.zu = self.zd = 0
            else:
                lu, ld = math.log(R), -math.log(R)
                self.su = max(0.0, self.su + dc * lu - lam * (R - 1.0))
                self.zu = self.zu + 1 if self.su > 0 else 0
                self.sd = max(0.0, self.sd + dc * ld - lam * (1.0 / R - 1.0))
                self.zd = self.zd + 1 if self.sd > 0 else 0
                cut = -1
                if self.su > H:
                    cut = self.zu - 1
                elif self.sd > H:
                    cut = self.zd - 1
                if cut >= 0:
                    if cut < self.n:
                        self.n = cut
                    self.su = self.sd = 0.0
                    self.zu = self.zd = 0
        self.c.append(dc); self.d.append(dd)
        if len(self.c) > K:
            self.c.pop(0); self.d.pop(0)
        self.n = min(self.n + 1, K)


def model():
    import numpy as np
    rng = np.random.default_rng(9)

    def run(counts):
        w, out = Win(), []
        for x in counts:
            w.step(int(x), 100000)
            sc, sd = w.sums()
            out.append(sc * 1e6 / sd)
        return np.array(out)

    print("фон B имп/с, скачок в момент 60 с; отклик - медиана (90 %) секунд, пока цифра")
    print("не пройдёт половину скачка и не удержится; покой - разброс без источника")
    for B in (30.0, 300.0):
        row = []
        for r1 in (B * 1.3, B * 1.5, B * 2, B * 10, B / 2):
            T = []
            for _ in range(40):
                rate = np.where(np.arange(1000) < 600, B, r1)
                y = run(rng.poisson(rate * 0.1))
                mid, up = (B + r1) / 2, r1 > B
                k = next((i for i in range(600, 995) if all((y[j] > mid) if up else (y[j] < mid)
                                                            for j in range(i, i + 5))), 1000)
                T.append((k - 599) * 0.1)
            row.append(f"{np.median(T):4.1f}({np.percentile(T, 90):4.1f})")
        S = [np.std(run(rng.poisson(np.full(2400, B) * 0.1))[600:]) / B for _ in range(20)]
        print(f"B={B:5.0f}: x1.3 {row[0]}  x1.5 {row[1]}  x2 {row[2]}  x10 {row[3]}  /2 {row[4]} с;"
              f" покой ±{100 * np.mean(S):.1f} %")


def device(ip, sec):
    """прибор против модели на одной и той же истории по 0.1 с"""
    import json, struct, time, urllib.request
    def g(p):
        for k in range(5):          # прибор иногда рвёт соединение при частых опросах
            try:
                return urllib.request.urlopen(f"http://{ip}{p}", timeout=5).read()
            except OSError:
                time.sleep(0.2)
        raise SystemExit(f"нет ответа от {ip}{p}")
    polls, t1 = [], time.time() + sec
    while time.time() < t1:
        polls.append(json.loads(g("/cps")))
        time.sleep(0.25)
    S, first, frm = [], None, 0
    while True:                     # /hist отдаёт не больше 1024 отсчётов за раз
        b = g(f"/hist?f=1&from={frm}")
        hd = struct.unpack("<7I", b[:28])
        if first is None:
            first = hd[1]
        n = hd[4]
        S += [struct.unpack("<IIH", b[28 + 10 * i:38 + 10 * i]) for i in range(n)]
        frm = hd[1] + n
        if n == 0 or frm >= hd[2]:
            break
    # модель с самого старого отсчёта: окно не длиннее 30 с, так что после
    # 30 с от начала истории состояние уже не зависит от того, что было раньше
    w, at, cuts = Win(), {}, []
    for i, (end_ds, c, dur) in enumerate(S):
        n0 = w.n
        w.step(c, dur * 1000)
        at[first + i] = (w.n, w.sums()[0])
        if w.n < min(n0 + 1, K) and i >= 400:   # окно сократилось (полное 30 с - не в счёт)
            cuts.append(first + i)
    ok = bad = skip = 0
    for p in polls:
        hi = p.get("hi")
        if hi is None or hi not in at or hi - first < 400:
            skip += 1
            continue
        mn, mc = at[hi]
        if (mn, mc) == (p["cpwn"], p["cpsn"]):
            ok += 1
        else:
            bad += 1
            if bad <= 5:
                print(f"  отсчёт {hi}: прибор окно {p['cpwn']} инт., {p['cpsn']} соб.; модель {mn} инт., {mc} соб.")
    his = [p["hi"] for p in polls if p.get("hi") is not None]
    inside = [c for c in cuts if his and min(his) <= c <= max(his)]
    print(f"срабатываний детектора за время опросов: {len(inside)}"
          + (f" (отсчёты {inside[:12]}{' ...' if len(inside) > 12 else ''})" if inside else ""))
    print(f"опросов {len(polls)}: совпало {ok}, расхождений {bad}, пропущено {skip} "
          f"(нет в истории или первые 40 с истории)")
    return bad == 0 and ok > 0


if __name__ == "__main__":
    if len(sys.argv) > 1:
        sys.exit(0 if device(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 30) else 1)
    model()
