#include "mca_settings.h"
#include "mca_dsp.h"
#include "adc_clk.h"

#include "nvs.h"
#include <stddef.h>
#include "esp_log.h"

static const char *TAG = "settings";

#define NS "mca_cfg"

/* Увеличивать при ЛЮБОЙ правке mca_params_t. Запись от прошивки с другой
 * структурой будет отброшена целиком, а не прочитана вкривь: совпадение
 * размера ещё не значит, что поля лежат на тех же местах. */
#define PARAMS_VER 5        /* 2: полярность; 3: убран архив форм;
                               4: «кодов на канал» вместо усиления;
                               5: окна L, G, поиск, перезапуск во времени */

/* Запись версии 4 - начало нынешней структуры: поля *_ns дописаны в
 * конец. Её окна были в отсчётах на сохранённой частоте - переводим во
 * время по ней же, тогда после обновления обработка остаётся прежней. */
#define PARAMS_V4_LEN offsetof(mca_params_t, trap_L_ns)

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

    /* Частота - первой: по ней переводятся окна записи версии 4. */
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
        mca_dsp_set_freq(adc_clk_get_freq());
        ESP_LOGI(TAG, "частота из сохранённых: %.3f МГц",
                 mca_freq_table[i] / 1e6);
    }
    const uint32_t fhz = adc_clk_get_freq();

    uint8_t ver = 0;
    mca_params_t p;
    size_t len = sizeof(p);
    const bool got = nvs_get_u8(h, "pver", &ver) == ESP_OK &&
                     nvs_get_blob(h, "params", &p, &len) == ESP_OK;
    if (got && ver == PARAMS_VER && len == sizeof(p)) {
        mca_dsp_set_params(&p);     /* пройдут ту же проверку, что из веба */
        ESP_LOGI(TAG, "настройки обработки загружены");
    } else if (got && ver == 4 && len == PARAMS_V4_LEN && fhz) {
        p.trap_L_ns = (int32_t)((int64_t)p.trap_L * 1000000000LL / fhz);
        p.trap_G_ns = (int32_t)((int64_t)p.trap_G * 1000000000LL / fhz);
        p.search_ns = (int32_t)((int64_t)p.search * 1000000000LL / fhz);
        p.rearm_ns  = (int32_t)((int64_t)p.rearm  * 1000000000LL / fhz);
        mca_dsp_set_params(&p);
        mca_dsp_get_params(&p);
        mca_settings_save_params(&p);
        ESP_LOGI(TAG, "настройки прежней версии перенесены: окна L=%ld G=%ld "
                 "нс на %.3f МГц", (long)p.trap_L_ns, (long)p.trap_G_ns,
                 fhz / 1e6);
    } else {
        ESP_LOGW(TAG, "настроек обработки нет или они от другой версии "
                      "прошивки - значения по умолчанию");
    }
    nvs_close(h);
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
