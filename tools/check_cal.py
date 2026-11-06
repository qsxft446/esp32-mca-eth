"""Проверка калибровки по энергии со страницы спектра (main/mca_web.c).

Ядро калибровки (поиск линий изотопа, подгонка пиков, полином) живёт
строками C в странице. Скрипт достаёт его, собирает синтетические спектры
Ra-226 и Cs-137 с известной шкалой E(канал) и разрешением, прогоняет ядро в
node и сверяет: какие линии найдены, центр (±1 %), FWHM (609 и 662 кэВ -
±4 %, линии с близкими соседями - ±15 %: их фон задевает соседа).
CHECK_CAL_WEB=путь - проверить другой файл страницы (проверка проверки).

    python tools/check_cal.py            (нужен node)
"""
import json, math, os, random, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.environ.get("CHECK_CAL_WEB") or os.path.join(ROOT, "main", "mca_web.c")
START = "КАЛИБРОВКА ПО ЭНЕРГИИ. Ядро"
STOP = '"var CAL={c:null}'


def core_js():
    """строки C между метками -> текст JS"""
    lines = open(WEB, encoding="utf-8").read().splitlines()
    i0 = next(i for i, l in enumerate(lines) if START in l)
    i1 = next(i for i, l in enumerate(lines) if l.strip().startswith(STOP))
    out = []
    for l in lines[i0 + 1:i1]:
        l = l.strip()
        if not (l.startswith('"') and l.endswith('"')):
            continue
        out.append(l[1:-1].replace('\\"', '"').replace("\\\\", "\\"))
    return "\n".join(out)


def spectrum(c, lines, res662, n, seed):
    """счёт по каналам: пики гаусса (FWHM ~ res662 * sqrt(662 * E)), спадающий
    фон и шум Пуассона; шкала E = c0 + c1 k + c2 k^2"""
    rnd = random.Random(seed)
    E = [c[0] + c[1] * k + c[2] * k * k for k in range(n)]
    dE = [c[1] + 2 * c[2] * k for k in range(n)]
    y = []
    for k in range(n):
        v = 4000.0 * math.exp(-max(E[k], 0) / 500.0) + 40.0
        for e0, amp in lines:
            fw = res662 / 100 * math.sqrt(661.66 * e0)
            s = fw / 2.3548
            v += amp * dE[k] / (s * 2.5066) * math.exp(-0.5 * ((E[k] - e0) / s) ** 2)
        y.append(v)
    out = []
    for v in y:   # Пуассон: при больших счётах - нормальное приближение
        out.append(max(0, int(round(v + rnd.gauss(0, math.sqrt(v))))) if v > 30 else
                   sum(1 for _ in range(int(v * 3)) if rnd.random() < 1 / 3))
    return out


CASES = [
    # имя, изотоп, шкала, линии (энергия, площадь), разрешение у 662 кэВ, каналов
    ("Ra-226, 0.25 кода на канал", "ra226", (-10.0, 1.255, 1.6e-5),
     [(351.93, 9e5), (609.31, 1.2e6), (1120.29, 3.5e5), (1764.49, 2.2e5),
      (295.22, 4.5e5), (242.0, 1.8e5), (768.4, 1.1e5), (1238.1, 1.2e5), (77.0, 5e5)], 7.6, 2048),
    ("Ra-226, 1 код на канал", "ra226", (-21.0, 1.612, 1.4e-6),
     [(351.93, 9e5), (609.31, 1.2e6), (1120.29, 3.5e5), (1764.49, 2.2e5),
      (295.22, 4.5e5), (242.0, 1.8e5), (768.4, 1.1e5), (1238.1, 1.2e5), (77.0, 5e5)], 11.0, 2048),
    ("Cs-137", "cs137", (4.0, 1.59, 0.0), [(32.19, 3e4), (661.66, 3e5)], 9.0, 2048),
    ("Cs-137 + K-40", "cs137k40", (3.0, 1.30, 0.0),
     [(32.19, 3e4), (661.66, 3e5), (1460.82, 6e4)], 8.5, 2048),
]


