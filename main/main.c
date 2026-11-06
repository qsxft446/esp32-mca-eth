#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "mca_config.h"
#include "adc_clk.h"
#include "adc_cap.h"
#include "mca_dsp.h"
#include "mca_diag.h"
#include "mca_prof.h"
#include "esp_timer.h"
#include "mca_settings.h"
#include "mca_web.h"
#include "mca_emu.h"
#include "mca_hist.h"
#include "mca_dose.h"
#include "mca_ota.h"
#include "mca_oled.h"
#include "mca_log.h"

static const char *TAG = "main";

static bool s_dsp_paused;

/* DSP-задача на ядре 1, отдельно от сети и HTTP (ядро 0). */
static void dsp_task(void *arg)
{
    const uint16_t *data;
    size_t n;

    /* Векторные команды проверяем здесь, в той же задаче и на том же
     * ядре, где потом идёт обработка. */
    mca_dsp_vec_init();

    uint64_t prev_lost = 0;

    while (1) {
        /* Замеры для профиля (галочка «диагностика» на странице): сколько ждали
         * чанк, сколько его обрабатывали, сколько чанков стоит в
         * очереди и не выросли ли потери. Стоит это пары чтений
         * счётчика времени. */
        const int64_t t_wait0 = esp_timer_get_time();
        if (!adc_cap_wait_chunk(&data, &n, 100)) {
            mca_dsp_service(true);      /* захват стоит - запросы целиком */
            continue;
        }
        const int64_t t_work0 = esp_timer_get_time();
        const uint32_t q_depth = (uint32_t)adc_cap_queue_depth();

        /* РАБОТАЕМ ТОЛЬКО НАД ТЕМ, ЧТО ОТКРЫТО.
         *
         * В режиме осциллографа спектр не нужен, а полный фильтр
         * на каждом отсчёте - самая дорогая часть. Раньше он
         * крутился всегда, независимо от того, что на экране.
         * Трапецию для картинки осциллографа считает браузер
         * по этому же снимку. */
        mca_mode_t mode = mca_mode;
        bool need_dsp   = (mode == MCA_MODE_SPECTRUM ||
                           mode == MCA_MODE_PULSE);
        diag_scope_mode_t sm =
              (mode == MCA_MODE_SCOPE)      ? DIAG_SCOPE_AUTO
            : (mode == MCA_MODE_SCOPE_TRIG) ? DIAG_SCOPE_NORMAL
            :                                 DIAG_SCOPE_OFF;

        /* запросы других задач (сброс спектра, новые L/G) - в любом режиме */
        mca_dsp_service(false);
        if (adc_cap_take_gap()) { mca_dsp_flush(); mca_diag_flush(); }
        mca_diag_feed(data, n, sm);

        if (need_dsp) {
            /* По месту, пока очередь далеко не полна: при 12 из 16 DMA
             * через 4 чанка (~0.4 мс на 20 МГц) запишет в этот же буфер -
             * тогда данные лучше унести копией. */
            mca_dsp_process(data, n, q_depth < 12);
        } else if (!s_dsp_paused) {
            /* уходим из режима набора - при возврате фильтру
             * понадобится прогрев, поток для него разорвался */
            mca_dsp_flush();
        }
        s_dsp_paused = !need_dsp;

        const int64_t t_end = esp_timer_get_time();
        mca_prof_chunk(q_depth, (uint32_t)(t_work0 - t_wait0),
                       (uint32_t)(t_end - t_work0));
        const uint64_t lost = adc_cap_chunks_lost();
        if (lost != prev_lost) {
            mca_prof_loss((uint32_t)(lost - prev_lost), q_depth, (uint8_t)mode);
            prev_lost = lost;
        }

        /* Принудительной паузы здесь НЕТ.
         *
         * Она уступала ядро на системный тик каждые 64 чанка,
         * но при 8 МГц за эту миллисекунду контроллер успевал
         * заполнить почти все буферы, и кольцо затирало
         * неразобранное - отсюда были постоянные 2% потерь.
         *
         * Пауза и не нужна: когда обработка успевает, задача
         * сама блокируется на ожидании следующего чанка, и
         * ядро освобождается. А проверка задачи простоя на
         * этом ядре отключена в конфигурации, потому что ядро
         * намеренно отдано под обработку потока. */
    }
}

