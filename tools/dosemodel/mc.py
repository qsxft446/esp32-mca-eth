"""Монте-Карло отклика цилиндра NaI(Tl) (по умолчанию 40x40 мм) на фотоны.

Физика: фотоэффект (с вылетом K-рентгена йода 28.6 кэВ), комптон по
Клейну-Нишине, рождение пар (две аннигиляционные 511 кэВ). Электроны
отдают энергию на месте. Когерентное рассеяние не учитывается. Корпус,
отражатель и ФЭУ не моделируются. Сечения - NIST XCOM (xcom_NaI.txt).

Поля: iso - равномерное изотропное (точки на диске описанной сферы),
side - пучок сбоку, top - пучок в торец. Результат - спектр выделенной
энергии на единичный флюенс (событий на 1 фотон/см^2)."""
import os
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))

ME = 0.51099895


def load_xs(fn):
    a = np.loadtxt(fn)
    E = a[:, 0].copy()
    for i in range(1, len(E)):          # дубли на краях поглощения
        if E[i] <= E[i - 1]:
            E[i] = E[i - 1] * 1.000001
    return E, a[:, 2], a[:, 3], a[:, 4] + a[:, 5]


class XS:
    def __init__(self, rho, fn):
        self.rho = rho
        self.E, self.inc, self.ph, self.pp = load_xs(fn)
        self.lE = np.log(self.E)

    def mu(self, E):
        le = np.log(E)
        f = lambda y: np.exp(np.interp(le, self.lE, np.log(np.maximum(y, 1e-30))))
        inc, ph, pp = f(self.inc), f(self.ph), f(self.pp)
        pp = np.where(E > 2 * ME, pp, 0.0)
        return inc * self.rho, ph * self.rho, pp * self.rho


def kn_sample(k, rng):
    """Клейн-Нишина: доля энергии eps = E'/E (метод Кана/Батлера)."""
    n = len(k)
    eps = np.empty(n)
    todo = np.arange(n)
    e0 = 1.0 / (1 + 2 * k)
    while len(todo):
        kk = k[todo]
        e0t = e0[todo]
        a1 = -np.log(e0t)
        a2 = 0.5 * (1 - e0t ** 2)
        r1, r2, r3 = rng.random((3, len(todo)))
        use1 = r1 < a1 / (a1 + a2)
        e = np.where(use1, np.exp(-a1 * r2), np.sqrt(e0t ** 2 + (1 - e0t ** 2) * r2))
        t = (1 - e) / (kk * e)
        st2 = t * (2 - t)
        g = 1 - e * st2 / (1 + e ** 2)
        ok = r3 <= g
        eps[todo[ok]] = e[ok]
        todo = todo[~ok]
    return eps


def rotate(d, cost, rng):
    """Повернуть направления d на угол с косинусом cost, азимут случайный."""
    phi = 2 * np.pi * rng.random(len(cost))
    sint = np.sqrt(np.maximum(0, 1 - cost ** 2))
    ux, uy, uz = d.T
    out = np.empty_like(d)
    big = np.abs(uz) > 0.99999
    s = np.sqrt(np.maximum(1e-30, 1 - uz ** 2))
    cp, sp = np.cos(phi), np.sin(phi)
    out[:, 0] = np.where(big, sint * cp, sint * (ux * uz * cp - uy * sp) / s + ux * cost)
    out[:, 1] = np.where(big, sint * sp, sint * (uy * uz * cp + ux * sp) / s + uy * cost)
    out[:, 2] = np.where(big, np.sign(uz) * cost, -sint * cp * s + uz * cost)
    return out / np.linalg.norm(out, axis=1)[:, None]


def iso_dirs(n, rng):
    c = 2 * rng.random(n) - 1
    p = 2 * np.pi * rng.random(n)
    s = np.sqrt(1 - c * c)
    return np.stack([s * np.cos(p), s * np.sin(p), c], 1)


class Cyl:
    def __init__(self, R, H):
        self.R, self.H = R, H

    def interval(self, p, d):
        """t_in, t_out пересечения луча с цилиндром (nan - мимо)."""
        a = d[:, 0] ** 2 + d[:, 1] ** 2
        b = 2 * (p[:, 0] * d[:, 0] + p[:, 1] * d[:, 1])
        c = p[:, 0] ** 2 + p[:, 1] ** 2 - self.R ** 2
        disc = b * b - 4 * a * c
        with np.errstate(invalid="ignore", divide="ignore"):
            sq = np.sqrt(disc)
            t1 = np.where(a > 1e-12, (-b - sq) / (2 * a), -np.inf)
            t2 = np.where(a > 1e-12, (-b + sq) / (2 * a), np.inf)
            inside_r = c <= 0
            t1 = np.where((a <= 1e-12) & ~inside_r, np.nan, t1)
            z1 = (-self.H / 2 - p[:, 2]) / d[:, 2]
            z2 = (self.H / 2 - p[:, 2]) / d[:, 2]
        zl = np.where(np.abs(d[:, 2]) > 1e-12, np.minimum(z1, z2), -np.inf)
        zh = np.where(np.abs(d[:, 2]) > 1e-12, np.maximum(z1, z2), np.inf)
        flat = np.abs(d[:, 2]) <= 1e-12
        outz = flat & (np.abs(p[:, 2]) > self.H / 2)
        tin = np.maximum(t1, zl)
        tout = np.minimum(t2, zh)
        bad = np.isnan(disc) | (disc < 0) | (tin >= tout) | outz | np.isnan(t1)
        return np.where(bad, np.nan, tin), np.where(bad, np.nan, tout)


