"""Многоуровневая проверка тракта обработки MCA. Железо не нужно.

    python tools/test_dsp.py

Модель берёт константы прямо из main/mca_config.h, поэтому тесты
следуют за прошивкой при изменении размера чанка и числа каналов.
"""
import math
import os
import random
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dspmodel import (DSP, CAP_CHUNK_SAMPLES, MCA_ADC_MAX, MCA_CHANNELS,
                      Params, make_pulse_train, run)

fails = []


def check(name, cond, detail=""):
    print(f"  [{'OK ' if cond else 'ПРОВАЛ'}] {name}" +
          (f"  -- {detail}" if detail else ""))
    if not cond:
        fails.append(name)


print(f"\nконфигурация из mca_config.h: чанк {CAP_CHUNK_SAMPLES} отсч, "
      f"каналов {MCA_CHANNELS}, шкала АЦП 0..{MCA_ADC_MAX}")

# ------------------------------------------------------------------ L1
print("\n=== УРОВЕНЬ 1: базовая работоспособность ===")
d = run(make_pulse_train(4096, [800], [1500]), Params())
check("одиночный импульс детектируется", len(d.events) == 1,
      f"каналы: {[e[0] for e in d.events]}")

lin = []
for a in (200, 400, 800, 1600):
    dd = run(make_pulse_train(4096, [a], [1500]), Params())
    if dd.events:
        lin.append((a, dd.events[0][0]))
print("  амплитуда -> канал: " +
      ", ".join(f"{a}->{c}" for a, c in lin))
ratios = [c / a for a, c in lin]
check("отклик линеен", len(ratios) == 4 and
      (max(ratios) - min(ratios)) / statistics.mean(ratios) < 0.05,
      f"разброс {100*(max(ratios)-min(ratios))/statistics.mean(ratios):.1f}%")

# ------------------------------------------------------------------ L2
print("\n=== УРОВЕНЬ 2: события на стыке чанков ===")
print("  Моноэнергетика без шума, считаем интегрированием (Способ 1):")
print("  оно берёт отсчёты вокруг запомненного положения пика, поэтому")
print("  ошибка индекса видна сразу. У самого интегрирования есть разброс")
print("  в пару процентов (плавает оценка базы), поэтому считаем только")
print("  грубые выбросы - дальше 10% от основного канала.\n")

# Интервалы СЛУЧАЙНЫЕ. При постоянном шаге фазы импульсов относительно
# границы чанка ложатся на решётку и могут целиком перепрыгнуть узкое
# окно, где проявляется ошибка: шаг 2000 при чанке 2046 попадал устаревшим
# индексом на следующий импульс, шаг 3001 давал решётку в 136 отсчётов
# при окне в 50. Зерно фиксировано - результат воспроизводим.
N, AMP = 200, 800
_rng = random.Random(3)
pos, _t = [], 3000
for _ in range(N):
    pos.append(_t)
    _t += _rng.randint(2500, 3500)
sig = make_pulse_train(pos[-1] + 4000, [AMP] * N, pos)

res = {}
for label, kw in (("без пересчёта peak_pos (старый баг)", {"rebase": False}),
                  ("как в прошивке сейчас", {"rebase": True})):
    dd = run(sig, Params(algo=1), **kw)
    ch = [c for c, _ in dd.events]
    mode = statistics.mode(ch)
    bad = [c for c in ch if abs(c - mode) > 0.1 * mode]
    res[label] = len(bad)
    print(f"  {label}: событий {len(ch)}, основной канал {mode}, "
          f"грубых выбросов {len(bad)} ({100*len(bad)/len(ch):.1f}%)")

check("баг воспроизводится на модели",
      res["без пересчёта peak_pos (старый баг)"] > 0)
check("текущая прошивка выбросов не даёт",
      res["как в прошивке сейчас"] == 0)

# ------------------------------------------------------------------ L3
print("\n=== УРОВЕНЬ 3: разрешение при реальном шуме (15 кодов RMS) ===")
sig = make_pulse_train(pos[-1] + 4000, [AMP] * N, pos, noise_rms=15.0, seed=7)
dd = run(sig, Params())
ch = [c for c, _ in dd.events]
med = statistics.median(ch)
# Ширину считаем по событиям пика: одно шумовое срабатывание далеко
# от пика раздувает СКО в разы и делает число бессмысленным.
core = [c for c in ch if abs(c - med) < 0.1 * med]
stray = len(ch) - len(core)
fwhm = 2.355 * statistics.pstdev(core)
print(f"  событий {len(ch)} (импульсов {N}): в пике {len(core)}, "
      f"вне пика {stray}")
print(f"  центр {med:.0f}, FWHM {fwhm:.1f} кан = {100*fwhm/med:.2f}%")
check("все импульсы найдены", len(core) == N, f"в пике {len(core)} из {N}")
# Порог 20 при шуме 15 кодов - это около 4 сигм выхода трапеции,
# на полумиллионе отсчётов единичные ложные срабатывания ожидаемы.
check("ложных срабатываний единицы", stray <= 3, f"вне пика {stray}")

# ------------------------------------------------------------------ L4
print("\n=== УРОВЕНЬ 4: «кодов на канал» ===")
print("  Канал = амплитуда / кодов на канал. Амплитуда делится целиком,")
print("  без округления до кода, поэтому доли кода на канал дают")
print("  настоящую подробность, а не «гребёнку» из пустых каналов.\n")
meds, off4 = {}, {}
for cpc in (2000, 1000, 500, 250):
    ch = [c for c, _ in run(sig, Params(cpc=cpc)).events]
    meds[cpc] = statistics.median(ch)
    off4[cpc] = sum(1 for c in ch if c % 4) / len(ch)
    print(f"  {cpc/1000:5.2f} кода на канал: пик в канале {meds[cpc]:6.0f}, "
          f"каналов не кратных 4: {100*off4[cpc]:3.0f}%")
check("канал обратно пропорционален «кодов на канал»",
      all(abs(meds[c] * c / (meds[1000] * 1000) - 1) < 0.01 for c in meds),
      ", ".join(f"{c/1000}: {meds[c]:.0f}" for c in meds))
check("при 0.25 кода на канал заняты все каналы, не только кратные 4",
      off4[250] > 0.5, f"не кратных 4: {100*off4[250]:.0f}%")

# ------------------------------------------------------------------ L5
print("\n=== УРОВЕНЬ 5: шкала и запас АЦП ===")
print("  База в СЕРЕДИНЕ шкалы АЦП (2048), импульсы положительные:")
print("  амплитуда не может превысить 4095-2048 = 2047 кодов.")
print("  Верх шкалы = каналов x кодов на канал.\n")

BASE = 2048
headroom = MCA_ADC_MAX - BASE


def one(amp_in, **pk):
    dd = run(make_pulse_train(6000, [amp_in], [2500], baseline=BASE),
             Params(**pk))
    return (dd.events[0][0] if dd.events else None), dd.overflow


