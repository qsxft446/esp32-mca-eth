#include "mca_settings.h"
#include "mca_dsp.h"
#include "adc_clk.h"

#include <stddef.h>
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "settings";

#define NS "mca_cfg"

/* Увеличивать при ЛЮБОЙ правке mca_params_t. Запись от прошивки с другой
 * структурой будет отброшена целиком, а не прочитана вкривь: совпадение
 * размера ещё не значит, что поля лежат на тех же местах. */
#define PARAMS_VER 5        /* 2: полярность; 3: убран архив форм;
                               4: «кодов на канал» вместо усиления;
                               5: фильтр формы (поля в конце) */
/* Версия 4 - та же структура без полей фильтра формы в конце: читаем её
 * как начало новой, фильтр получает значения по умолчанию (выключен). */
#define PARAMS_V4_LEN offsetof(mca_params_t, shape_on)

/* ЧАСТОТА ХРАНИТСЯ В ГЕРЦАХ (ключ "fhz").
 *
 * Прежние прошивки хранили номер строки таблицы (ключ "freq"), но
 * таблица выросла с 11 до 15 частот, и прежний номер 10 (16 МГц)
 * означал бы теперь 13.33 МГц. Старый номер переводим по прежней
 * таблице в герцы, и дальше выбирается ближайшая частота. */
static const uint32_t OLD_FREQ_TABLE[11] = {
    1000000, 2000000, 4000000, 5000000, 6666667, 8000000,
    8888889, 10000000, 11428571, 13333333, 16000000
};

static int freq_nearest_idx(uint32_t hz)
{
    int best = 0;
    uint32_t best_d = UINT32_MAX;
    for (int i = 0; i < MCA_FREQ_COUNT; i++) {
        uint32_t d = mca_freq_table[i] > hz ? mca_freq_table[i] - hz
                                            : hz - mca_freq_table[i];
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

void mca_settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "сохранённых настроек нет - значения по умолчанию");
        return;
    }

    uint8_t ver = 0;
    mca_params_t p;
    mca_dsp_get_params(&p);         /* дефолты - для полей, которых нет в v4 */
    size_t len = sizeof(p);
    const bool vok = nvs_get_u8(h, "pver", &ver) == ESP_OK;
    if (vok && ver == PARAMS_VER &&
        nvs_get_blob(h, "params", &p, &len) == ESP_OK && len == sizeof(p)) {
        mca_dsp_set_params(&p);     /* пройдут ту же проверку, что из веба */
        ESP_LOGI(TAG, "настройки обработки загружены");
    } else if (vok && ver == 4 &&
               nvs_get_blob(h, "params", NULL, &len) == ESP_OK &&
               len == PARAMS_V4_LEN &&
               nvs_get_blob(h, "params", &p, &len) == ESP_OK) {
        mca_dsp_set_params(&p);
        ESP_LOGI(TAG, "настройки обработки загружены (прежняя версия, "
                      "фильтр формы выключен)");
    } else {
        ESP_LOGW(TAG, "настроек обработки нет или они от другой версии "
                      "прошивки - значения по умолчанию");
    }

    uint32_t hz = 0;
    uint8_t  fi;
    if (nvs_get_u32(h, "fhz", &hz) != ESP_OK) {
        hz = 0;
        if (nvs_get_u8(h, "freq", &fi) == ESP_OK && fi < 11)
            hz = OLD_FREQ_TABLE[fi];     /* от прежней прошивки */
    }
    if (hz) {
        const int i = freq_nearest_idx(hz);
        adc_clk_set_freq(mca_freq_table[i]);
        ESP_LOGI(TAG, "частота из сохранённых: %.3f МГц",
                 mca_freq_table[i] / 1e6);
    }

    /* эталонный импульс фильтра формы: заголовок и len отсчётов */
    static mca_shape_wave_t w;
    size_t wl = sizeof(w);
    if (nvs_get_blob(h, "shw", &w, &wl) == ESP_OK &&
        wl == offsetof(mca_shape_wave_t, v) + (size_t)w.len * sizeof(int16_t))
        mca_dsp_shape_set_wave(&w);
    nvs_close(h);
}

void mca_settings_save_shape(const mca_shape_wave_t *w)
{
    if (w->len <= 0 || w->len > SHAPE_WAVE_MAX) return;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, "shw", w, offsetof(mca_shape_wave_t, v) +
                                      (size_t)w->len * sizeof(int16_t));
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK)
        ESP_LOGW(TAG, "эталонный импульс не сохранён: %s", esp_err_to_name(e));
}

void mca_settings_save_params(const mca_params_t *p)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_u8(h, "pver", PARAMS_VER);
        if (e == ESP_OK) e = nvs_set_blob(h, "params", p, sizeof(*p));
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK)
        ESP_LOGW(TAG, "настройки не сохранены: %s", esp_err_to_name(e));
}

void mca_settings_save_freq(int idx)
{
    if (idx < 0 || idx >= MCA_FREQ_COUNT) return;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_u32(h, "fhz", mca_freq_table[idx]);
        nvs_erase_key(h, "freq");        /* старый ключ больше не нужен */
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK)
        ESP_LOGW(TAG, "частота не сохранена: %s", esp_err_to_name(e));
}