def transport(E0, p, d, cyl, xs, rng, kx=(0.0286, 0.83 * 0.88), Ecut=0.003):
    """Перенос фотонов внутри цилиндра. p - точки входа, d - направления.
    Возвращает выделенную энергию на исходный фотон."""
    n = len(E0)
    dep = np.zeros(n)
    owner = np.arange(n)
    E = E0.copy()
    while len(E):
        _, tout = cyl.interval(p, d)
        tout = np.nan_to_num(tout, nan=0.0)
        tout = np.maximum(tout, 0)
        mi, mp, mpp = xs.mu(E)
        mt = mi + mp + mpp
        s = -np.log(rng.random(len(E))) / mt
        stay = s < tout
        p, d, E, owner, mi, mp, mpp, mt, s = (x[stay] for x in (p, d, E, owner, mi, mp, mpp, mt, s))
        if not len(E):
            break
        p = p + d * s[:, None]
        r = rng.random(len(E)) * mt
        isph = r < mp
        ispp = (r >= mp) & (r < mp + mpp)
        isc = ~isph & ~ispp
        newp, newd, newE, newo = [], [], [], []
        # фотоэффект: вся энергия, кроме K-рентгена йода, если он вылетел
        if isph.any():
            Ep = E[isph]
            k = (Ep > 0.03317) & (rng.random(isph.sum()) < kx[1])
            dep_here = np.where(k, Ep - kx[0], Ep)
            np.add.at(dep, owner[isph], dep_here)
            if k.any():
                m = np.where(isph)[0][k]
                newp.append(p[m]); newd.append(iso_dirs(len(m), rng))
                newE.append(np.full(len(m), kx[0])); newo.append(owner[m])
        # пары
        if ispp.any():
            m = np.where(ispp)[0]
            np.add.at(dep, owner[m], E[m] - 2 * ME)
            dd = iso_dirs(len(m), rng)
            for sgn in (1, -1):
                newp.append(p[m]); newd.append(sgn * dd)
                newE.append(np.full(len(m), ME)); newo.append(owner[m])
        # комптон
        if isc.any():
            m = np.where(isc)[0]
            k = E[m] / ME
            eps = kn_sample(k, rng)
            cost = 1 - (1 - eps) / (k * eps)
            np.add.at(dep, owner[m], E[m] * (1 - eps))
            E2 = E[m] * eps
            d2 = rotate(d[m], np.clip(cost, -1, 1), rng)
            lo = E2 < Ecut
            np.add.at(dep, owner[m][lo], E2[lo])
            newp.append(p[m][~lo]); newd.append(d2[~lo]); newE.append(E2[~lo]); newo.append(owner[m][~lo])
        if newE:
            p = np.concatenate(newp); d = np.concatenate(newd)
            E = np.concatenate(newE); owner = np.concatenate(newo)
        else:
            break
    return dep


def response(Eg, N, cyl, xs, geom="iso", seed=1):
    """Выделенная энергия N фотонов энергии Eg (МэВ) и площадь A (см^2):
    событий на единичный флюенс = гистограмма * A / N."""
    rng = np.random.default_rng(seed)
    R, H = cyl.R, cyl.H
    if geom == "iso":
        Rs = np.hypot(R, H / 2) * 1.0001
        d = iso_dirs(N, rng)
        r = Rs * np.sqrt(rng.random(N)); ph = 2 * np.pi * rng.random(N)
        u = np.where(np.abs(d[:, 2:3]) < 0.9, np.array([[0, 0, 1.0]]), np.array([[1.0, 0, 0]]))
        e1 = np.cross(d, u); e1 /= np.linalg.norm(e1, axis=1)[:, None]
        e2 = np.cross(d, e1)
        p = (e1 * (r * np.cos(ph))[:, None] + e2 * (r * np.sin(ph))[:, None]) - d * (2 * Rs)
        A = np.pi * Rs ** 2
    elif geom == "side":
        d = np.tile([1.0, 0, 0], (N, 1))
        p = np.stack([np.full(N, -3 * R), (2 * rng.random(N) - 1) * R, (rng.random(N) - 0.5) * H], 1)
        A = 2 * R * H
    else:
        d = np.tile([0, 0, -1.0], (N, 1))
        r = R * np.sqrt(rng.random(N)); ph = 2 * np.pi * rng.random(N)
        p = np.stack([r * np.cos(ph), r * np.sin(ph), np.full(N, H)], 1)
        A = np.pi * R ** 2
    tin, _ = cyl.interval(p, d)
    hit = ~np.isnan(tin)
    p = p[hit] + d[hit] * tin[hit][:, None] * 1.000001 + d[hit] * 1e-7
    dep = np.zeros(N)
    dep[np.where(hit)[0]] = transport(np.full(hit.sum(), Eg), p, d[hit], cyl, xs, rng)
    return dep, A


if __name__ == "__main__":
    cyl = Cyl(2.0, 4.0)
    xs = XS(3.67, os.path.join(HERE, "xcom_NaI.txt"))
    for Eg in (0.0596, 0.662, 1.461):
        for g in ("iso", "side", "top"):
            dep, A = response(Eg, 200000, cyl, xs, g)
            pk = np.abs(dep - Eg) < 0.002
            tot = dep > 0.02
            print(f"{Eg*1000:6.1f} кэВ {g:4s}: полный пик {pk.sum()/len(dep)*A:6.3f} см^2, всего >20 кэВ "
                  f"{tot.sum()/len(dep)*A:6.3f} см^2, пик/всё {pk.sum()/max(1,tot.sum()):.3f}")