print("  1 код на канал и 2048 каналов (по умолчанию) - шкала до 2048 кодов:")
for amp_in in (500, 1000, 2000, 4000, 8000):
    c, _ = one(amp_in)
    tag = "  <- уже в ограничении АЦП" if amp_in > headroom else ""
    print(f"    вход {amp_in:5d} кодов -> канал {c:5d} "
          f"({100*c/2048:3.0f}% из 2048){tag}")
c2000, _ = one(2000)
c4000, _ = one(4000)
c8000, _ = one(8000)
# Трапеция отдаёт чуть меньше высоты входа (спад успевает пройти за
# окно L: 2000 -> ~1886), поэтому запас «90% шкалы», а не впритык.
check("2048 каналов по 1 коду покрывают весь запас АЦП",
      0.9 * 2048 < c2000 < 2048, f"канал {c2000}")
check("после ограничения канал не растёт и остаётся в шкале",
      c4000 == c8000 and c8000 < 2048, f"{c4000} и {c8000}")
c05, _ = one(1800, cpc=500)
print(f"  0.5 кода на канал: вход 1800 кодов -> канал {c05} из 4096")
check("0.5 кода на канал разворачивает тот же запас на 4096 каналов",
      0.8 * 4096 < c05 < 4096, f"канал {c05}")

# ------------------------------------------------------------------ L6
print("\n=== УРОВЕНЬ 6: осциллограф - синхронизация по фронту ===")
from scopemodel import (Scope, LEN as SLEN, PRE as SPRE, SPAN as SSPAN,
                        FAST as SFAST, AMP_WIN as SAMP_WIN)

FS = 10e6
CHUNK_US = CAP_CHUNK_SAMPLES / FS * 1e6
LVL = 30


def scope_run(sig, mode, neg=False, **kw):
    # Период снимков и ожидание авто уменьшены в сто раз, чтобы тест
    # шёл быстро; логика движка от этого не меняется.
    s = Scope(level=LVL, snap_us=1000, auto_us=1500, neg=neg, **kw)
    now = 0.0
    step = CAP_CHUNK_SAMPLES
    for off in range(0, len(sig) - step + 1, step):
        s.feed(sig[off:off + step], mode, now, off)
        now += CHUNK_US
    return s


def run_scope(sig, mode, neg=False, **kw):
    return scope_run(sig, mode, neg, **kw).pub


def rise_at(buf, i):
    return buf[i] - buf[i - SSPAN]


# импульсы как у вас на экране: ~60 кодов, фронт 0.5 мкс, спад 4.3 мкс
rng6 = random.Random(11)
p6, t6 = [], 5000
while t6 < 1_500_000:
    p6.append(t6)
    t6 += rng6.randint(3000, 20000)
sig6 = make_pulse_train(t6 + 5000, [60] * len(p6), p6, fs_hz=FS,
                        rise_ns=500, tau_us=4.3, noise_rms=3.0, seed=5)

pub = run_scope(sig6, 2)
tpos = sorted(set(t for _, t, _, _ in pub))
print(f"  ждущий, импульсы есть: снимков {len(pub)}, "
      f"момент синхронизации в отсчёте {tpos} (предыстория {SPRE})")
check("снимки идут", len(pub) >= 10, f"{len(pub)}")
check("фронт всегда в одном месте окна", tpos == [SPRE], f"{tpos}")
check("окно непрерывно - совпадает с куском исходного сигнала",
      all(cap == sig6[a:a + SLEN] for cap, _, _, a in pub))
check("срабатывание ровно на пересечении уровня",
      all(rise_at(cap, t) >= LVL > rise_at(cap, t - 1)
          for cap, t, _, _ in pub))

pa = run_scope(sig6, 1)
print(f"  авто, импульсы есть: снимков {len(pa)}, из них "
      f"синхронизированных {sum(1 for _, t, _, _ in pa if t >= 0)}")
check("авто при импульсах синхронизируется",
      sum(1 for _, t, _, _ in pa if t == SPRE) >= 0.8 * len(pa))

quiet = make_pulse_train(600_000, [], [], fs_hz=FS, noise_rms=3.0, seed=6)
qa = run_scope(quiet, 1)
qn = run_scope(quiet, 2)
print(f"  без импульсов: авто дал {len(qa)} снимков, ждущий {len(qn)}")
check("авто без фронта пускает развёртку свободно",
      len(qa) > 0 and all(t == -1 for _, t, _, _ in qa))
check("свободные окна тоже непрерывны",
      all(cap == quiet[a:a + SLEN] for cap, _, _, a in qa))
check("ждущий без фронта ничего не публикует", len(qn) == 0)

# Окно под развёртку: на развёртке 512 страница просит n = 512 + запас
# трапеции (L+G+16) и pre = пятая часть экрана + тот же запас. Прибор
# собирает ровно столько, с фронтом на месте pre, а не всё окно, - и
# такое окно помещается во внутреннюю память (DIAG_SCOPE_FAST).
M6 = 20 + 44 + 16
W6, P6 = 512 + M6, round(512 * 0.2) + M6
ws = scope_run(sig6, 2, want=W6, pre=P6)
wlen = sorted(set(len(cap) for cap, _, _, _ in ws.pub))
wtg = sorted(set(t for _, t, _, _ in ws.pub))
print(f"  развёртка 512: снимков {len(ws.pub)}, длина окна {wlen} "
      f"(ждали {W6}), фронт на {wtg} (ждали {P6})")
check("окно собирается под развёртку",
      len(ws.pub) >= 10 and wlen == [W6] and W6 <= SFAST and wtg == [P6]
      and all(cap == sig6[a:a + len(cap)] for cap, _, _, a in ws.pub))
# Развёртка 8192 - самая длинная, что должна влезать во внутреннюю память.
M8 = 64 + 128 + 16
check("окно развёртки 8192 с наибольшим запасом трапеции влезает во "
      "внутреннюю память", 8192 + M8 <= SFAST,
      f"окно {8192 + M8}, внутренний буфер {SFAST}")
# Самая короткая развёртка: после фронта всё равно не меньше AMP_WIN -
# иначе высоту импульса не измерить.
wz = scope_run(sig6, 2, want=64 + M6, pre=13 + M6)
zlen = sorted(set(len(cap) for cap, _, _, _ in wz.pub))
check("короткое окно - предыстория + не меньше поиска высоты",
      len(wz.pub) >= 10 and zlen == [13 + M6 + SAMP_WIN], f"{zlen}")

# Синхронизация по амплитуде: импульсы двух высот вперемешку, фильтр
# пропускает только высокие.
rng6a = random.Random(12)
pa6, aa6, ta6 = [], [], 5000
while ta6 < 1_500_000:
    pa6.append(ta6)
    aa6.append(rng6a.choice((60, 200)))
    ta6 += rng6a.randint(3000, 20000)
