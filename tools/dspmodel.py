"""Модель обработки из main/mca_dsp.c для проверок без железа.

Повторяет раскладку s_work, сдвиг хвоста через memmove, перенос
состояния автомата между чанками, amp_integrate и
пересчёт амплитуды в номер канала.

Константы НЕ дублируются руками, а читаются из main/mca_config.h -
иначе модель незаметно расходится с прошивкой.

Запуск проверок:  python tools/test_dsp.py
"""
import math
import os
import random
import re

_HDR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    '..', 'main', 'mca_config.h')


def _defines(path):
    txt = open(path, encoding='utf-8').read()
    out = {}
    for name, val in re.findall(r'#define\s+(\w+)\s+(-?\d+)', txt):
        out[name] = int(val)
    return out


_D = _defines(_HDR)
TRAP_L_MAX = _D['TRAP_L_MAX']
TRAP_G_MAX = _D['TRAP_G_MAX']
CAP_CHUNK_SAMPLES = _D['CAP_CHUNK_SAMPLES']
MCA_CHANNELS = _D['MCA_CHANNELS']
MCA_ADC_MAX = _D['MCA_ADC_MAX']
# размеры карты и эталона фильтра формы - из mca_dsp.h
_DS = _defines(os.path.join(os.path.dirname(_HDR), 'mca_dsp.h'))
SHAPE_HB = _DS['SHAPE_HB']
SHAPE_LEARN_N = _DS['SHAPE_LEARN_N']
# средний импульс копится с совмещением по середине фронта (mca_dsp.c)
SHAPE_WAVE_MAX = 8 + 200 + 400 + 1
WOFF = 8 + 200
WBUF = 2 * SHAPE_WAVE_MAX


