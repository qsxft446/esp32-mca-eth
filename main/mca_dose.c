#include "mca_dose.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs.h"
#include "adc_clk.h"
#include "mca_config.h"
#include "mca_dsp.h"
#include "mca_settings.h"

static const char *TAG = "mca_dose";

/* G(E) = сумма a_k * ln(E/МэВ)^k, на одно событие с энергией E (кэВ по
 * калибровке), по блокам NaI(Tl) Ø x H. Подгонка по модели Монте-Карло
 * кристалла в равномерном изотропном поле, 33 кэВ ... 3 МэВ, разрешение
 * 47.2 кэВ на 609 кэВ, порог 25 кэВ (tools/dosemodel/fit_blocks.py,
 * 2026-11-07). Ошибка H*(10) от 50 кэВ до 3 МэВ, изотропно: в среднем
 * 4.8 / 3.8 / 3.4 / 3.2 / 2.0 / 1.5 % (по порядку блоков), не больше
 * 9.5...17 %; источник сбоку - не больше 11...19 %; в торец - до -34 %
 * (Ø30x70 - до -65 %: длинный кристалл, торец мал). Порядок блоков - как
 * в списке на странице (KDET). */
const char *const MCA_DOSE_DET_NAME[MCA_DOSE_NDET] = {
    "NaI 25x25", "NaI 30x40", "NaI 30x70", "NaI 40x40", "NaI 63x63", "NaI 80x80" };
static const double G_H10[MCA_DOSE_NDET][6] = {   /* пЗв на событие: H*(10), ICRP 74 */
    { 4.84569, 5.45657, 1.1763, -0.792993, -0.408574, -0.0521507 },          /* Ø25x25 */
    { 2.12398, 2.39736, 0.551606, -0.324123, -0.174444, -0.02269 },          /* Ø30x40 */
    { 1.22359, 1.38091, 0.32434, -0.18268, -0.0999592, -0.0131087 },         /* Ø30x70 */
    { 1.20549, 1.3611, 0.340599, -0.159453, -0.0916454, -0.0121542 },        /* Ø40x40 */
    { 0.321499, 0.356718, 0.105046, -0.0254623, -0.0185984, -0.00260305 },   /* Ø63x63 */
    { 0.164091, 0.177883, 0.0555584, -0.00859236, -0.00777792, -0.00112838 },/* Ø80x80 */
};
static const double G_KA[MCA_DOSE_NDET][6] = {    /* пГр на событие: керма в воздухе */
    { 4.1189, 4.94124, 1.46596, -0.389687, -0.269052, -0.0362528 },          /* Ø25x25 */
    { 1.81137, 2.16196, 0.643985, -0.171225, -0.119589, -0.0162908 },        /* Ø30x40 */
    { 1.04521, 1.24345, 0.368753, -0.101036, -0.0701444, -0.0095929 },       /* Ø30x70 */
    { 1.03103, 1.2253, 0.376627, -0.0858145, -0.0643976, -0.00891641 },      /* Ø40x40 */
    { 0.276438, 0.320999, 0.106454, -0.0133805, -0.0137275, -0.00199508 },   /* Ø63x63 */
    { 0.141433, 0.160367, 0.054566, -0.00437325, -0.00596057, -0.00089304 }, /* Ø80x80 */
};
#define G_EMIN   25.0     /* ниже порога - вес 0, кэВ                   */
#define G_EMAX 3000.0     /* выше - продление пропорционально энергии  */

/* веса по каналам (PSRAM), парами: [2*ch] - H*(10), [2*ch+1] - керма.
 * Одна таблица - один указатель для обработки: она читает его раз на
 * событие и не увидит половину пары. */
static float          *s_g;
static mca_dose_why_t  s_why = MCA_DOSE_NO_CAL;
static int             s_det = 3;         /* NaI 40x40 - блок, с которого начинали */
static float           s_k[MCA_DOSE_NDET] = { 1, 1, 1, 1, 1, 1 };
static bool            s_cal_dirty = true;
static double          s_c[5];            /* калибровка, кэВ(канал)    */
static int             s_nc;
static char            s_sig[96];         /* подпись калибровки        */
static char            s_built[96];       /* для каких настроек таблицы */
static char            s_cal[MCA_CALIB_MAX + 1];

static double gpoly(const double *a, double e_kev)
{
    if (e_kev < G_EMIN) return 0.0;
    const double e = e_kev > G_EMAX ? G_EMAX : e_kev;
    const double x = log(e / 1000.0);
    double g = 0, xp = 1;
    for (int k = 0; k < 6; k++) { g += a[k] * xp; xp *= x; }
    if (g < 0) g = 0;
    return e_kev > G_EMAX ? g * e_kev / G_EMAX : g;
}

/* "c":[..] и "sig":".." из JSON калибровки страницы */
static bool parse_cal(const char *s)
{
    const char *p = strstr(s, "\"c\":[");
    const char *q = strstr(s, "\"sig\":\"");
    if (!p || !q) return false;
    p += 5;
    s_nc = 0;
    while (s_nc < 5) {
        char *end;
        const double v = strtod(p, &end);
        if (end == p || !isfinite(v)) break;
        s_c[s_nc++] = v;
        p = end;
        while (*p == ' ') p++;
        if (*p == ',') p++; else break;
    }
    q += 7;
    const char *e = strchr(q, '"');
    if (!e || e - q >= (int)sizeof(s_sig) || s_nc < 2) return false;
    memcpy(s_sig, q, e - q);
    s_sig[e - q] = 0;
    return true;
}