siga = make_pulse_train(ta6 + 5000, aa6, pa6, fs_hz=FS, rise_ns=500,
                        tau_us=4.3, noise_rms=3.0, seed=13)
fa = scope_run(siga, 2, amin=140, amax=320)
fall = scope_run(siga, 2)
print(f"  фильтр 140..320: снимков {len(fa.pub)}, высоты "
      f"{min(fa.amps) if fa.amps else '-'}..{max(fa.amps) if fa.amps else '-'}, "
      f"отброшено {sum(fa.rejs)}; без фильтра высоты "
      f"{min(fall.amps)}..{max(fall.amps)}")
check("фильтр амплитуды пропускает только импульсы в диапазоне",
      len(fa.pub) >= 5 and all(140 <= a <= 320 for a in fa.amps)
      and sum(fa.rejs) > 0,
      f"снимков {len(fa.pub)}, отброшено {sum(fa.rejs)}")
check("без фильтра видны обе высоты",
      min(fall.amps) < 140 and max(fall.amps) >= 140)
check("окна с фильтром тоже непрерывны и с фронтом на месте",
      all(cap == siga[a:a + len(cap)] for cap, _, _, a in fa.pub)
      and sorted(set(t for _, t, _, _ in fa.pub)) == [SPRE])

# ------------------------------------------------------------------ L7
print("\n=== УРОВЕНЬ 7: импульсы вниз (полярность «−») ===")
print("  Тот же сигнал, перевёрнутый относительно шкалы АЦП: база наверху,")
print("  импульсы идут вниз. С полярностью «−» всё должно совпасть.\n")
sig7 = make_pulse_train(pos[-1] + 4000, [AMP] * N, pos, noise_rms=15.0, seed=7)
inv7 = [MCA_ADC_MAX - v for v in sig7]
chp = [c for c, _ in run(sig7, Params()).events]
chn = [c for c, _ in run(inv7, Params(polarity=1)).events]
ch0 = [c for c, _ in run(inv7, Params()).events]
print(f"  вверх, полярность +: событий {len(chp)}; вниз, полярность −: "
      f"{len(chn)}; вниз без переключения: {len(ch0)}")
