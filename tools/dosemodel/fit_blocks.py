"""G(E) для блоков NaI(Tl) из списка прибора: коэффициенты для
main/mca_dose.c (G_H10, G_KA по блокам) и точность по энергиям и
направлениям. Разрешение в модели - как у NaI 40x40 на приборе (47.2 кэВ
на 609 кэВ); на G оно почти не влияет.

    python fit_blocks.py            # все блоки, ~1 мин на блок"""
import numpy as np
import fitg
from mc import Cyl

BLOCKS = [(25, 25), (30, 40), (30, 70), (40, 40), (63, 63), (80, 80)]   # Ø x H, мм

if __name__ == "__main__":
    out = []
    for D, H in BLOCKS:
        fitg.cyl = Cyl(D / 20, H / 10)
        Mi = fitg.resp_mat("iso"); Ms = fitg.resp_mat("side"); Mt = fitg.resp_mat("top")
        row = {"D": D, "H": H}
        for key, tgt in (("h10", fitg.H10), ("ka", fitg.KA)):
            Dv = tgt(fitg.Ein); a, G = fitg.fitG(Mi, Dv)
            sel = fitg.Ein >= 0.05
            err = {nm: (M @ G) / Dv - 1 for nm, M in (("iso", Mi), ("side", Ms), ("top", Mt))}
            row[key] = a
            row[key + "_err"] = {nm: (100 * np.abs(r[sel]).mean(), 100 * np.abs(r[sel]).max(),
                                      100 * r[np.argmin(np.abs(fitg.Ein - 0.662))]) for nm, r in err.items()}
        out.append(row)
        e = row["h10_err"]
        print(f"NaI Ø{D}x{H}: H*(10) изотропно ср {e['iso'][0]:.1f} % макс {e['iso'][1]:.1f} %; сбоку макс {e['side'][1]:.1f} %; "
              f"в торец макс {e['top'][1]:.1f} %; на 662 кэВ {e['iso'][2]:+.1f}/{e['side'][2]:+.1f}/{e['top'][2]:+.1f} %", flush=True)
    print("\n/* для main/mca_dose.c */")
    for key in ("h10", "ka"):
        print(f"static const double G_{key.upper()}[MCA_DOSE_NDET][6] = {{")
        for r in out:
            print(f"    {{ {', '.join('%.6g' % x for x in r[key])} }},   /* Ø{r['D']}x{r['H']} */")
        print("};")