static void cur_sig(char *out, size_t n)
{
    mca_params_t p;
    mca_dsp_get_params(&p);
    snprintf(out, n, "%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%lu",
             (long)p.algo, (long)p.int_rise, (long)p.int_fall, (long)p.trap_L,
             (long)p.trap_G, (long)p.flat_avg, (long)p.cpc_milli,
             (long)p.polarity, (unsigned long)adc_clk_get_freq());
}

void mca_dose_init(void)
{
    s_g = heap_caps_calloc(2 * MCA_CHANNELS, sizeof(float), MALLOC_CAP_SPIRAM);
    if (!s_g) {
        s_why = MCA_DOSE_NO_MEM;
        ESP_LOGE(TAG, "нет памяти под таблицы дозы");
    }
}

/* Блок - "dosedet"; поправки - "dk0".."dk5". Прежний "dosek" (одна
 * поправка, до выбора блоков) - поправка блока 40x40. Читается на первом
 * секундном тике, а не в mca_dose_init: NVS поднимает старт сети, позже
 * init, и чтение молча не удавалось - поправка оставалась 1. */
static void load_nvs(void)
{
    nvs_handle_t h;
    const esp_err_t eo = nvs_open("mca_cfg", NVS_READONLY, &h);
    if (eo != ESP_OK && eo != ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGW(TAG, "настройки дозы не прочитаны: %s", esp_err_to_name(eo));
    if (eo == ESP_OK) {
        uint32_t u;
        if (nvs_get_u32(h, "dosedet", &u) == ESP_OK && u < MCA_DOSE_NDET) s_det = (int)u;
        for (int i = 0; i < MCA_DOSE_NDET; i++) {
            char key[8];
            snprintf(key, sizeof(key), "dk%d", i);
            esp_err_t e = nvs_get_u32(h, key, &u);
            if (e != ESP_OK && i == 3) e = nvs_get_u32(h, "dosek", &u);
            if (e == ESP_OK) {
                float k;
                memcpy(&k, &u, 4);
                if (isfinite(k) && k >= 0.05f && k <= 20.0f) s_k[i] = k;
            }
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "блок %s, поправка k = %.3f", MCA_DOSE_DET_NAME[s_det], s_k[s_det]);
}

int mca_dose_det(void) { return s_det; }

static esp_err_t save_u32(const char *key, uint32_t u)
{
    /* запись во флеш - потерянные чанки, как у любых настроек; редко */
    nvs_handle_t h;
    esp_err_t e = nvs_open("mca_cfg", NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_u32(h, key, u);
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK) ESP_LOGW(TAG, "%s не сохранён: %s", key, esp_err_to_name(e));
    return e;
}

esp_err_t mca_dose_set_det(int det)
{
    if (det < 0 || det >= MCA_DOSE_NDET) return ESP_ERR_INVALID_ARG;
    if (det == s_det) return ESP_OK;
    s_det = det;
    s_built[0] = 0;                      /* таблицы - под новый блок */
    return save_u32("dosedet", (uint32_t)det);
}

void mca_dose_calib_changed(void) { s_cal_dirty = true; }
mca_dose_why_t mca_dose_state(void) { return s_why; }
float mca_dose_k(void) { return s_k[s_det]; }

void mca_dose_tick_1s(void)
{
    static bool loaded;
    if (!loaded) {
        loaded = true;
        load_nvs();
    }
    if (!s_g) return;
    if (s_cal_dirty) {
        s_cal_dirty = false;
        s_built[0] = 0;                  /* пересчитать таблицы */
        if (!mca_settings_load_calib(s_cal, sizeof(s_cal))) {
            s_why = MCA_DOSE_NO_CAL;
            s_sig[0] = 0;
            mca_dsp_dose_table(NULL);
            return;
        }
        if (!parse_cal(s_cal)) {
            s_why = MCA_DOSE_BAD_CAL;
            s_sig[0] = 0;
            mca_dsp_dose_table(NULL);
            ESP_LOGW(TAG, "калибровка не разобрана");
            return;
        }
    }
    if (!s_sig[0]) return;
    char now[96];
    cur_sig(now, sizeof(now));
    if (strcmp(now, s_sig) != 0) {
        if (s_why != MCA_DOSE_OTHER_SET) {
            s_why = MCA_DOSE_OTHER_SET;
            s_built[0] = 0;
            mca_dsp_dose_table(NULL);
        }
        return;
    }
    if (strcmp(now, s_built) == 0 && s_why == MCA_DOSE_OK) return;
    /* пересчёт: таблицы выключены на время записи */
    mca_dsp_dose_table(NULL);
    for (int ch = 0; ch < MCA_CHANNELS; ch++) {
        double e = 0, xp = 1;
        for (int i = 0; i < s_nc; i++) { e += s_c[i] * xp; xp *= ch; }
        s_g[2 * ch]     = (float)gpoly(G_H10[s_det], e);
        s_g[2 * ch + 1] = (float)gpoly(G_KA[s_det], e);
    }
    mca_dsp_dose_table(s_g);
    strcpy(s_built, now);
    s_why = MCA_DOSE_OK;
    ESP_LOGI(TAG, "таблицы дозы, %s: E(0)=%.1f кэВ, E(1000)=%.1f кэВ, G(1000)=%.3f пЗв",
             MCA_DOSE_DET_NAME[s_det], s_c[0], s_c[0] + s_c[1] * 1000 + (s_nc > 2 ? s_c[2] * 1e6 : 0),
             s_g[2000]);
}

esp_err_t mca_dose_set_k(float k)
{
    if (!isfinite(k) || k < 0.05f || k > 20.0f) return ESP_ERR_INVALID_ARG;
    s_k[s_det] = k;
    uint32_t u;
    memcpy(&u, &k, 4);
    char key[8];
    snprintf(key, sizeof(key), "dk%d", s_det);
    return save_u32(key, u);
}