static void capture_start(void)
{
    esp_err_t e = adc_cap_start();
    if (e != ESP_OK)
        ESP_LOGE(TAG, "старт LCD_CAM с ошибкой: %s — смотрите «диагностику» на странице",
                 esp_err_to_name(e));
}

static void capture_stop(void)
{
    adc_cap_stop();
}

/* Шаг 100 мс: быстрый CPS и отсчёт быстрой истории - события и время
 * набора за шаг (по счётчику набора, как у секундной истории: без потерь
 * и остановок). Номер отсчёта по 0.1 с - счётчиком от первого шага, а не
 * по часам в момент вызова: задержка вызова не сдвигает отсчёт в соседнюю
 * десятую. Пока набор стоит, отсчёты не пишутся. */
static void fast_tick(void *arg)
{
    static bool     init;
    static uint64_t ev0, ms0;
    static double   h0, k0;
    static uint32_t ds;
    uint64_t ev, rn;
    double dh, dk;
    /* один снимок: события, время набора и суммы дозы - под одной блокировкой */
    mca_dsp_dose_snap(&dh, &dk, &ev, &rn);
    const uint64_t ms = rn / 1000000ULL;
    if (!init) {
        ds = (uint32_t)(esp_timer_get_time() / 100000);
        init = true;
    } else {
        ds++;
        /* «Сброс» обнулил счётчики - считаем с нуля */
        const uint64_t dc = ev >= ev0 ? ev - ev0 : ev;
        const uint64_t dm = ms >= ms0 ? ms - ms0 : ms;
        const double ih = dh >= h0 ? dh - h0 : dh;
        const double ik = dk >= k0 ? dk - k0 : dk;
        if (dm) {
            const uint32_t hi = mca_hist_push_fast((uint32_t)dc, (uint32_t)dm, ds);
            mca_dsp_win_step((uint32_t)dc, (uint32_t)dm, (float)ih, (float)ik, hi);
        } else {
            mca_dsp_win_idle();
        }
    }
    ev0 = ev;
    ms0 = ms;
    h0 = dh;
    k0 = dk;
}

