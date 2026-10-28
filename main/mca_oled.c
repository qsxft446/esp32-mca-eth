#include "mca_oled.h"
#include "mca_config.h"
#include "mca_dsp.h"
#include "mca_eth.h"
#include "mca_web.h"
#include "adc_clk.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "oled";

#define W 128
#define H 64

/* Кадр в раскладке SSD1306: 8 страниц по 8 строк, в байте - столбец из 8
 * точек, младший бит сверху. Рисует задача дисплея; /oled.pbm читает
 * копию (s_shown) под той же блокировкой. */
static uint8_t s_fb[W * H / 8];
static uint8_t s_shown[W * H / 8];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

/* CPS по секундам набора, новые - справа */
static uint32_t s_cps[W];
static int      s_ncps;

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

/* Шрифт 5x7, символы 0x20..0x7E: 5 столбцов, младший бит сверху. */
static const uint8_t FONT[95][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
    {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
    {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
    {0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
    {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
    {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
    {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
    {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},
    {0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},
    {0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},
    {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},
    {0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},
    {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},
    {0x00,0x7F,0x10,0x28,0x44},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},
    {0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},
    {0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
    {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},
    {0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},
    {0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},
    {0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};

static void px(int x, int y, bool on)
{
    if ((unsigned)x >= W || (unsigned)y >= H) return;
    uint8_t *b = &s_fb[(y >> 3) * W + x];
    if (on) *b |= 1u << (y & 7);
    else    *b &= ~(1u << (y & 7));
}

static void fill(int x, int y, int w, int h, bool on)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++) px(i, j, on);
}

/* Строка шрифтом 5x7 с шагом 6 точек; inv - светлым фоном. Возвращает x
 * после строки. */
static int text(int x, int y, const char *s, bool inv)
{
    for (; *s; s++, x += 6) {
        const unsigned c = (unsigned char)*s;
        const uint8_t *g = FONT[(c >= 0x20 && c <= 0x7E) ? c - 0x20 : '?' - 0x20];
        for (int i = 0; i < 6; i++)
            for (int j = -1; j < 8; j++) {
                const bool dot = i < 5 && j >= 0 && j < 7 && (g[i] >> j & 1);
                if (inv) px(x + i, y + j, !dot);
                else if (dot) px(x + i, y + j, true);
            }
    }
    return x;
}

static int text_w(const char *s) { return (int)strlen(s) * 6; }

/* ---------------- кадр ---------------- */
static void render(void)
{
    memset(s_fb, 0, sizeof(s_fb));
    char b[32];

    /* строка 1: адрес прибора или что с сетью */
    const uint32_t ip = mca_eth_ip();
    if (ip)
        snprintf(b, sizeof(b), "IP %lu.%lu.%lu.%lu",
                 (unsigned long)(ip & 0xFF), (unsigned long)(ip >> 8 & 0xFF),
                 (unsigned long)(ip >> 16 & 0xFF), (unsigned long)(ip >> 24));
    else
        snprintf(b, sizeof(b), "%s", !mca_eth_present() ? "NO ETHERNET"
                                     : mca_eth_link_up() ? "WAITING FOR DHCP"
                                                         : "NO CABLE");
    text(0, 0, b, false);

    /* строка 2: идёт ли набор, CPS и частота АЦП */
    mca_stats_t st;
    mca_dsp_get_stats(&st);
    const mca_mode_t m = mca_mode;
    const bool scope = m == MCA_MODE_SCOPE || m == MCA_MODE_SCOPE_TRIG;
    const bool run = !scope && mca_spec_run;
    int x = text(0, 11, scope ? "SCOPE" : (run ? "RUN" : "STOP"), run);
    if (run) {
        snprintf(b, sizeof(b), "%lu cps", (unsigned long)st.cps);
        text(x + 6, 11, b, false);
    }
    snprintf(b, sizeof(b), "%luM", (unsigned long)((adc_clk_get_freq() + 500000) / 1000000));
    text(W - text_w(b), 11, b, false);

    /* График CPS: секунды набора, новые справа, линией. Шкала - от
     * минимума до максимума на экране, подписаны оба: от нуля CPS около
     * 3500 +- 250 выглядел сплошной заливкой, и колебаний не было видно. */
    const int gy = 21, top = gy + 1, bot = H - 1;
    static uint32_t cps[W];
    taskENTER_CRITICAL(&s_lock);
    const int n = s_ncps;
    memcpy(cps, s_cps, sizeof(cps));
    taskEXIT_CRITICAL(&s_lock);
    for (int i = 0; i < W; i += 2) px(i, gy - 1, true);    /* верх графика */
    if (n == 0) {
        text(0, gy + 8, "no data", false);
    } else {
        uint32_t mx = 0, mn = UINT32_MAX;
        for (int i = 0; i < n; i++) {
            if (cps[i] > mx) mx = cps[i];
            if (cps[i] < mn) mn = cps[i];
        }
        const uint32_t lo = mn, sp = mx > mn ? mx - mn : 1;
        int prev = -1;
        for (int i = 0; i < n; i++) {
            const int col = W - n + i;
            const int y = bot - (int)((uint64_t)(cps[i] - lo) * (bot - top) / sp);
            if (prev < 0) px(col, y, true);
            else fill(col, y < prev ? y : prev, 1, (y < prev ? prev - y : y - prev) + 1, true);
            prev = y;
        }
        snprintf(b, sizeof(b), "%lu", (unsigned long)mx);
        text(0, top, b, true);
        snprintf(b, sizeof(b), "%lu", (unsigned long)mn);
        text(0, H - 7, b, true);
    }

    taskENTER_CRITICAL(&s_lock);
    memcpy(s_shown, s_fb, sizeof(s_fb));
    taskEXIT_CRITICAL(&s_lock);
}

void mca_oled_tick_1s(void)
{
    /* первая секунда после старта набора неполная: CPS в ней занижен
     * (на приборе 1761 против ~2100), он растягивал шкалу - пропускаем */
    static bool was_run;
    const mca_mode_t m = mca_mode;
    const bool run = !(m == MCA_MODE_SCOPE || m == MCA_MODE_SCOPE_TRIG) && mca_spec_run;
    const bool first = run && !was_run;
    was_run = run;
    if (!run || first) return;
    mca_stats_t st;
    mca_dsp_get_stats(&st);
    taskENTER_CRITICAL(&s_lock);
    if (s_ncps == W) memmove(s_cps, s_cps + 1, (W - 1) * sizeof(s_cps[0]));
    else s_ncps++;
    s_cps[s_ncps - 1] = st.cps;
    taskEXIT_CRITICAL(&s_lock);
}

size_t mca_oled_pbm(uint8_t *out, size_t cap)
{
    static const char hdr[] = "P4\n128 64\n";
    const size_t n = sizeof(hdr) - 1 + W * H / 8;
    if (cap < n) return 0;
    memcpy(out, hdr, sizeof(hdr) - 1);
    uint8_t *p = out + sizeof(hdr) - 1;
    memset(p, 0, W * H / 8);
    taskENTER_CRITICAL(&s_lock);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (s_shown[(y >> 3) * W + x] >> (y & 7) & 1)
                p[y * (W / 8) + x / 8] |= 0x80 >> (x & 7);
    taskEXIT_CRITICAL(&s_lock);
    return n;
}

/* ---------------- SSD1306 по I2C ---------------- */
static esp_err_t cmds(const uint8_t *c, size_t n)
{
    uint8_t b[32];
    b[0] = 0x00;                             /* дальше - команды */
    memcpy(b + 1, c, n);
    return i2c_master_transmit(s_dev, b, n + 1, 100);
}

static esp_err_t panel_init(void)
{
    static const uint8_t init[] = {
        0xAE,             /* выключить */
        0xD5, 0x80,       /* делитель такта */
        0xA8, 0x3F,       /* 64 строки */
        0xD3, 0x00,       /* без смещения */
        0x40,             /* первая строка 0 */
        0x8D, 0x14,       /* внутренний умножитель питания */
        0x20, 0x00,       /* адресация по горизонтали */
#if MCA_OLED_FLIP
        0xA0, 0xC0,       /* развёрнут на 180 градусов */
#else
        0xA1, 0xC8,       /* выводы дисплея сверху */
#endif
        0xDA, 0x12,       /* раскладка строк для 128x64 */
        0x81, 0xCF,       /* контраст */
        0xD9, 0xF1,       /* предзаряд */
        0xDB, 0x40,
        0xA4, 0xA6,       /* из памяти, без инверсии */
        0xAF,             /* включить */
    };
    return cmds(init, sizeof(init));
}

static esp_err_t panel_push(void)
{
    static const uint8_t win[] = { 0x21, 0, W - 1, 0x22, 0, H / 8 - 1 };
    esp_err_t e = cmds(win, sizeof(win));
    if (e != ESP_OK) return e;
    static uint8_t b[1 + W * H / 8];
    b[0] = 0x40;                             /* дальше - данные */
    taskENTER_CRITICAL(&s_lock);
    memcpy(b + 1, s_shown, W * H / 8);
    taskEXIT_CRITICAL(&s_lock);
    return i2c_master_transmit(s_dev, b, sizeof(b), 200);
}

/* Найти дисплей на шине: адрес 0x3C или 0x3D (перемычка на модуле). */
static bool panel_find(void)
{
    static const uint16_t addr[] = { 0x3C, 0x3D };
    for (int i = 0; i < 2; i++) {
        if (i2c_master_probe(s_bus, addr[i], 50) != ESP_OK) continue;
        const i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address  = addr[i],
            .scl_speed_hz    = MCA_OLED_I2C_HZ,
        };
        if (i2c_master_bus_add_device(s_bus, &dc, &s_dev) != ESP_OK) return false;
        if (panel_init() == ESP_OK) {
            ESP_LOGI(TAG, "дисплей SSD1306 найден, адрес 0x%02X", addr[i]);
            return true;
        }
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    return false;
}

/* Раз в секунду: кадр и, если дисплей есть, отправка на него. Дисплея нет
 * или он перестал отвечать - ищем его раз в 10 с: можно подключить на
 * ходу. Задача на ядре 0, ниже веба: кадр - 1 КБ по I2C, ~25 мс. */
static void oled_task(void *arg)
{
    bool have = s_bus && panel_find();
    if (s_bus && !have)
        ESP_LOGI(TAG, "дисплея SSD1306 нет (SDA GPIO%d, SCL GPIO%d) - "
                      "ищу раз в 10 с", PIN_OLED_SDA, PIN_OLED_SCL);
    int t = 0;
    for (;;) {
        render();
        if (have && panel_push() != ESP_OK) {
            ESP_LOGW(TAG, "дисплей перестал отвечать");
            i2c_master_bus_rm_device(s_dev);
            s_dev = NULL;
            have = false;
        }
        if (!have && s_bus && ++t >= 10) {
            t = 0;
            have = panel_find();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void mca_oled_start(void)
{
#if MCA_OLED_ENABLE
    const i2c_master_bus_config_t bc = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = PIN_OLED_SDA,
        .scl_io_num        = PIN_OLED_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* на модулях обычно свои подтяжки 4.7-10 кОм; внутренние - на
         * случай голого дисплея, на 400 кГц их одних мало */
        .flags.enable_internal_pullup = true,
    };
    esp_err_t e = i2c_new_master_bus(&bc, &s_bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "шина I2C дисплея не поднялась: %s", esp_err_to_name(e));
        s_bus = NULL;
    }
#endif
    /* кадр рисуется и без дисплея: его видно по /oled.pbm */
    xTaskCreatePinnedToCore(oled_task, "oled", 3072, NULL, 2, NULL, 0);
}
