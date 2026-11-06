"""Ожидаемый счёт в пике 1461 кэВ от банки KCl под торцом кристалла NaI.

Соль - цилиндр радиуса RS, высоты HS, плотности RHO, излучает 1461 кэВ
равномерно и изотропно. Проверка 2026-11-07: 400 г KCl, банка Ø114x45 мм,
детектор торцом на крышке (стенка 0.5 мм), домик свинец + медь: пик
1461 кэВ 3.51 +- 0.05 имп/с против 2.37-2.67 у модели NaI Ø40x40 (глубина
кристалла в корпусе 2-6 мм) -> поправка k = 0.71 (README, «Мощность дозы»). Кристалл NaI R x H, ось вертикальна, нижний торец
на высоте HS + GAP над дном соли (GAP = стенка банки + стенка блока до
кристалла). Пик - только фотоны, вышедшие из соли без взаимодействия
(ослабление exp(-mu*L)) и отдавшие кристаллу всю энергию."""
import os, sys, numpy as np
from mc import Cyl, XS, transport, load_xs, HERE

E = 1.46082                         # МэВ
YIELD = 0.1066                      # квантов 1461 на распад K-40
# удельная активность KCl: природная доля K-40 1.17e-4, T1/2 1.248e9 лет
NA = 6.02214076e23
lam = np.log(2) / (1.248e9 * 365.25 * 86400)
A_K = NA / 39.0983 * 1.17e-4 * lam          # Бк на грамм калия
A_KCL = A_K * 39.0983 / 74.551             # Бк на грамм KCl


def mu_kcl(e):
    Ek, inc, ph, pp = load_xs(os.path.join(HERE, "xcom_KCl.txt"))
    f = lambda y: np.exp(np.interp(np.log(e), np.log(Ek), np.log(np.maximum(y, 1e-30))))
    return f(inc) + f(ph) + f(pp)          # см^2/г без когерентного


def peak_eff(R, H, RS, HS, RHO, gap, N=2_000_000, seed=1, wall_mu_t=0.0):
    rng = np.random.default_rng(seed)
    cyl = Cyl(R, H)
    xs = XS(3.67, os.path.join(HERE, "xcom_NaI.txt"))
    m = mu_kcl(E) * RHO
    # точки в соли: начало координат - центр кристалла
    z0 = -(H / 2 + gap)                     # верх соли
    r = RS * np.sqrt(rng.random(N)); ph = 2 * np.pi * rng.random(N)
    p = np.stack([r * np.cos(ph), r * np.sin(ph), z0 - HS * rng.random(N)], 1)
    c = 2 * rng.random(N) - 1; a = 2 * np.pi * rng.random(N); s = np.sqrt(1 - c * c)
    d = np.stack([s * np.cos(a), s * np.sin(a), c], 1)
    up = d[:, 2] > 0
    p, d = p[up], d[up]
    tin, _ = cyl.interval(p, d)
    hit = ~np.isnan(tin) & (tin > 0)
    p, d, tin = p[hit], d[hit], tin[hit]
    # путь в соли: до верхней плоскости соли или до боковой стенки
    tz = (z0 - p[:, 2]) / d[:, 2]
    a2 = d[:, 0] ** 2 + d[:, 1] ** 2
    b2 = 2 * (p[:, 0] * d[:, 0] + p[:, 1] * d[:, 1])
    c2 = p[:, 0] ** 2 + p[:, 1] ** 2 - RS ** 2
    with np.errstate(invalid="ignore", divide="ignore"):
        tr = np.where(a2 > 1e-12, (-b2 + np.sqrt(b2 * b2 - 4 * a2 * c2)) / (2 * a2), np.inf)
    L = np.minimum(tz, tr)
    w = np.exp(-m * L) * np.exp(-wall_mu_t / np.maximum(d[:, 2], 1e-3))
    pe = p + d * tin[:, None] * 1.000001 + d * 1e-7
    dep = transport(np.full(len(pe), E), pe, d, cyl, xs, rng)
    pk = np.abs(dep - E) < 0.002
    geo = len(p) / N
    return (w * pk).sum() / N, geo, (w.sum() / N)


if __name__ == "__main__":
    MASS, RS, HS = 400.0, 5.7, 4.5
    RHO = MASS / (np.pi * RS ** 2 * HS)
    print(f"KCl: {A_KCL:.2f} Бк/г, квантов 1461: {A_KCL*YIELD:.3f} /с/г; {MASS:.0f} г -> {MASS*A_KCL*YIELD:.1f} /с; плотность {RHO:.3f} г/см^3, mu {mu_kcl(E)*RHO:.4f} 1/см")
    for gap_mm in (0.5, 3, 5, 8, 12, 20.5, 25.5):
        eff, geo, att = peak_eff(2.0, 4.0, RS, HS, RHO, gap_mm / 10)
        print(f"зазор соль-кристалл {gap_mm:5.1f} мм: пик {eff*100:.3f} % испущенных -> {eff*MASS*A_KCL*YIELD:6.2f} имп/с в пике; "
              f"(попадает в кристалл {geo*100:.2f} %, из них без ослабления в соли {att/geo*100:.1f} %)")