void app_main(void)
{
    mca_log_init();             /* лог для страницы «Диагностика» - первым */
    ESP_LOGI(TAG, "=== MCA на AD9226 + ESP32-S3 ===");
    mca_ota_init();             /* слот прошивки и след отката - в лог */

    mca_dsp_init();
    mca_diag_init();
    mca_hist_init();
    mca_dose_init();
    /* Таймер 100 мс: быстрый CPS и быстрая история CPS по 0.1 с. Не главный
     * цикл: у того шаг 100 мс плюс работа (секундный тик - дольше), и
     * отсчёты по 0.1 с ложились бы неровно. */
    {
        static esp_timer_handle_t ft;
        const esp_timer_create_args_t fa = {
            .callback = fast_tick, .name = "cps_fast",
        };
        esp_err_t fe = esp_timer_create(&fa, &ft);
        if (fe == ESP_OK) fe = esp_timer_start_periodic(ft, 100000);
        if (fe != ESP_OK)
            ESP_LOGE(TAG, "таймер быстрого CPS не запущен: %s", esp_err_to_name(fe));
    }

    ESP_ERROR_CHECK(adc_clk_start(mca_freq_table[MCA_FREQ_DEFAULT_IDX]));
    ESP_ERROR_CHECK(adc_cap_init());
    ESP_ERROR_CHECK(mca_web_start());
    mca_oled_start();           /* дисплей SSD1306, если подключён */
    mca_settings_load();        /* NVS к этому моменту поднят стартом сети */
    /* Эмуляция MCA на UART0, если включена: с этого места консоль молчит. */
    mca_emu_init();

    /* ПРИОРИТЕТ 19 - выше TCP/IP (18). Задача TCP/IP не привязана к ядру
     * и, когда ядро 0 занято WiFi, шла на ядро 1 и вытесняла обработку
     * (была 10): запас очереди захвата на 20 МГц всего 1.6 мс. Когда
     * обработка успевает, она сама ждёт чанк, и ядро 1 свободно для
     * остальных. Выше неё на ядре 1 только системная ipc1. */
    xTaskCreatePinnedToCore(dsp_task, "dsp", 8192, NULL, 19, NULL, 1);

    ESP_LOGI(TAG, "свободно внутр. RAM: %u, PSRAM: %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    /* Секундный тик - по часам, а не счётчиком «10 проходов по 100 мс»:
     * у главной задачи самый низкий приоритет, и под нагрузкой ядра 0
     * проходы растягиваются. Сами «в секунду» считаются по фактическому
     * времени (см. mca_diag_tick_1s, mca_dsp_tick_1s, mca_prof_tick_1s). */
    int64_t next_tick_us = esp_timer_get_time() + 1000000;

    while (1) {
        if (mca_cmd_clear) { mca_cmd_clear = false; mca_dsp_reset_spectrum(); }

        if (mca_cmd_freq_idx >= 0) {
            int idx = mca_cmd_freq_idx;
            mca_cmd_freq_idx = -1;
            if (idx < MCA_FREQ_COUNT) {
                bool was = adc_cap_is_running();
                capture_stop();
                adc_clk_set_freq(mca_freq_table[idx]);
                mca_settings_save_freq(idx);
                vTaskDelay(pdMS_TO_TICKS(20));
                if (was) capture_start();
            }
        }

        /* ЗАХВАТ ВКЛЮЧЁН ТОГДА, КОГДА ОН НУЖЕН ОТКРЫТОЙ ВКЛАДКЕ.
         *
         * Раньше «Старт/Стоп» запускали и останавливали сам захват, общий
         * для всех вкладок: остановили осциллограф - встал и набор
         * спектра, запустили осциллограф - спектр пошёл набираться после
         * возврата на свою вкладку. Теперь у спектра и осциллографа свои
         * признаки, а захват здесь подгоняется под открытую вкладку. В
         * режиме осциллографа спектр не набирается (см. dsp_task), так
         * что работающий для осциллографа захват спектр не трогает. */
        {
            const mca_mode_t m = mca_mode;
            const bool scope = (m == MCA_MODE_SCOPE ||
                                m == MCA_MODE_SCOPE_TRIG);
            const bool want  = scope ? mca_scope_run : mca_spec_run;
            if (want != adc_cap_is_running()) {
                if (want) capture_start();
                else      capture_stop();
            }
        }

        const int64_t now_us = esp_timer_get_time();
        if (now_us >= next_tick_us) {
            /* плановое время тика: ровно через 1 с, в отличие от now_us,
             * который опаздывает на долю 100-мс шага цикла */
            const int64_t tick_us = next_tick_us;
            next_tick_us += 1000000;
            if (now_us - next_tick_us > 1000000)   /* сильно отстали - не догоняем */
                next_tick_us = now_us + 1000000;
            mca_dsp_tick_1s();
            mca_dose_tick_1s();         /* калибровка и настройки -> таблицы дозы */
            /* набор эталона формы закончен - эталон в память прибора */
            if (mca_dsp_shape_poll()) {
                static mca_shape_wave_t sw;
                mca_params_t sp;
                mca_dsp_get_params(&sp);
                mca_settings_save_params(&sp);
                mca_dsp_shape_wave(&sw);
                mca_settings_save_shape(&sw);
            }
            mca_oled_tick_1s();
            /* история CPS: секунда, в которую шёл набор спектра */
            {
                uint32_t cnt, dur;
                mca_dsp_last_second(&cnt, &dur);
                if (dur) mca_hist_push(cnt, dur, tick_us);
            }
            mca_diag_tick_1s(adc_clk_get_freq());
            mca_prof_tick_1s();

            /* Если захват включён, а чанки не идут - повторяем фронт
             * кадровой синхронизации. Возможно, один фронт запускает
             * ровно один кадр, и после него модуль снова ждёт старта. */
            if (adc_cap_is_running()) {
                static uint64_t prev_chunks;
                uint64_t now_chunks = adc_cap_chunks_total();
                if (now_chunks == prev_chunks) adc_cap_kick();
                prev_chunks = now_chunks;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
