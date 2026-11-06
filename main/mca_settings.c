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
#define PARAMS_VER 7        /* 2: полярность; 3: убран архив форм;
                               4: «кодов на канал» вместо усиления;
                               5: фильтр формы (поля в конце);
                               6: отбор импульсов в эталон (в конце);
                               7: структура та же, но ширина меряется
                                  от базы до фронта - эталон ширины v6
                                  ~на 3 отсчёта уже, он сбрасывается */
/* Версии 4 и 5 - та же структура без полей, добавленных позже в конец:
 * читаем их как начало новой, новые поля - значения по умолчанию. */
#define PARAMS_V4_LEN offsetof(mca_params_t, shape_on)
#define PARAMS_V5_LEN offsetof(mca_params_t, shape_lmin)

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
    if (vok && (ver == PARAMS_VER || ver == 6) &&
        nvs_get_blob(h, "params", &p, &len) == ESP_OK && len == sizeof(p)) {
        /* эталон ширины v6 мерян с базой, ложившейся на фронт: сравнивать
         * с ним новую ширину нельзя - без эталона фильтр всё пропускает,
         * а страница просит набрать его заново */
        if (ver == 6) p.shape_ref = 0;
        mca_dsp_set_params(&p);     /* пройдут ту же проверку, что из веба */
        if (ver == 6)
            ESP_LOGW(TAG, "настройки обработки загружены (версия 6): эталон "
                          "фильтра формы мерян по-старому - наберите заново");
        else
            ESP_LOGI(TAG, "настройки обработки загружены");
    } else if (vok && (ver == 4 || ver == 5) &&
               nvs_get_blob(h, "params", NULL, &len) == ESP_OK &&
               len == (ver == 4 ? PARAMS_V4_LEN : PARAMS_V5_LEN) &&
               nvs_get_blob(h, "params", &p, &len) == ESP_OK) {
        mca_dsp_set_params(&p);
        ESP_LOGI(TAG, "настройки обработки загружены (версия %d, новые поля - "
                      "по умолчанию)", (int)ver);
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

size_t mca_settings_load_calib(char *buf, size_t n)
{
    if (n < 2) return 0;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t len = n - 1;
    const esp_err_t e = nvs_get_blob(h, "cal", buf, &len);
    nvs_close(h);
    if (e != ESP_OK) return 0;
    buf[len] = 0;
    return len;
}

/* Запись во флеш останавливает и второе ядро (кэш выключен на время
 * записи) - это потерянные чанки, как и у любых настроек; калибровку
 * сохраняют редко. */
esp_err_t mca_settings_save_calib(const char *s, size_t len)
{
    if (len > MCA_CALIB_MAX) return ESP_ERR_INVALID_SIZE;
    nvs_handle_t h;
    esp_err_t e = nvs_open(NS, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        if (len) e = nvs_set_blob(h, "cal", s, len);
        else {
            e = nvs_erase_key(h, "cal");
            if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
        }
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK)
        ESP_LOGW(TAG, "калибровка не сохранена: %s", esp_err_to_name(e));
    return e;
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
