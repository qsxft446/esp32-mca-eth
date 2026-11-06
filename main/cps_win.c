#include "cps_win.h"

#include <math.h>
#include <string.h>

void cpsw_reset(cpsw_t *w)
{
    memset(w, 0, sizeof(*w));
}

void cpsw_sum(const cpsw_t *w, uint64_t *cnt, uint64_t *dur_us,
              double *h, double *k, int *n)
{
    uint64_t sc = 0, sd = 0;
    double sh = 0, sk = 0;
    for (int j = 0; j < w->n; j++) {
        const int i = (w->head - 1 - j + 2 * CPSW_K) % CPSW_K;
        sc += w->c[i];
        sd += w->d[i];
        sh += w->h[i];
        sk += w->k[i];
    }
    if (cnt) *cnt = sc;
    if (dur_us) *dur_us = sd;
    if (h) *h = sh;
    if (k) *k = sk;
    if (n) *n = w->n;
}

void cpsw_step(cpsw_t *w, uint32_t dc, uint32_t dd_us, float dh, float dk)
{
    if (w->n > 0) {
        uint64_t sc, sdur;
        cpsw_sum(w, &sc, &sdur, NULL, NULL, NULL);
        /* ожидаемое число событий за этот интервал по скорости окна */
        const double lam = sdur ? (double)sc * (double)dd_us / (double)sdur : 0.0;
        if (lam <= 0.0) {
            if (dc > 0) {           /* было пусто, события пошли */
                w->n = 0;
                w->su = w->sd = 0;
                w->zu = w->zd = 0;
            }
        } else {
            const double lu = log(CPSW_R), ld = -log(CPSW_R);
            w->su += dc * lu - lam * (CPSW_R - 1.0);
            if (w->su < 0) w->su = 0;
            w->zu = w->su > 0 ? w->zu + 1 : 0;
            w->sd += dc * ld - lam * (1.0 / CPSW_R - 1.0);
            if (w->sd < 0) w->sd = 0;
            w->zd = w->sd > 0 ? w->zd + 1 : 0;
            int cut = -1;
            if (w->su > CPSW_H) cut = w->zu - 1;
            else if (w->sd > CPSW_H) cut = w->zd - 1;
            if (cut >= 0) {
                /* окно - с того места, где сумма начала расти (с текущим) */
                if (cut < w->n) w->n = cut;
                w->su = w->sd = 0;
                w->zu = w->zd = 0;
            }
        }
    }
    w->c[w->head] = dc;
    w->d[w->head] = dd_us;
    w->h[w->head] = dh;
    w->k[w->head] = dk;
    w->head = (w->head + 1) % CPSW_K;
    if (w->n < CPSW_K) w->n++;
    w->steps++;
}