check("полярность «−» даёт тот же спектр, что и прямой сигнал", chp == chn)
# Без переключения импульсы вниз НЕ пропадают: после каждого из них
# трапеция даёт положительный провал-«отражение», он пересекает порог.
# Событий даже больше, чем импульсов, но все они в низких каналах -
# на месте настоящего пика нет ни одного.
mp = sorted(chp)[len(chp) // 2]
near0 = sum(1 for c in ch0 if abs(c - mp) < 0.1 * mp)
check("без переключения полярности спектр ложный (нет событий у пика)",
      near0 == 0 and len(ch0) > 0,
      f"событий {len(ch0)}, у настоящего пика ({mp}) - {near0}")

inv6 = [MCA_ADC_MAX - v for v in sig6]
pn7 = run_scope(inv6, 2, neg=True)
check("осциллограф синхронизируется по фронту вниз",
      len(pn7) >= 10 and sorted(set(t for _, t, _, _ in pn7)) == [SPRE]
      and all(cap == inv6[a:a + SLEN] for cap, _, _, a in pn7),
      f"снимков {len(pn7)}")

# ------------------------------------------------------------------ L8
print("\n=== УРОВЕНЬ 8: ускорение без изменения результата ===")
print("  Прошивка считает разность трапеции для всего чанка векторными")
print("  командами (16 бит с насыщением), порог проверяет восьмёрками по")
print("  максимуму и переворачивает полярность словами по два отсчёта.")
print("  Прежний поштучный и новый варианты прогоняются на одних и тех же")
print("  сигналах: события, каналы, гистограмма и состояние фильтра обязаны")
print("  совпасть, а насыщение 16 бит - ни разу не наступить.\n")
from dspmodel import invert_words


def same(sig8, p, **new):
    a = run(sig8, p)
    b = run(sig8, p, **new)
    return a, b, (a.events == b.events and a.hist == b.hist
                  and a.trap == b.trap and a.overflow == b.overflow)


# Смесь случайных амплитуд и интервалов: срабатывания ложатся на все
# восемь позиций в восьмёрке и на остаток чанка, не кратный восьми.
rng8 = random.Random(8)
pos8, t8 = [], 1000
while t8 < 400_000:
    pos8.append(t8)
    t8 += rng8.randint(150, 3000)
amp8 = [rng8.randint(40, 1800) for _ in pos8]
mix = make_pulse_train(t8 + 4000, amp8, pos8, noise_rms=6.0, seed=9)

# Крайние отсчёты 0 и 4095 вперемешку: разность достигает ±8190 -
# проверка, что 16 бит хватает и насыщение не наступает.
rng7 = random.Random(7)
extreme = [MCA_ADC_MAX if rng7.random() < 0.5 else 0 for _ in range(60_000)]

lanes, sats = [0] * 9, 0
# L и G с разными остатками от деления на 8 - это разные сдвиги
# выравнивания у трёх невыровненных потоков векторной разности.
for name, s8, p8 in (("основной L=20 G=44", mix, Params()),
                     ("L=7 G=13", mix, Params(trap_L=7, trap_G=13)),
                     ("L=3 G=9", mix, Params(trap_L=3, trap_G=9)),
                     ("L=8 G=16", mix, Params(trap_L=8, trap_G=16)),
                     ("L=64 G=128", mix, Params(trap_L=64, trap_G=128,
                                                search=200, rearm=192)),
                     ("интегрирование", mix, Params(algo=1)),
                     ("низкий порог, шум", sig, Params(threshold=4)),
                     ("мелкие импульсы", sig6[:400_000], Params(threshold=8)),
                     ("крайние 0/4095", extreme, Params(trap_L=5, trap_G=11))):
    a, b, ok = same(s8, p8, vec=True)
    lanes = [x + y for x, y in zip(lanes, b.lanes)]
    sats += b.sat
    check(f"векторно = поштучно: {name}", ok and b.sat == 0,
          f"событий {len(a.events)} и {len(b.events)}, насыщений {b.sat}")
print(f"  где сработал порог: позиции 0-7 в восьмёрке {lanes[:8]}, "
      f"в остатке {lanes[8]}; насыщений 16 бит всего {sats}")
check("срабатывания были на всех позициях восьмёрки и в остатке",
      all(lanes), f"{lanes}")

# Переворот словами - перебором: каждая 16-битная половина при разных
# соседях, включая мусор в старших битах (маска обязана его убрать).
bad8 = 0
others = (0, 1, 0x0FFF, 0x1000, 0x7FFF, 0x8000, 0xF000, 0xFFFF, 0xABCD)
for v in range(65536):
    ref = MCA_ADC_MAX - (v & 0xFFF)
    for o in others:
        ro = MCA_ADC_MAX - (o & 0xFFF)
        if invert_words([v, o]) != [ref, ro]:
            bad8 += 1
        if invert_words([o, v]) != [ro, ref]:
            bad8 += 1
check("переворот словами = поштучно для всех 65536 значений половины",
      bad8 == 0, f"расхождений {bad8}")
check("нечётная длина: последний отсчёт переворачивается по одному",
      invert_words([5, 6, 0x1007]) ==
      [MCA_ADC_MAX - 5, MCA_ADC_MAX - 6, MCA_ADC_MAX - 7])

rng9 = random.Random(10)
dirty = [(MCA_ADC_MAX - v) | (rng9.randint(0, 15) << 12) for v in mix]
a, b, ok = same(dirty, Params(polarity=1), word_invert=True)
check("полярность «−»: словами = поштучно на всём тракте, с мусором "
      "в старших битах", ok, f"событий {len(a.events)}")
a, b, ok = same(dirty, Params(polarity=1, algo=1), word_invert=True,
                vec=True)
check("оба ускорения вместе, интегрирование", ok,
      f"событий {len(a.events)}")

# ------------------------------------------------------------------ L9
print("\n=== УРОВЕНЬ 9: сжатие осциллограммы для WebSocket ===")
print("  Прошивка (scope_codec.c) сжимает кадр кусками по 4 КБ, страница")
print("  (wdec) расшифровывает. Модель прошивки - scopecodec.py; wdec берётся")
print("  прямо из mca_web.c и гоняется в node. Всё обязано совпасть бит в бит.\n")
import json
import re
import subprocess
import tempfile
import scopecodec
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_i18n as ci

FS9 = 20e6


def frame9(n, cps, noise, seed):
    r = random.Random(seed)
    ps, t = [], 100
    while t < n - 10:
        ps.append(t)
        t += max(1, int(r.expovariate(cps / FS9)))
    am = [r.choice((80, 300, 1000, 1500)) for _ in ps]
    return make_pulse_train(n, am, ps, fs_hz=FS9, rise_ns=1300, tau_us=4.0,
                            noise_rms=noise, seed=seed)


rng9c = random.Random(99)
cases9 = {
    "развёртка 512, 7000 имп/с": frame9(592, 7000, 1.5, 1),
    "развёртка 16384, 7000 имп/с": frame9(16592, 7000, 1.5, 2),
    "развёртка 32768, 20000 имп/с": frame9(32976, 20000, 2.0, 3),
    "крайние 0/4095 вперемешку": [rng9c.choice((0, 4095)) for _ in range(3000)],
    "разности ровно ±7 и ±8": [2048 + (7 if i % 4 == 1 else -8 if i % 4 == 3
                                       else 0) for i in range(1001)],
    "один отсчёт": [1234],
    "пусто": [],
}
enc9 = {}
ok_rt = True
for name, s in cases9.items():
    parts = scopecodec.encode(s)
    blob = b''.join(parts)
    enc9[name] = blob
    back = scopecodec.decode(blob, len(s))
    ratio = (2 * len(s) / len(blob)) if blob else 0
    print(f"  {name}: {len(s)} отсчётов, {2 * len(s)} -> {len(blob)} байт "
          f"(x{ratio:.1f}), кусков {len(parts)}")
    ok_rt &= back == [v & 0xFFF for v in s]
check("модель: сжатие и расшифровка совпадают бит в бит", ok_rt)

# куски разной длины - состояние переживает границы, результат тот же
s9 = cases9["развёртка 16384, 7000 имп/с"]
same9 = all(b''.join(scopecodec.encode(s9, fc, c)) == enc9["развёртка 16384, 7000 имп/с"]
            for fc, c in ((2, 2), (5, 7), (100, 3000)))
check("сжатие не зависит от нарезки на куски", same9)
big = cases9["развёртка 32768, 20000 имп/с"]
check("длинная развёртка сжимается больше чем в 3 раза",
      2 * len(big) / len(enc9["развёртка 32768, 20000 имп/с"]) > 3.0)

# wdec из самой страницы
src9 = ci.strip_c_comments(open(ci.SRC, encoding='utf-8').read())
page9 = None
for m9 in re.finditer(r'static const char (\w+)\[\]\s*=(.*?);\n', src9, re.S):
    if m9.group(1) == 'PAGE':
        page9 = ''.join(ci.expand(ci.c_tokens(m9.group(2)), ci.macros(src9)))
i9 = page9.find('function wdec(') if page9 else -1
if i9 < 0:
    check("wdec найдена на странице", False)
else:
    depth, j9 = 0, i9
    while True:
        ch = page9[j9]
        depth += ch == '{'
        depth -= ch == '}'
        j9 += 1
        if ch == '}' and depth == 0:
            break
    js = page9[i9:j9] + """
var fs=require('fs'),inp=JSON.parse(fs.readFileSync(process.argv[2],'utf8')),res={};
for(var k in inp){var b=Buffer.from(inp[k][0],'hex');
res[k]=wdec(new Uint8Array(b.buffer,b.byteOffset,b.length),inp[k][1])}
console.log(JSON.stringify(res));"""
    with tempfile.TemporaryDirectory() as td:
        jf = os.path.join(td, 'wdec.js')
        df = os.path.join(td, 'in.json')
        open(jf, 'w', encoding='utf-8').write(js)
        # ключи - номерами: русские портятся в выводе node под Windows
        keys9 = list(cases9)
        json.dump({f"c{i}": [enc9[k].hex(), len(cases9[k])]
                   for i, k in enumerate(keys9)}, open(df, 'w'))
        try:
            r9 = subprocess.run(['node', jf, df], capture_output=True, text=True)
            out9 = json.loads(r9.stdout) if r9.returncode == 0 else None
        except FileNotFoundError:
            out9 = None
    if out9 is None:
        check("node запустил wdec", False, "нужен node")
    else:
        bad = [k for i, k in enumerate(keys9)
               if out9.get(f"c{i}") != [x & 0xFFF for x in cases9[k]]]
        check("wdec страницы расшифровывает кадры прошивки бит в бит",
              not bad, f"расхождения: {bad}")

# ------------------------------------------------------------------ L10
print("\n=== УРОВЕНЬ 10: мониторинг CPS - интервалы ===")
print("  Страница (mbuck в mca_web.c) складывает односекундные отсчёты прибора")
print("  в интервалы по часам. Сверка с независимым расчётом здесь: на")
print("  разрывах, при дрожании тика и незаконченном последнем интервале.\n")


def mbuck_ref(T, C, D, step):
    """законченные интервалы [b, b+step): сумма импульсов и времени набора"""
    if not T:
        return [], [], []
    last = T[-1]
    acc = {}
    for t, c, d in zip(T, C, D):
        b = (t // step) * step
        if b + step > last + 500:
            continue
        s = acc.setdefault(b, [0, 0])
        s[0] += c
        s[1] += d
    keys = sorted(acc)
    return keys, [acc[k][0] for k in keys], [acc[k][1] for k in keys]


r10 = random.Random(10)
T10, C10, D10, t = [], [], [], 1_700_000_000_000
for i in range(9000):
    t += 1000 + r10.randint(-40, 40)          # тик главного цикла гуляет
    if i in (2000, 5000):
        t += r10.randint(90_000, 400_000)     # набор стоял
    T10.append(t)
    C10.append(r10.randint(150, 260))
    D10.append(r10.randint(950, 1000))
i10 = page9.find('function mbuck(') if page9 else -1
if i10 < 0:
    check("mbuck найдена на странице", False)
else:
    depth, j10 = 0, i10
    while True:
        ch = page9[j10]
        depth += ch == '{'
        depth -= ch == '}'
        j10 += 1
        if ch == '}' and depth == 0:
            break
    js10 = page9[i10:j10] + """
var fs=require('fs'),a=JSON.parse(fs.readFileSync(process.argv[2],'utf8')),r={};
[1000,5000,60000,3600000].forEach(function(s){r[s]=mbuck(a.T,a.C,a.D,s)});
console.log(JSON.stringify(r));"""
    with tempfile.TemporaryDirectory() as td:
        jf = os.path.join(td, 'mbuck.js')
        df = os.path.join(td, 'in.json')
        open(jf, 'w', encoding='utf-8').write(js10)
        json.dump({"T": T10, "C": C10, "D": D10}, open(df, 'w'))
        r = subprocess.run(['node', jf, df], capture_output=True, text=True)
        out10 = json.loads(r.stdout) if r.returncode == 0 else None
    if out10 is None:
        check("node запустил mbuck", False, r.stderr[-200:])
    else:
        bad10 = []
        for s in (1000, 5000, 60000, 3600000):
            kt, kc, kd = mbuck_ref(T10, C10, D10, s)
            o = out10[str(s)]
            if (o['t'], o['c'], o['d']) != (kt, kc, kd):
                bad10.append(s)
            print(f"  интервал {s // 1000} с: {len(kt)} интервалов, "
                  f"импульсов {sum(kc)} из {sum(C10)}")
        check("интервалы страницы совпадают с независимым расчётом",
              not bad10, f"расхождения на шагах {bad10}")
        # законченные интервалы теряют только хвост: не больше одного шага
        kt, kc, kd = mbuck_ref(T10, C10, D10, 60000)
        tail = sum(c for t, c in zip(T10, C10) if t >= kt[-1] + 60000)
        check("в интервалы не попал только незаконченный хвост",
              sum(kc) + tail == sum(C10), f"хвост {tail}")

# ------------------------------------------------------------------ L11
print("\n=== УРОВЕНЬ 11: мониторинг CPS - время отсчётов без разрывов ===")
print("  Прибор кладёт отсчёт раз в секунду со своим временем (0.1 с), страница")
print("  дочитывает их порциями и переводит в часы браузера (mofs). Порции")
print("  приходят с задержкой сети; от неё время соседних отсчётов не должно")
print("  гулять - иначе секунда остаётся пустой, и график рвётся.\n")

def page_fn(name):
    """текст функции страницы по имени (скобки считаются до парной)"""
    i = page9.find('function ' + name + '(') if page9 else -1
    if i < 0:
        return None
    depth, j = 0, i
    while True:
        ch = page9[j]
        depth += ch == '{'
        depth -= ch == '}'
        j += 1
        if ch == '}' and depth == 0:
            return page9[i:j]


r12 = random.Random(12)
# часы браузера минус часы прибора, мс: фаза секунды у них случайная (тут
# 870 мс) - от неё зависит, доходит ли разброс задержки до границы секунды
OFS12 = 1_700_000_000_870
batches12, T12true = [], []
dev_ms = 5_000                     # время прибора, мс
pending = []
for i in range(1800):              # 30 минут непрерывного набора
    dev_ms += 1000
    pending.append(dev_ms // 100)  # end_ds - как mca_hist_push
    # запрос страницы раз в секунду, в случайный момент секунды
    t_resp = dev_ms + r12.randint(0, 999)
    nds = t_resp // 100            # время прибора в ответе, до 0.1 с
    now = OFS12 + t_resp + r12.randint(5, 300)   # пришёл с задержкой сети
    batches12.append((now, nds, pending))
    T12true += [OFS12 + e * 100 for e in pending]
    pending = []

js12 = (page_fn('mofs') or '') + '\n' + (page_fn('mbuck') or '')
check("mofs и mbuck найдены на странице",
      'function mofs(' in js12 and 'function mbuck(' in js12)
js12 = "var MH={o:null};\n" + js12 + """
var fs=require('fs'),a=JSON.parse(fs.readFileSync(process.argv[2],'utf8')),T=[],C=[],D=[];
a.forEach(function(b){var o0=mofs(MH,b[0],b[1]);b[2].forEach(function(e){T.push(o0+e*100);C.push(100);D.push(1000)})});
var B=mbuck(T,C,D,1000);console.log(JSON.stringify({T:T,bt:B.t}));"""


def gaps12(bt):
    return sum(1 for i in range(1, len(bt)) if bt[i] - bt[i - 1] > 1500)


with tempfile.TemporaryDirectory() as td:
    jf = os.path.join(td, 'mon.js')
    df = os.path.join(td, 'in.json')
    open(jf, 'w', encoding='utf-8').write(js12)
    json.dump(batches12, open(df, 'w'))
    r = subprocess.run(['node', jf, df], capture_output=True, text=True)
    out12 = json.loads(r.stdout) if r.returncode == 0 else None
if out12 is None:
    check("node запустил mofs/mbuck", False, r.stderr[-300:])
else:
    sh = [t - u for t, u in zip(out12['T'], T12true)]
    # прежний расчёт: «сейчас минус возраст» для каждой порции отдельно
    T_old = [now - (nds - e) * 100 for now, nds, ends in batches12 for e in ends]
    bt_old = sorted({t // 1000 * 1000 for t in T_old})
    print(f"  разрывов внутри 30 мин набора: прежний расчёт {gaps12(bt_old)}, "
          f"страница сейчас {gaps12(out12['bt'])}")
    check("прежний расчёт времени давал разрывы (проверка самой проверки)",
          gaps12(bt_old) > 0)
    check("время отсчётов на странице идёт ровно через 1 с",
          max(sh) - min(sh) == 0, f"разброс сдвига {max(sh) - min(sh)} мс")
    check("в непрерывном наборе нет пустых секунд", gaps12(out12['bt']) == 0,
          f"разрывов {gaps12(out12['bt'])}")

# ------------------------------------------------------------------ L12
print("\n=== УРОВЕНЬ 12: фильтр формы импульса ===")
print("  Сигнал как у шумного ФЭУ (замер 2026-11-03, 20 МГц): сцинтилляции -")
print("  фронт 10 отсч., спад 50 отсч.; шумовые всплески - фронт 3, спад 7, ниже")
print("  по амплитуде. Эталон набирается из сигнала, фильтр должен убрать всплески")
print("  и оставить сцинтилляции. Классы различаются амплитудой (всплески 20-60")
print("  кодов, сцинтилляции от 150), поэтому проверка не зависит от самой меры.\n")
FS12 = 20e6
rnd12 = random.Random(12)
N_REAL, N_NOISE, STEP12 = 1200, 400, 300
kinds12 = [1] * N_REAL + [0] * N_NOISE
rnd12.shuffle(kinds12)
pos12 = [200 + k * STEP12 + rnd12.randint(0, 40) for k in range(len(kinds12))]
ra = [rnd12.uniform(150, 1000) if k else 0 for k in kinds12]
na = [0 if k else rnd12.uniform(20, 60) for k in kinds12]
n12 = pos12[-1] + 1000
real12 = make_pulse_train(n12, [a for a in ra if a], [p for p, k in zip(pos12, kinds12) if k],
                          fs_hz=FS12, rise_ns=500, tau_us=2.5, clip=False)
noise12 = make_pulse_train(n12, [a for a in na if a], [p for p, k in zip(pos12, kinds12) if not k],
                           fs_hz=FS12, rise_ns=150, tau_us=0.35, baseline=0, clip=False)
g12 = random.Random(7)
sig12 = [max(0, min(MCA_ADC_MAX, r + n + int(round(g12.gauss(0, 1.6)))))
         for r, n in zip(real12, noise12)]
cfg12 = dict(threshold=10, trap_L=12, trap_G=122, rearm=23, search=56,
             int_rise=13, int_fall=36, cpc=1000, nch=2048)


def run12(p, learn=False, **kw):
    d = DSP(p, **kw)
    if learn:
        d.start_learn()
    for off in range(0, len(sig12) - CAP_CHUNK_SAMPLES + 1, CAP_CHUNK_SAMPLES):
        d.process(sig12[off:off + CAP_CHUNK_SAMPLES])
    return d


def cls12(d, split=100):
    """(всплесков всего, принято; сцинтилляций всего, принято) - по каналу.
    При интегрировании амплитуда - средняя высота в окне (~0.6 высоты у
    сцинтилляции, ~0.3 у всплеска), граница - 50."""
    lo = [ok for ch, w, ok in d.widths if ch < split]
    hi = [ok for ch, w, ok in d.widths if ch >= split]
    return len(lo), sum(lo), len(hi), sum(hi)


# ширина сцинтилляции из формы генератора (независимо от прошивки):
# площадь в окне вокруг вершины / высота
rs12, tau12 = int(500e-9 * FS12), 2.5e-6 * FS12
shape12 = [0.0] * 20 + [(k + 1) / rs12 if k < rs12 else math.exp(-(k - rs12) / tau12)
                        for k in range(400)]
pk12 = max(range(len(shape12)), key=lambda k: shape12[k])
w_true12 = sum(shape12[pk12 - 13:pk12 + 37])
p_off = Params(**cfg12)
d_off = run12(p_off, shape_watch=True)
p_l = Params(**cfg12)
d_l = run12(p_l, learn=True)
q10, q50, q90 = d_l.finish_learn()
print(f"  эталон: {q50 / 10:.1f} отсч. (10-90 %: {q10 / 10:.1f}-{q90 / 10:.1f}), "
      f"из {len(d_l.learn)} событий; по форме генератора {w_true12:.1f}")
check("эталон совпал с шириной сцинтилляции по форме генератора (±5 %)",
      abs(q50 / 10 - w_true12) <= 0.05 * w_true12, f"{q50 / 10:.1f} против {w_true12:.1f}")
# средний импульс, который показывает страница: по форме генератора,
# отсчёт за отсчётом от вершины (каждый импульс - своей высотой к 1)
wv12, pre12, nw12 = d_l.wave
# у формы генератора вершина - два отсчёта по 1.0, средний импульс может
# взять вершиной любой из них: сравниваем со сдвигом в пределах отсчёта
dev12 = min(max(abs(y / 1000 - shape12[pk12 + j - pre12 + sh]) for j, y in enumerate(wv12))
            for sh in (-1, 0, 1))
print(f"  средний импульс: {len(wv12)} отсч., вершина {pre12}, из {nw12} импульсов, "
      f"наибольшее отличие от формы генератора {dev12:.3f}")
check("средний импульс повторяет форму генератора (отличие не больше 0.05)", dev12 <= 0.05,
      f"{dev12:.3f}")
check("в среднем импульсе вершина там, где её ждёт страница (= 1000)", wv12[pre12] == 1000,
      f"{wv12[pre12]}")
hb12 = max(range(len(d_l.whist)), key=lambda k: d_l.whist[k])
w_hb12 = (hb12 + 0.5) * d_l.whist_win / len(d_l.whist)
check("в гистограмме ширины все измеренные импульсы, горб - у эталона",
      sum(d_l.whist) == len(d_l.widths) and abs(w_hb12 - q50 / 10) <= 1.0,
      f"{sum(d_l.whist)} из {len(d_l.widths)}, горб {w_hb12:.1f} против {q50 / 10:.1f}")
p_on = Params(**cfg12, shape_on=1, shape_tol=25, shape_ref=p_l.shape_ref,
              shape_win=p_l.shape_win)
d_on = run12(p_on)
a0, b0, c0, e0 = cls12(d_off)
a1, b1, c1, e1 = cls12(d_on)
print(f"  без фильтра: всплесков {a0} (в спектре {b0}), сцинтилляций {c0} (в спектре {e0}), "
      f"событий в спектре {len(d_off.events)}")
print(f"  с фильтром:  всплесков {a1} (в спектре {b1}), сцинтилляций {c1} (в спектре {e1}), "
      f"событий в спектре {len(d_on.events)}, отброшено {d_on.shape_rej}")
check("без фильтра всплески попадают в спектр (проверка самой проверки)",
      a0 >= 0.3 * N_NOISE and b0 == a0, f"всплесков {a0}, в спектре {b0}")
check("фильтр убрал не меньше 95 % всплесков", a1 > 0 and b1 <= 0.05 * a1, f"осталось {b1} из {a1}")
check("фильтр оставил не меньше 99 % сцинтилляций", c1 > 0 and e1 >= 0.99 * c1,
      f"осталось {e1} из {c1}")
acc12 = sum(1 for ch, w, ok in d_on.widths if ok and ch < MCA_CHANNELS)
check("в спектре ровно принятые фильтром события", len(d_on.events) == acc12,
      f"{len(d_on.events)} против {acc12}")
# интегрирование: вершину и сумму окна фильтр берёт у amp_integrate -
# разделение классов должно остаться тем же
p_il = Params(**cfg12, algo=1)
d_il = run12(p_il, learn=True)
qi10, qi50, qi90 = d_il.finish_learn()
p_ion = Params(**cfg12, algo=1, shape_on=1, shape_tol=25, shape_ref=p_il.shape_ref,
               shape_win=p_il.shape_win)
d_ion = run12(p_ion)
ai, bi, ci, ei = cls12(d_ion, 50)
print(f"  интегрирование: эталон {qi50 / 10:.1f} отсч.; всплесков {ai} (в спектре {bi}), "
      f"сцинтилляций {ci} (в спектре {ei})")
check("интегрирование: эталон тот же (±5 % от формы генератора)",
      abs(qi50 / 10 - w_true12) <= 0.05 * w_true12, f"{qi50 / 10:.1f} против {w_true12:.1f}")
check("интегрирование: убрано не меньше 95 % всплесков, оставлено не меньше 99 % сцинтилляций",
      ai > 0 and bi <= 0.05 * ai and ci > 0 and ei >= 0.99 * ci,
      f"всплесков осталось {bi} из {ai}, сцинтилляций {ei} из {ci}")
# ---- отбор импульсов в эталон (как запись эталона в BecqMoni)
p_n = Params(**cfg12, shape_ln=500)
d_n = run12(p_n, learn=True)
check("в эталон берётся заданное число импульсов (500)", len(d_n.learn) == 500, f"{len(d_n.learn)}")
p_r = Params(**cfg12, shape_lmin=300, shape_lmax=700)
d_r = run12(p_r, learn=True)
hr = [h / 8 for h in d_r.learn_h]
check("в эталон - только высоты от 300 до 700 кодов",
      len(hr) > 100 and min(hr) >= 300 and max(hr) <= 700,
      f"взято {len(hr)}, высоты {min(hr):.0f}-{max(hr):.0f}" if hr else "не взято ни одного")
# зашкалившие (вершина срезана верхом АЦП) в эталон не идут
rc = random.Random(21)
pc = [300 + k * 320 + rc.randint(0, 40) for k in range(300)]
ac = [2500 if k % 3 == 0 else 400 for k in range(300)]
sc = make_pulse_train(pc[-1] + 1000, ac, pc, fs_hz=FS12, rise_ns=500, tau_us=2.5)
gc = random.Random(22)
sc = [max(0, min(MCA_ADC_MAX, v + int(round(gc.gauss(0, 1.6))))) for v in sc]
d_c = DSP(Params(**cfg12))
d_c.start_learn()
for off in range(0, len(sc) - CAP_CHUNK_SAMPLES + 1, CAP_CHUNK_SAMPLES):
    d_c.process(sc[off:off + CAP_CHUNK_SAMPLES])
hc = [h / 8 for h in d_c.learn_h]
check("зашкалившие импульсы в эталон не берутся",
      len(hc) > 150 and max(hc) < 1000, f"взято {len(hc)}, наибольшая высота {max(hc):.0f}" if hc else "не взято ни одного")

# ---- амплитуда свёрткой с эталоном: моноэнергетические импульсы, шум 1.6
# кода, настройки как на приборе (L = 10, G = 51); эталон - с импульсов 400
# кодов. Свёртка должна мерить высоту без смещения и при белом шуме быть
# точнее трапеции на малых высотах. На приборе (шум не белый, форма от
# импульса к импульсу разная) вышло наоборот - см. README.
cfgc = dict(threshold=8, trap_L=10, trap_G=51, rearm=23, search=56, int_rise=13, int_fall=36,
            cpc=1000, nch=2048)


def mono(A, seed):
    rm = random.Random(seed)
    pm = [300 + k * 320 + rm.randint(0, 40) for k in range(600)]
    sm = make_pulse_train(pm[-1] + 1000, [A] * 600, pm, fs_hz=FS12, rise_ns=500, tau_us=2.5, clip=False)
    gm = random.Random(seed + 1)
    return [max(0, min(MCA_ADC_MAX, v + int(round(gm.gauss(0, 1.6))))) for v in sm]


def runc(sig, p, **kw):
    d = DSP(p, **kw)
    for off in range(0, len(sig) - CAP_CHUNK_SAMPLES + 1, CAP_CHUNK_SAMPLES):
        d.process(sig[off:off + CAP_CHUNK_SAMPLES])
    return d


p_cl = Params(**cfgc)
d_cl = DSP(p_cl)
d_cl.start_learn()
s400 = mono(400, 9)
for off in range(0, len(s400) - CAP_CHUNK_SAMPLES + 1, CAP_CHUNK_SAMPLES):
    d_cl.process(s400[off:off + CAP_CHUNK_SAMPLES])
d_cl.finish_learn()
refc = (d_cl.wave[0], d_cl.wave[1], d_cl.wave[2], d_cl.wave_mid)
s30 = mono(30, 3)
res = {}
for algo in (0, 2):
    pc_ = Params(**cfgc, algo=algo, shape_ref=p_cl.shape_ref, shape_win=p_cl.shape_win)
    d_ = runc(s30, pc_, ref_wave=refc)
    a_ = [x for _, x in d_.events]
    m_ = statistics.median(a_)
    a_ = [x for x in a_ if abs(x - m_) < 0.5 * m_]
    res[algo] = (statistics.mean(a_), 235.5 * statistics.pstdev(a_) / statistics.mean(a_), len(a_))
print(f"  высота 30 кодов: трапеция среднее {res[0][0]:.1f}, FWHM {res[0][1]:.2f} %; "
      f"свёртка среднее {res[2][0]:.1f}, FWHM {res[2][1]:.2f} % (событий {res[2][2]})")
pi_ = Params(**cfgc, algo=1)
di_ = runc(s30, pi_)
ai_ = [x for _, x in di_.events]
mi_ = statistics.median(ai_)
mi_ = statistics.mean([x for x in ai_ if abs(x - mi_) < 0.5 * mi_])
# шкала интегрирования без ошибки базы: высота x среднее формы по окну 13 + 36
exp_ = 30 * w_true12 / 50
print(f"  интегрирование тех же импульсов: среднее {mi_:.2f}; без ошибки базы - {exp_:.2f}")
# прежде база интегрирования была бегущей: в неё подмешивались хвосты частых
# импульсов, и амплитуда выходила на ~7 % ниже (17.61). Теперь база - перед
# фронтом этого импульса, как у свёртки
check("интегрирование: база перед фронтом - амплитуда без сноса (±3 % от расчёта)",
      abs(mi_ - exp_) <= 0.03 * exp_, f"{mi_:.2f} против {exp_:.2f}")
check("свёртка - в шкале интегрирования (высота x среднее формы по окну, ±3 %)",
      abs(res[2][0] - exp_) <= 0.03 * exp_, f"{res[2][0]:.2f} против {exp_:.2f}")
check("свёртка на малых высотах точнее трапеции (белый шум)", res[2][1] < 0.85 * res[0][1],
      f"{res[2][1]:.2f} % против {res[0][1]:.2f} %")
# ---- длинный фронт (20 отсч.) при коротком окне «до вершины» (10): база
# перед окном ложилась на фронт - у эталона в начале было -0.40 вместо 0,
# свёртка проигрывала трапеции (прибор, 2026-11-04). База - от пробоя порога.
cfgs = dict(cfgc, int_rise=10, int_fall=50)


def slow(A, seed):
    rm = random.Random(seed)
    pm = [300 + k * 400 + rm.randint(0, 40) for k in range(500)]
    sm = make_pulse_train(pm[-1] + 1000, [A] * 500, pm, fs_hz=FS12, rise_ns=1000, tau_us=2.5, clip=False)
    gm = random.Random(seed + 1)
    return [max(0, min(MCA_ADC_MAX, v + int(round(gm.gauss(0, 1.6))))) for v in sm]


p_sl = Params(**cfgs)
d_sl = DSP(p_sl)
d_sl.start_learn()
s_sl = slow(400, 31)
for off in range(0, len(s_sl) - CAP_CHUNK_SAMPLES + 1, CAP_CHUNK_SAMPLES):
    d_sl.process(s_sl[off:off + CAP_CHUNK_SAMPLES])
d_sl.finish_learn()
w_sl, pre_sl, mid_sl = d_sl.wave[0], d_sl.wave[1], d_sl.wave_mid
# та же форма без шума: совмещение по середине фронта, как при наборе
g_sl = make_pulse_train(400, [400], [100], fs_hz=FS12, rise_ns=1000, tau_us=2.5, clip=False)
pk_sl = max(range(len(g_sl)), key=lambda i: (g_sl[i], -i))
h_sl = g_sl[pk_sl] - 2048
c_sl = pk_sl
while (g_sl[c_sl - 1] - 2048) * 2 > h_sl:
    c_sl -= 1
dev_sl = max(abs(w_sl[j] / 1000 - (g_sl[c_sl - mid_sl + j] - 2048) / h_sl)
             for j in range(min(len(w_sl), pre_sl + cfgs["int_fall"] + 1)))
print(f"  длинный фронт: начало эталона {', '.join(f'{v / 1000:.3f}' for v in w_sl[:6])}, "
      f"генератор {', '.join(f'{(g_sl[c_sl - mid_sl + j] - 2048) / h_sl:.3f}' for j in range(6))}")
check("длинный фронт: эталон совпадает с формой генератора (до конца окна, < 0.03)",
      dev_sl < 0.03, f"{dev_sl:.3f}")
ref_sl = (d_sl.wave[0], d_sl.wave[1], d_sl.wave[2], d_sl.wave_mid)
s_sl2 = slow(300, 41)
fw_sl = {}
for algo in (0, 2):
    q_ = Params(**cfgs, algo=algo, shape_ref=p_sl.shape_ref, shape_win=p_sl.shape_win)
    a_ = [x for _, x in runc(s_sl2, q_, ref_wave=ref_sl).events]
    m_ = statistics.median(a_)
    a_ = [x for x in a_ if abs(x - m_) < 0.5 * m_]
    fw_sl[algo] = 235.5 * statistics.pstdev(a_) / statistics.mean(a_)
print(f"  длинный фронт, 300 кодов: FWHM трапеция {fw_sl[0]:.2f} %, свёртка {fw_sl[2]:.2f} %")
check("длинный фронт: свёртка не хуже трапеции", fw_sl[2] <= 1.1 * fw_sl[0],
      f"{fw_sl[2]:.2f} % против {fw_sl[0]:.2f} %")
# ---- скруглённая вершина: шум сдвигает максимум позже, и база ширины «8
# отсчётов перед окном» ложилась на фронт - фильтр отбрасывал настоящие малые
# импульсы (сырые кадры Ra-226, 2026-11-04: 55 % при 16-25 кодах). База - от
# пробоя порога.
def rounded(A, seed, n=500):
    rm = random.Random(seed)
    pm = [300 + k * 400 + rm.randint(0, 40) for k in range(n)]
    sm = make_pulse_train(pm[-1] + 1000, [A] * n, pm, fs_hz=FS12, rise_ns=800, tau_us=2.5, clip=False)
    k9 = 9                                  # скругление вершины: среднее по 9
    sm = [sum(sm[max(0, i - k9 + 1):i + 1]) / min(k9, i + 1) for i in range(len(sm))]
    gm = random.Random(seed + 1)
    return [max(0, min(MCA_ADC_MAX, int(round(v + gm.gauss(0, 1.6))))) for v in sm]


cfgr = dict(cfgc, int_rise=10, int_fall=36)
d_r = DSP(Params(**cfgr))
d_r.start_learn()
s_r = rounded(400, 51)
for off in range(0, len(s_r) - CAP_CHUNK_SAMPLES + 1, CAP_CHUNK_SAMPLES):
    d_r.process(s_r[off:off + CAP_CHUNK_SAMPLES])
d_r.finish_learn()
for A_r in (20, 30):
    q_r = Params(**cfgr, shape_on=1, shape_tol=40, shape_ref=d_r.p.shape_ref,
                 shape_win=d_r.p.shape_win)
    d_q = runc(rounded(A_r, 61 + A_r), q_r)
    fr_r = d_q.shape_rej / max(d_q.shape_chk, 1)
    print(f"  скруглённая вершина, {A_r} кодов: эталон ширины {d_r.p.shape_ref / 10:.1f}, "
          f"отброшено {d_q.shape_rej} из {d_q.shape_chk}")
    check(f"скруглённая вершина, {A_r} кодов: фильтр отбрасывает не больше 3 % настоящих импульсов",
          fr_r <= 0.03, f"{100 * fr_r:.1f} %")
p_w = Params(**cfg12, shape_on=1, shape_tol=25, shape_ref=p_l.shape_ref,
             shape_win=p_l.shape_win)
p_w.int_fall = 30
d_w = run12(p_w)
check("окно сменилось после набора эталона - фильтр ничего не отбрасывает",
      d_w.shape_rej == 0 and d_w.shape_chk == 0, f"отброшено {d_w.shape_rej}")
p_h = Params(**cfg12, shape_on=1, shape_tol=25, shape_ref=p_l.shape_ref,
             shape_win=p_l.shape_win, shape_hz=16000000)
d_h = run12(p_h, fclk=20000000)
check("частота сменилась после набора эталона - фильтр ничего не отбрасывает",
      d_h.shape_rej == 0, f"отброшено {d_h.shape_rej}")

print("\n" + "=" * 62)
if fails:
    print(f"ПРОВАЛЕНО: {len(fails)}")
    for f in fails:
        print("   -", f)
    sys.exit(1)
print("Все проверки пройдены")