def cdiv(a, b):
    """Деление как в C: с отбрасыванием дробной части к нулю."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q

DSP_TAIL = TRAP_L_MAX + TRAP_G_MAX + 8
WORK = DSP_TAIL + CAP_CHUNK_SAMPLES

ST_IDLE, ST_PEAK, ST_REARM = 0, 1, 2


class Params:
    """Значения по умолчанию из mca_dsp_init(). При правке дефолтов
    в прошивке поправьте и здесь."""

    def __init__(self, **kw):
        self.threshold = 20
        self.hysteresis = 50
        self.trap_L = 20
        self.trap_G = 44
        self.rearm = 64
        self.search = 70
        self.algo = 0
        self.polarity = 0
        self.int_rise = 20
        self.int_fall = 32
        self.cpc = 1000          # кодов на канал x1000
        self.nch = 2048
        self.baseline_shift = 10
        self.baseline_win = 150
        self.flat_avg = 0
        # фильтр формы (shape_check в mca_dsp.c): выключен, допуск 25 %
        self.shape_on = 0
        self.shape_tol = 25
        self.shape_ref = 0
        self.shape_win = 0
        self.shape_hz = 0
        # какие импульсы брать в эталон (как запись эталона в BecqMoni)
        self.shape_lmin = 0
        self.shape_lmax = 0
        self.shape_ln = SHAPE_LEARN_N
        self.__dict__.update(kw)


class DSP:
    """rebase=True   - пересчёт s_peak_pos после сдвига буфера
                       (исправление, которое стоит в прошивке).
       guard_end=True - запрет чтения за концом свежих данных."""

    def __init__(self, p, rebase=True, guard_end=False, vec=False,
                 word_invert=False, fclk=None, shape_watch=False,
                 ref_wave=None):
        """vec         - как в прошивке: разность трапеции d для всего
                         чанка заранее, в 16-битной арифметике с насыщением
                         (векторные команды), цикл ожидания восьмёрками с
                         проверкой порога по максимуму;
           word_invert - переворот полярности словами по два отсчёта.
        Без флагов - прежний поштучный вариант: тест сравнивает оба.
           fclk        - частота АЦП для сверки с частотой эталона формы
                         (None - не сверять);
           shape_watch - карта на экране: мера формы считается всегда."""
        self.p = p
        self.rebase = rebase
        self.guard_end = guard_end
        self.vec = vec
        self.word_invert = word_invert
        self.lanes = [0] * 9     # где сработал порог: 0..7 в восьмёрке, 8 - остаток
        self.sat = 0             # сколько раз сработало бы насыщение 16 бит
        self.w = [0] * WORK
        self.trap = 0
        self.state = ST_IDLE
        self.peak = 0
        self.peak_pos = 0
        self.since = 0
        self.pre_trap = 0
        self.need_prime = True
        self.prime_left = 0
        self.base_fp = 0
        self.base_init = False
        self.base_lost = 0
        self.hist = [0] * MCA_CHANNELS
        self.overflow = 0
        self.events = []
        self.valid_end = WORK
        # фильтр формы
        self.fclk = fclk
        self.shape_watch = shape_watch
        # эталон для свёртки (algo = 2): (отсчёты x1000, вершина, импульсов,
        # середина фронта) - как s_wave в прошивке
        self.ref_wave = ref_wave
        self.learn_h = []         # высоты импульсов, взятых в эталон, x8
        self.cross_pos = 0
        self.whist = [0] * SHAPE_HB   # гистограмма ширины
        self.whist_win = 0
        self.learn = []           # набранные ширины x10
        self.wsum = []            # сумма импульсов набора над базой, x8 (по ячейкам)
        self.wcnt = []            # сколько импульсов легло в ячейку
        self.learn_need = 0
        self.learn_win = 0
        self.wave = None          # эталонный импульс: (отсчёты x1000, вершина, импульсов)
        self.examples = [None, None]  # последние в допуске / вне: (отсчёты, вершина, высота, ширина x10)
        self.shape_chk = 0
        self.shape_rej = 0
        self.widths = []          # (канал, ширина x10, принято) - для проверок

    # ---- расчёт амплитуды (порт функций из mca_dsp.c) ----
    def amp_integrate(self, peak_pos, rise, fall, L, G, base):
        total = self.valid_end if self.guard_end else WORK
        # не раньше, чем «порог пробит минус L» - как в прошивке
        lo = max(peak_pos - L - G, self.cross_pos - L, 1)
        hi = min(peak_pos + 4, total - 1)
        w = self.w
        mx, pk = -32768, lo
        for i in range(lo, hi + 1):
            if w[i] > mx:
                mx, pk = w[i], i
        self.int_pk = pk          # вершина - её берёт фильтр формы
        a = max(pk - rise, 0)
        b = min(pk + fall, total - 1)
        ssum = sum(w[i] - base for i in range(a, b + 1))
        n = b - a + 1
        return (ssum, n) if n > 0 else (0, 1)

    def record_event(self, num, den):
        """Канал = (num/den) / кодов на канал, дробь не округляется -
        как в прошивке (деление целиком в 64 битах)."""
        d = den * self.p.cpc
        ch = (num * 1000) // d if (d > 0 and num > 0) else 0
        if ch >= MCA_CHANNELS:
            self.overflow += 1
            return
        self.hist[ch] += 1
        self.events.append((ch, num / den))

    # ---- фильтр формы (порт shape_check, набора эталона и mca_dsp_shape_poll) ----
    def start_learn(self):
        """Как запрос набора, выполненный mca_dsp_service."""
        p = self.p
        self.learn = []
        self.learn_win = p.int_rise + p.int_fall + 1
        self.wsum = [0] * WBUF
        self.wcnt = [0] * WBUF
        self.learn_need = p.shape_ln
        self.learn_h = []

    def build_wave(self, n):
        """Как build_wave в прошивке: (отсчёты x1000, вершина, n, середина)."""
        cells = [j for j in range(WBUF) if self.wcnt[j] > 0 and self.wcnt[j] * 10 >= n * 9]
        j0, j1 = (cells[0], cells[-1]) if cells else (0, -1)
        j1 = min(j1, j0 + SHAPE_WAVE_MAX - 1)
        pre, top = j0, 1
        for j in range(j0, j1 + 1):
            m = cdiv(self.wsum[j] * 1000, self.wcnt[j])
            if m > top:
                top, pre = m, j
        wv = []
        for j in range(j0, j1 + 1):
            m = cdiv(self.wsum[j] * 1000, self.wcnt[j])
            wv.append(max(-32768, min(32767, cdiv(m * 1000, top))))
        return (wv, pre - j0, n, WOFF - j0)

    def finish_learn(self):
        """Как mca_dsp_shape_poll: медиана - в эталон, средний импульс -
        в тысячных от его вершины. Модель берёт столько, сколько набрано."""
        v = sorted(self.learn)
        n = len(v)
        p = self.p
        wv, pre, _, mid = self.build_wave(n)
        self.wave = (wv, pre, n)
        self.wave_mid = mid
        p.shape_ref = v[n // 2]
        p.shape_win = self.learn_win
        p.shape_hz = self.fclk or 0
        self.learn_need = 0
        return v[n // 10], v[n // 2], v[n * 9 // 10]

    def amp_conv(self, peak_pos, end):
        """Порт amp_conv: (num, den, вершина) или None - окно не легло."""
        p, w = self.p, self.w
        wv, _, _, mid = self.ref_wave
        cr = [cdiv(v * 256, 1000) for v in wv]
        r2 = sum(r * r for r in cr)
        lo = max(peak_pos - p.trap_L - p.trap_G, self.cross_pos - p.trap_L, 1)
        hi = min(peak_pos + 4, end - 1)
        mx, pk = -1, lo
        for i in range(lo, hi + 1):
            if w[i] > mx:
                mx, pk = w[i], i
        a = pk - p.int_rise
        if a - 32 < 0:                  # база - 32 отсчёта, как в прошивке
            return None
        base32 = sum(w[a - 32:a])
        base8 = base32 // 4
        h8 = mx * 8 - base8
        if h8 <= 0:
            return None
        c50 = pk
        while c50 > a - 8 and w[c50 - 1] * 16 - 2 * base8 > h8:
            c50 -= 1
        s0 = c50 - mid
        if s0 < 0 or s0 + len(cr) > end:
            return None
        acc = sum((w[s0 + j] * 32 - base32) * cr[j] for j in range(len(cr)))
        return acc * 256, 32 * r2, pk

    def shape_check(self, tpk, end, num, den, cmp, filt, learn, ipk=-1, isum=0):
        """ipk >= 0 - вершину и сумму окна (isum, без вычета базы) уже
        нашло интегрирование, как в прошивке."""
        p, w = self.p, self.w
        if ipk >= 0:                    # isum < 0 - сумму считаем здесь (свёртка)
            pk, mx = ipk, w[ipk]
        else:
            lo = max(self.cross_pos - p.trap_L, 1)
            hi = min(tpk + 4, end - 1)
            mx, pk = -1, lo
            for i in range(lo, hi + 1):
                if w[i] > mx:
                    mx, pk = w[i], i
        a, b = pk - p.int_rise, pk + p.int_fall
        if a - 8 < 0 or b >= end:
            return True
        base8 = sum(w[a - 8:a])
        ssum = isum if (ipk >= 0 and isum >= 0) else sum(w[a:b + 1])
        n = b - a + 1
        h8 = mx * 8 - base8
        s8 = ssum * 8 - n * base8
        w10 = (s8 * 10) // h8 if (h8 > 0 and s8 > 0) else 0
        w10 = min(w10, 10 * n)
        if self.whist_win != n:
            self.whist = [0] * SHAPE_HB
            self.whist_win = n
        self.whist[min(w10 * SHAPE_HB // (10 * n), SHAPE_HB - 1)] += 1
        if (learn and n == self.learn_win and h8 > 0 and
                (h8 >= p.shape_lmin * 8 if p.shape_lmin > 0
                 else num >= 3 * p.threshold * den) and
                (p.shape_lmax <= 0 or h8 <= p.shape_lmax * 8)):
            clip = any(w[i] >= MCA_ADC_MAX - 1 or w[i] == 0 for i in range(a - 8, b + 1))
            if len(self.learn) < self.learn_need and not clip:
                self.learn.append(min(w10, 32767))
                self.learn_h.append(h8)
                c50 = pk                # середина фронта, как в прошивке
                while c50 > a - 8 and w[c50 - 1] * 16 - 2 * base8 > h8:
                    c50 -= 1
                for i in range(a - 8, b + 1):    # каждый своей высотой к 1, x1024
                    j = i - c50 + WOFF
                    self.wsum[j] += cdiv((w[i] * 8 - base8) * 1024, h8)
                    self.wcnt[j] += 1
        ok = True
        if cmp:
            tol = p.shape_ref * p.shape_tol // 100
            ok = p.shape_ref - tol <= w10 <= p.shape_ref + tol
        self.examples[0 if ok else 1] = (
            [cdiv(w[i] * 8 - base8, 8) for i in range(a - 8, b + 1)],
            8 + p.int_rise, h8 // 8, w10)
        d = den * p.cpc
        ch = (num * 1000) // d if (d > 0 and num > 0) else 0
        if filt:
            self.shape_chk += 1
            if not ok:
                self.shape_rej += 1
        self.widths.append((ch, w10, ok or not filt))
        return ok or not filt

    # ---- порт mca_dsp_process ----
    def process(self, data):
        p = self.p
        n = len(data)
        L, G = p.trap_L, p.trap_G
        thr = p.threshold * L
        hp = min(max(p.hysteresis, 1), 99)
        thr_lo = thr * hp // 100

        w = self.w
        if p.polarity and self.word_invert:
            w[DSP_TAIL:DSP_TAIL + n] = invert_words(data)
        elif p.polarity:
            w[DSP_TAIL:DSP_TAIL + n] = [MCA_ADC_MAX - (v & 0xFFF) for v in data]
        else:
            w[DSP_TAIL:DSP_TAIL + n] = data
        self.valid_end = DSP_TAIL + n

        if self.need_prime:
            v0 = w[DSP_TAIL]
            for k in range(DSP_TAIL):
                w[k] = v0
            self.trap = 0
            self.state = ST_IDLE
            self.prime_left = L + G + p.search + 4
            self.need_prime = False

        trap = self.trap
        base_fp = self.base_fp
        end = DSP_TAIL + n
        i = DSP_TAIL

        swin = p.int_rise + p.int_fall + 1
        hz_ok = self.fclk is None or p.shape_hz == self.fclk
        shape_cmp = p.shape_ref > 0 and p.shape_win == swin and hz_ok
        conv_ok = (p.algo == 2 and self.ref_wave is not None and
                   len(self.ref_wave[0]) > 0 and shape_cmp)
        shape_filt = shape_cmp and p.shape_on
        shape_learn = self.learn_need > 0
        shape_need = shape_filt or shape_learn or self.shape_watch

        if self.vec:
            # Как diff_vec в прошивке: три операции с насыщением 16 бит
            # в том же порядке (w - w[-L], затем - w[-G], затем + w[-G-L]).
            def sat(v):
                if v > 32767 or v < -32768:
                    self.sat += 1
                    return max(-32768, min(32767, v))
                return v
            dd = [sat(sat(sat(w[a] - w[a - L]) - w[a - G]) + w[a - G - L])
                  for a in range(DSP_TAIL, end)]

            def D(a):
                return dd[a - DSP_TAIL]
        else:
            def D(a):
                return w[a] - w[a - L] - w[a - G] + w[a - G - L]

        while self.prime_left > 0 and i < end:
            trap += D(i)
            self.prime_left -= 1
            i += 1

        for k in range(DSP_TAIL + 8, end, 64):
            v = w[k]
            dev = v - (base_fp >> 8)
            if not self.base_init:
                base_fp = v << 8
                self.base_init = True
            elif -p.baseline_win <= dev <= p.baseline_win:
                base_fp += ((v << 8) - base_fp) >> p.baseline_shift
                self.base_lost = 0
            else:
                self.base_lost += 1
                if self.base_lost > 64:
                    base_fp = v << 8
                    self.base_lost = 0

        while i < end:
            if self.state == ST_IDLE:
                if self.vec:
                    # Как в прошивке: восьмёрками, порог проверяется один
                    # раз по максимуму восьми значений; если он выше
                    # порога - восьмёрка разбирается поштучно.
                    cnt = end - i
                    q = cnt >> 3
                    k = 0
                    while q > 0:
                        t, ts = trap, []
                        for o in range(8):
                            t += D(i + k + o)
                            ts.append(t)
                        if max(ts) > thr:
                            break
                        trap = t
                        k += 8
                        q -= 1
                    if q > 0:
                        for o in range(8):
                            trap += D(i + k)
                            if trap > thr:
                                self.lanes[o] += 1
                                break
                            k += 1
                    else:
                        while k < cnt:
                            trap += D(i + k)
                            if trap > thr:
                                self.lanes[8] += 1
                                break
                            k += 1
                    i += k
                else:
                    while i < end:
                        trap += D(i)
                        if trap > thr:
                            break
                        i += 1
                if i >= end:
                    break
                self.pre_trap = trap
                self.peak = trap
                self.peak_pos = i
                self.cross_pos = i
                self.since = 0
                self.state = ST_PEAK
                i += 1

            if self.state == ST_PEAK:
                fired = False
                while i < end:
                    trap += D(i)
                    if trap > self.peak:
                        self.peak, self.peak_pos = trap, i
                    self.since += 1
                    if self.since >= p.search:
                        ipk, isum = -1, 0
                        if p.algo == 1:
                            ib = base_fp >> 8
                            num, den = self.amp_integrate(
                                self.peak_pos, p.int_rise, p.int_fall,
                                L, G, ib)
                            ipk, isum = self.int_pk, num + den * ib
                        elif conv_ok and self.amp_conv(self.peak_pos, end) is not None:
                            num, den, ipk = self.amp_conv(self.peak_pos, end)
                            isum = -1
                        else:
                            num, den = self.peak, L
                        if (not shape_need or
                                self.shape_check(self.peak_pos, end, num, den,
                                                 shape_cmp, shape_filt, shape_learn,
                                                 ipk, isum)):
                            self.record_event(num, den)
                        self.since = 0
                        self.state = ST_REARM
                        i += 1
                        fired = True
                        break
                    i += 1
                if not fired and i >= end:
                    break

            if self.state == ST_REARM:
                while i < end:
                    trap += D(i)
                    self.since += 1
                    if self.since >= p.rearm and trap < thr_lo:
                        self.state = ST_IDLE
                        i += 1
                        break
                    i += 1

        shift = end - DSP_TAIL
        w[0:DSP_TAIL] = w[shift:shift + DSP_TAIL]
        if self.rebase and self.state == ST_PEAK:
            self.peak_pos -= shift
            self.cross_pos -= shift
            if self.peak_pos < L + G + 1:
                self.state = ST_IDLE
                self.since = 0
        self.trap = trap
        self.base_fp = base_fp


def invert_words(data):
    """Переворот полярности как в прошивке: по два отсчёта в 32-битном
    слове, 0x0FFF0FFF - (слово & 0x0FFF0FFF). Нечётный последний - по
    одному."""
    out = []
    n2 = len(data) // 2
    for k in range(n2):
        word = (data[2 * k] & 0xFFFF) | ((data[2 * k + 1] & 0xFFFF) << 16)
        r = (0x0FFF0FFF - (word & 0x0FFF0FFF)) & 0xFFFFFFFF
        out += [r & 0xFFFF, r >> 16]
    if len(data) & 1:
        out.append(MCA_ADC_MAX - (data[-1] & 0xFFF))
    return out


def run(sig, p, **kw):
    """Прогнать сигнал чанками, как это делает задача dsp_task."""
    d = DSP(p, **kw)
    step = CAP_CHUNK_SAMPLES
    for off in range(0, len(sig) - step + 1, step):
        d.process(sig[off:off + step])
    return d


def make_pulse_train(n_samples, amps, positions, fs_hz=8e6,
                     rise_ns=350, tau_us=18.0, baseline=2048,
                     noise_rms=0.0, seed=1, clip=True):
    """Реальная форма измеренного сигнала: быстрый фронт плюс
    экспоненциальный спад tau=18 мкс, положительная полярность,
    база в середине шкалы АЦП."""
    rnd = random.Random(seed)
    rise = max(1, int(rise_ns * 1e-9 * fs_hz))
    tau = tau_us * 1e-6 * fs_hz
    sig = [float(baseline)] * n_samples
    for amp, pos in zip(amps, positions):
        for k in range(int(tau * 6)):
            idx = pos + k
            if idx >= n_samples:
                break
            if k < rise:
                sig[idx] += amp * (k + 1) / rise
            else:
                sig[idx] += amp * math.exp(-(k - rise) / tau)
    out = []
    for v in sig:
        if noise_rms > 0:
            v += rnd.gauss(0, noise_rms)
        iv = int(round(v))
        if clip:                      # за шкалу АЦП сигнал выйти не может
            iv = max(0, min(MCA_ADC_MAX, iv))
        out.append(iv)
    return out
