#include "mca_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

/* Строка s лежит в ячейке s % MCA_LOG_LINES. Пишут все задачи, которые
 * зовут ESP_LOG; в ячейку - под спин-блокировкой, это копия ~200 байт. */
typedef struct {
    uint32_t seq;
    char     lvl;
    char     txt[MCA_LOG_WIDTH];
} log_line_t;

static log_line_t        *s_ring;
static uint32_t           s_next;
static portMUX_TYPE       s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t     s_console_vprintf;
static volatile bool      s_console_on = true;

/* одна строка в кольцо: без управляющих последовательностей и \r */
static void put_line(const char *s, size_t n)
{
    if (!s_ring || n == 0) return;
    log_line_t l = { 0 };
    size_t k = 0;
    for (size_t i = 0; i < n && k < sizeof(l.txt) - 1; i++) {
        if (s[i] == '\033') {                /* цвет: ESC [ ... m */
            while (i < n && s[i] != 'm') i++;
            continue;
        }
        if (s[i] == '\r') continue;
        l.txt[k++] = s[i];
    }
    if (k == 0) return;
    /* строка ESP_LOG начинается с «E (1234) тег: » */
    l.lvl = (k > 2 && strchr("EWIDV", l.txt[0]) && l.txt[1] == ' ' && l.txt[2] == '(')
          ? l.txt[0] : ' ';
    taskENTER_CRITICAL(&s_lock);
    l.seq = s_next++;
    s_ring[l.seq % MCA_LOG_LINES] = l;
    taskEXIT_CRITICAL(&s_lock);
}

static int log_vprintf(const char *fmt, va_list ap)
{
    char b[MCA_LOG_WIDTH + 2];
    va_list cp;
    va_copy(cp, ap);
    const int n = vsnprintf(b, sizeof(b), fmt, cp);
    va_end(cp);
    if (n > 0) {
        const size_t len = (size_t)n < sizeof(b) - 1 ? (size_t)n : sizeof(b) - 1;
        size_t a = 0;
        for (size_t i = 0; i <= len; i++)
            if (i == len || b[i] == '\n') {
                put_line(b + a, i - a);
                a = i + 1;
            }
    }
    return (s_console_on && s_console_vprintf) ? s_console_vprintf(fmt, ap) : n;
}

void mca_log_init(void)
{
    s_ring = heap_caps_calloc(MCA_LOG_LINES, sizeof(log_line_t),
                              MALLOC_CAP_SPIRAM);
    s_console_vprintf = esp_log_set_vprintf(log_vprintf);
    if (!s_ring)
        ESP_LOGW("mca_log", "нет памяти под лог для страницы - только консоль");
}

void mca_log_console(bool on) { s_console_on = on; }

void mca_log_range(uint32_t *first, uint32_t *next)
{
    taskENTER_CRITICAL(&s_lock);
    *next = s_next;
    taskEXIT_CRITICAL(&s_lock);
    *first = *next > MCA_LOG_LINES ? *next - MCA_LOG_LINES : 0;
}

bool mca_log_get(uint32_t seq, char *lvl, char *txt, size_t cap)
{
    if (!s_ring || cap == 0) return false;
    bool ok;
    taskENTER_CRITICAL(&s_lock);
    const log_line_t *l = &s_ring[seq % MCA_LOG_LINES];
    ok = l->seq == seq && seq < s_next;
    if (ok) {
        *lvl = l->lvl;
        strncpy(txt, l->txt, cap - 1);
        txt[cap - 1] = 0;
    }
    taskEXIT_CRITICAL(&s_lock);
    return ok;
}
