#include "mca_hist.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char *TAG = "mca_hist";

/* Два кольца одного вида: по 1 с (6 ч) и по 0.1 с (10 мин). Отсчёт с
 * номером s лежит в ячейке s % len. Пишут главная задача (1 с) и таймер
 * 100 мс (0.1 с), читает веб. Мьютекс, а не спин-блокировка: копия до
 * 10 КБ из PSRAM, а задача обработки спектра его не берёт - её он не
 * задерживает. */
typedef struct {
    mca_hist_sample_t *ring;
    uint32_t           len;
    uint32_t           next;     /* номер следующего отсчёта */
    uint32_t           count;    /* сколько лежит            */
    uint32_t           epoch;
} hring_t;

static hring_t           s_h[2] = { { .len = MCA_HIST_LEN },
                                    { .len = MCA_HIST_FAST_LEN } };
static SemaphoreHandle_t s_mx;

static uint32_t new_epoch(void)
{
    return esp_random() | 1u;        /* 0 у страницы значит «ещё не знаю» */
}

bool mca_hist_init(void)
{
    s_mx = xSemaphoreCreateMutex();
    bool ok = s_mx != NULL;
    for (int i = 0; i < 2; i++) {
        s_h[i].ring  = heap_caps_calloc(s_h[i].len, sizeof(mca_hist_sample_t),
                                        MALLOC_CAP_SPIRAM);
        s_h[i].epoch = new_epoch();
        if (!s_h[i].ring) ok = false;
    }
    if (!ok) ESP_LOGE(TAG, "нет памяти под историю CPS");
    return ok;
}

static void push(hring_t *h, uint32_t end_ds, uint32_t counts, uint32_t dur_ms)
{
    if (!h->ring || !s_mx) return;
    const mca_hist_sample_t smp = {
        .end_ds = end_ds,
        .counts = counts,
        .dur_ms = (uint16_t)(dur_ms > 65535 ? 65535 : dur_ms),
    };
    xSemaphoreTake(s_mx, portMAX_DELAY);
    h->ring[h->next % h->len] = smp;
    h->next++;
    if (h->count < h->len) h->count++;
    xSemaphoreGive(s_mx);
}

void mca_hist_push(uint32_t counts, uint32_t dur_ms, int64_t t_us)
{
    /* Время отсчёта - плановое время секундного тика, а не момент записи:
     * главный цикл просыпается раз в 100 мс, и момент записи гулял на
     * долю шага - соседние отсчёты шли через 0.9 и 1.1 с (на приборе пара
     * на ~280 отсчётов), и при неудачной фазе секунда на графике
     * страницы оставалась пустой. */
    push(&s_h[0], (uint32_t)(t_us / 100000), counts, dur_ms);
}

void mca_hist_push_fast(uint32_t counts, uint32_t dur_ms, uint32_t end_ds)
{
    push(&s_h[1], end_ds, counts, dur_ms);
}

void mca_hist_clear(void)
{
    if (!s_mx) return;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    for (int i = 0; i < 2; i++) {
        s_h[i].count = 0;
        s_h[i].epoch = new_epoch();
    }
    xSemaphoreGive(s_mx);
}

size_t mca_hist_copy_ex(bool fast, uint32_t from, mca_hist_sample_t *out,
                        size_t max, uint32_t *first, uint32_t *next,
                        uint32_t *epoch)
{
    hring_t *h = &s_h[fast ? 1 : 0];
    if (!h->ring || !s_mx) {
        *first = *next = 0;
        *epoch = 0;
        return 0;
    }
    xSemaphoreTake(s_mx, portMAX_DELAY);
    const uint32_t nx = h->next;
    const uint32_t oldest = nx - h->count;
    if (from < oldest || from > nx) from = oldest;
    size_t n = nx - from;
    if (n > max) n = max;
    for (size_t i = 0; i < n; i++)
        out[i] = h->ring[(from + i) % h->len];
    *first = from;
    *next  = nx;
    *epoch = h->epoch;
    xSemaphoreGive(s_mx);
    return n;
}

size_t mca_hist_copy(uint32_t from, mca_hist_sample_t *out, size_t max,
                     uint32_t *first, uint32_t *next, uint32_t *epoch)
{
    return mca_hist_copy_ex(false, from, out, max, first, next, epoch);
}