def main():
    js = core_js()
    data = []
    real = json.load(open(os.path.join(ROOT, "tools", "cal_ra226_trap.json"), encoding="utf-8"))
    for name, iso, c, lines, res, n in CASES:
        data.append(dict(name=name, iso=iso, c=c, res=res, a=spectrum(c, lines, res, n, 7)))
    runner = js + r"""
var D = JSON.parse(require('fs').readFileSync(process.argv[2], 'utf8')), out = [];
D.forEach(function (d) {
  var L = calFind(d.a, CALISO[d.iso][1]), c = L.length ? calPoly(L) : null;
  out.push({name: d.name, lines: L.map(function (l) {
    return {E: l.E, ch: l.ch, fwk: c ? calEv(c, l.ch + l.fw / 2) - calEv(c, l.ch - l.fw / 2) : 0}; }), c: c});
});
var q = [-9, 1.25, 1.6e-5, -2e-9, 1e-13], inv = [100, 609.31, 1764.49, 2600].map(function (E) {
  return Math.abs(calEv(q, calChv(q, E)) - E); });
console.log(JSON.stringify({res: out, inv: Math.max.apply(null, inv)}));
"""
    with tempfile.TemporaryDirectory() as td:
        jf, df = os.path.join(td, "r.js"), os.path.join(td, "d.json")
        open(jf, "w", encoding="utf-8").write(runner)
        json.dump(data, open(df, "w"))
        got = json.loads(subprocess.run(["node", jf, df], capture_output=True, text=True, check=True).stdout)
        res, inv = got["res"], got["inv"]
        # настоящий спектр прибора: линии те же, 609 кэВ - там, где показала
        # подгонка гаусс + прямая (±1 %)
        json.dump([dict(name="real", iso="ra226", a=real["spec"])], open(df, "w"))
        rr = json.loads(subprocess.run(["node", jf, df], capture_output=True, text=True, check=True).stdout)["res"][0]
    fails = 0
    ok = inv < 1e-6
    fails += not ok
    print(f"  [{'OK ' if ok else 'ПРОВАЛ'}] полином 4-й степени: энергия -> канал -> энергия, расхождение {inv:.1e} кэВ")
    l609 = [l for l in rr["lines"] if abs(l["E"] - 609.31) < 0.1]
    ok = len(rr["lines"]) == 4 and l609 and abs(l609[0]["ch"] / real["c609"] - 1) < 0.01
    fails += not ok
    print(f"  [{'OK ' if ok else 'ПРОВАЛ'}] спектр прибора (Ra-226, трапеция L10 G40): линий {len(rr['lines'])}, "
          f"609 кэВ у канала {l609[0]['ch']:.1f} (подгонка гаусс + прямая: {real['c609']})" if l609 else
          f"  [ПРОВАЛ] спектр прибора: 609 кэВ не найден")
    for d, r in zip(data, res):
        c0 = d["c"]
        want = {"ra226": [351.93, 609.31, 1120.29, 1764.49], "cs137": [32.19, 661.66],
                "cs137k40": [661.66, 1460.82]}[d["iso"]]
        got = [l["E"] for l in r["lines"]]
        ok_lines = got == want
        dev = 0.0
        ok_fw = True
        if r["c"]:
            for l in r["lines"]:
                k_true = None   # канал линии по настоящей шкале - Ньютоном
                k = (l["E"] - c0[0]) / c0[1]
                for _ in range(20):
                    k -= (c0[0] + c0[1] * k + c0[2] * k * k - l["E"]) / (c0[1] + 2 * c0[2] * k)
                k_true = k
                dev = max(dev, abs(l["ch"] - k_true) / k_true)
                fw_true = d["res"] / 100 * math.sqrt(661.66 * l["E"])
                # строго - у 609 и 662 кэВ; у линий с близкими соседями (352
                # рядом с 295, 1120 рядом с 1238) фон по краям окна задевает
                # соседний пик и FWHM занижается на 5-11 %; рентген бария на
                # пологом фоне - грубо
                tol = 0.04 if l["E"] in (609.31, 661.66) else (0.25 if l["E"] < 100 else 0.15)
                if abs(l["fwk"] / fw_true - 1) > tol:
                    ok_fw = False
        ok = ok_lines and dev < 0.01 and ok_fw
        fails += not ok
        print(f"  [{'OK ' if ok else 'ПРОВАЛ'}] {d['name']}: линии {got}; центр не дальше "
              f"{100 * dev:.2f} % от истинного канала; FWHM, кэВ: " +
              ", ".join(f"{l['E']:.0f}: {l['fwk']:.1f} (истинно {d['res'] / 100 * math.sqrt(661.66 * l['E']):.1f})"
                        for l in r["lines"]))
    print("Все проверки калибровки пройдены" if not fails else f"ПРОВАЛЕНО: {fails}")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
