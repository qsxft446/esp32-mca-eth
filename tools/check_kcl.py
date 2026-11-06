"""Проверка пробы KCl со страницы спектра (main/mca_web.c) против Python.

Ядро пробы (модель Монте-Карло пика 1461 кэВ от банки KCl под торцом
кристалла NaI и подгонка чистого пика по фону и пробе) живёт строками C в
странице. Скрипт достаёт его и сверяет в node:
  1. модель: доля полного пика для нескольких геометрий против
     tools/dosemodel/mc_kcl.py (допуск - 3 сигмы статистики двух моделей, 6 млн квантов);
  2. подгонку: площадь пика K-40 на настоящих спектрах (tools/kcl_test.json,
     фон пустого домика и проба 400 г) против scipy curve_fit в том же окне
     -4...+4.5 сигмы (допуск 3 %).
CHECK_KCL_WEB=путь - проверить другой файл страницы (проверка проверки).

    python tools/check_kcl.py            (нужен node, numpy, scipy)
"""
import json, os, subprocess, sys, tempfile
import numpy as np
from scipy.optimize import curve_fit

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.environ.get("CHECK_KCL_WEB") or os.path.join(ROOT, "main", "mca_web.c")
sys.path.insert(0, os.path.join(ROOT, "tools", "dosemodel"))
import mc_kcl  # noqa: E402

START, STOP = "ПРОБА KCl. Ядро", "конец ядра пробы KCl"
bad = 0


def check(name, ok, info=""):
    global bad
    print(f"  [{'OK ' if ok else 'ОШИБКА'}] {name}  {info}")
    bad += not ok


def core_js():
    lines = open(WEB, encoding="utf-8").read().splitlines()
    i0 = next(i for i, l in enumerate(lines) if START in l)
    i1 = next(i for i, l in enumerate(lines) if STOP in l)
    out = []
    for l in lines[i0 + 1:i1]:
        l = l.strip()
        if l.startswith('"') and l.endswith('"'):
            out.append(l[1:-1].replace('\\"', '"').replace("\\\\", "\\"))
    return "\n".join(out)


GEOS = [  # R, H (см) кристалла; радиус и высота соли, см; масса, г; зазор, см
    (2.0, 4.0, 5.7, 4.5, 400, 0.45),
    (2.0, 4.0, 5.7, 4.5, 400, 0.05),
    (3.15, 6.3, 5.0, 3.0, 300, 0.3),
    (1.25, 2.5, 6.0, 2.0, 250, 0.6),
]
T = json.load(open(os.path.join(ROOT, "tools", "kcl_test.json"), encoding="utf-8"))

js = core_js() + """
var T=%s,G=%s,out={eff:[],peak:null};
G.forEach(function(g){var rho=g[4]/(Math.PI*g[2]*g[2]*g[3]),N=6000000,a=kclEff({R:g[0],H:g[1],rs:g[2],hs:g[3],rho:rho,gap:g[5]},N,kRng(99),null);
out.eff.push([a.s1/N,Math.sqrt(a.s2)/N])});
var E=T.s.d.map(function(_,k){return T.c.reduce(function(a,ci,i){return a+ci*Math.pow(k,i)},0)});
out.peak=kclPeak(T.s.d,T.s.ms/1000,T.bg.d,T.bg.ms/1000,E);
console.log(JSON.stringify(out));
""" % (json.dumps(T), json.dumps(GEOS))

with tempfile.TemporaryDirectory() as td:
    f = os.path.join(td, "kcl.js")
    open(f, "w", encoding="utf-8").write(js)
    r = subprocess.run(["node", f], capture_output=True, text=True, encoding="utf-8")
if r.returncode:
    print(r.stderr[-800:])
    sys.exit("node не выполнил ядро пробы KCl")
J = json.loads(r.stdout)

print("=== модель пика 1461 кэВ: страница против tools/dosemodel/mc_kcl.py ===")
for g, (e_js, s_js) in zip(GEOS, J["eff"]):
    R, H, rs, hs, m, gap = g
    rho = m / (np.pi * rs * rs * hs)
    e_py, _, _ = mc_kcl.peak_eff(R, H, rs, hs, rho, gap, N=6_000_000, seed=11)
    d = e_js / e_py - 1
    tol = 3 * np.sqrt(1 / (e_js * 6e6) + 1 / (e_py * 6e6))   # 3 сигмы статистики двух моделей
    check(f"NaI Ø{R*20:.0f}x{H*10:.0f}, соль Ø{rs*20:.0f}x{hs*10:.0f} мм {m} г, зазор {gap*10:.1f} мм",
          abs(d) < tol, f"страница {e_js*100:.4f} %, Python {e_py*100:.4f} % ({d*100:+.1f} %, допуск ±{tol*100:.1f} %)")

print("=== пик K-40 на спектрах tools/kcl_test.json: страница против curve_fit ===")
s, b = np.array(T["s"]["d"], float), np.array(T["bg"]["d"], float)
ts, tb = T["s"]["ms"] / 1000, T["bg"]["ms"] / 1000
ch = np.arange(len(s)); E = sum(ci * ch ** i for i, ci in enumerate(T["c"]))
net = s / ts - b / tb; var = np.maximum(s, 1) / ts ** 2 + np.maximum(b, 1) / tb ** 2
p = J["peak"]
sg = p["fw"] / 2.3548
w = (E > p["E"] - 4 * sg) & (E < p["E"] + 4.5 * sg)
f = lambda x, A, mu, sgm, p0, p1: A / (sgm * np.sqrt(2 * np.pi)) * np.exp(-0.5 * ((x - mu) / sgm) ** 2) * np.gradient(E).mean() + p0 + p1 * (x - mu)
q, cv = curve_fit(f, E[w], net[w], p0=[p["area"], p["E"], sg, 0.01, 0], sigma=np.sqrt(var[w]), absolute_sigma=True, maxfev=20000)
d = p["area"] / q[0] - 1
check("площадь пика", abs(d) < 0.03, f"страница {p['area']:.3f} ± {p['err']:.3f}, curve_fit {q[0]:.3f} ± {np.sqrt(cv[0,0]):.3f} имп/с ({d*100:+.1f} %)")
check("центр пика", abs(p["E"] - q[1]) < 3, f"{p['E']:.1f} против {q[1]:.1f} кэВ")
check("ширина пика", abs(p["fw"] / (2.3548 * q[2]) - 1) < 0.08, f"FWHM {p['fw']:.1f} против {2.3548*q[2]:.1f} кэВ")

print("Все проверки пробы KCl пройдены" if not bad else f"ОШИБОК: {bad}")
sys.exit(1 if bad else 0)
