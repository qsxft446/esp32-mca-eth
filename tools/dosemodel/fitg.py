"""G(E) для NaI 40x40 (коэффициенты - в main/mca_dose.c, G_H10 и G_KA): мощность дозы = сумма по событиям G(E_события).
Отклик - Монте-Карло (mc.py), цели - H*(10) по ICRP 74 (photons_H10.txt из
OpenMC) и керма в воздухе по NIST mu_en/rho."""
import numpy as np, sys
import os
from mc import Cyl, XS, response, HERE

FWA = 47.2 / np.sqrt(609.31)     # FWHM = a*sqrt(E), кэВ: замер 609 кэВ
ETH = 25.0                       # ниже - событий нет (порог), кэВ
cyl = Cyl(2.0, 4.0); xs = XS(3.67, os.path.join(HERE, "xcom_NaI.txt"))
Ein = np.geomspace(0.033, 3.0, 36)   # МэВ
bins = np.arange(0, 3300, 2.0); Ec = (bins[:-1] + bins[1:]) / 2

def resp_mat(geom, N=120000):
    rng = np.random.default_rng(7); M = []
    for i, e in enumerate(Ein):
        dep, A = response(e, N, cyl, xs, geom, seed=100 + i)
        d = dep[dep > 0] * 1000
        d = d + rng.normal(0, 1, len(d)) * FWA * np.sqrt(d) / 2.355
        h, _ = np.histogram(d, bins=bins); M.append(h * A / N)
    return np.array(M)

h10 = np.loadtxt(os.path.join(HERE, "photons_H10.txt"), skiprows=3)
H10 = lambda e: np.exp(np.interp(np.log(e), np.log(h10[:, 0]), np.log(h10[:, 1])))
air = np.loadtxt(os.path.join(HERE, "air_muen.txt"))
KA = lambda e: e * np.exp(np.interp(np.log(e), np.log(air[:, 0]), np.log(air[:, 2]))) * 160.2   # пГр*см^2

def fitG(M, D, K=5, emin=0.045):
    use = Ec >= ETH
    L = np.log(Ec[use] / 1000.0)
    B = np.stack([L ** k for k in range(K + 1)], 1)       # G = sum a_k ln(E)^k
    A = M[:, use] @ B
    w = (Ein >= emin) / D                                  # относительная ошибка
    a, *_ = np.linalg.lstsq(A * w[:, None], D * w, rcond=None)
    G = np.zeros(len(Ec)); G[use] = B @ a
    return a, G

if __name__ == "__main__":
    Mi = resp_mat("iso"); Ms = resp_mat("side"); Mt = resp_mat("top")
    for name, tgt in (("H*(10), пЗв*см^2", H10), ("керма, пГр*см^2", KA)):
        D = tgt(Ein); a, G = fitG(Mi, D)
        print(f"\n{name}: a = {', '.join('%.6g' % x for x in a)}")
        print(" E, кэВ   iso     сбоку   в торец   (расчёт/истина - 1, %)")
        for i in range(0, len(Ein), 3):
            r = [(M[i] @ G) / D[i] - 1 for M in (Mi, Ms, Mt)]
            print(f" {Ein[i]*1000:7.1f} " + "  ".join(f"{100*x:+6.1f}" for x in r))
        sel = Ein >= 0.05
        for nm, M in (("iso", Mi), ("сбоку", Ms), ("в торец", Mt)):
            r = (M @ G) / D - 1
            print(f" {nm:8s}: 50 кэВ-3 МэВ макс |ошибка| {100*np.abs(r[sel]).max():.1f} %, средняя {100*np.abs(r[sel]).mean():.1f} %")
        # простой вариант: G = k*E, k по изотропному
        Gs = np.where(Ec >= ETH, Ec, 0); kk = np.median(D[sel] / (Mi[sel] @ Gs)); r = (Mi @ (kk * Gs)) / D - 1
        print(f" для сравнения G=k*E: от {100*r[sel].min():+.0f} до {100*r[sel].max():+.0f} %")
