#include "mca_eth.h"
#include "mca_config.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_eth_mac_spi.h"
#include "esp_eth_netif_glue.h"
#include "driver/spi_master.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "driver/gpio.h"

static const char *TAG = "mca_eth";

static esp_eth_handle_t  s_eth;
static esp_netif_t      *s_netif;
static volatile bool     s_link;
static volatile uint32_t s_ip;       /* 0 - адреса нет */

/* ---------------- настройка адреса: DHCP или статический ----------------
 * В памяти (NVS, "mca_net") - подтверждённая настройка. Новый статический
 * адрес сначала только применяется: страница переходит на него и
 * подтверждает (/net/confirm). Не подтвердила за отведённое время
 * (адрес не из этой сети, не тот шлюз) - возвращается записанная.
 * Кнопка BOOT, удержанная 5 с во время работы, - снова DHCP. */
#define NET_NS     "mca_net"
#define NET_VER    1
static mca_net_cfg_t     s_cfg = { .dhcp = 1 };   /* действующая      */
static mca_net_cfg_t     s_saved = { .dhcp = 1 }; /* записанная       */
static volatile int      s_pend_s;                /* до возврата, с   */
static volatile int      s_apply_ms = -1;         /* применить через  */

static void cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NET_NS, NVS_READONLY, &h) != ESP_OK) return;
    uint8_t v = 0;
    mca_net_cfg_t c;
    size_t len = sizeof(c);
    if (nvs_get_u8(h, "v", &v) == ESP_OK && v == NET_VER &&
        nvs_get_blob(h, "cfg", &c, &len) == ESP_OK && len == sizeof(c))
        s_saved = s_cfg = c;
    nvs_close(h);
}

static esp_err_t cfg_save(const mca_net_cfg_t *c)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(NET_NS, NVS_READWRITE, &h);
    if (e != ESP_OK) return e;
    e = nvs_set_u8(h, "v", NET_VER);
    if (e == ESP_OK) e = nvs_set_blob(h, "cfg", c, sizeof(*c));
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) s_saved = *c;
    return e;
}

/* Поставить настройку s_cfg на интерфейс. */
static void cfg_apply(void)
{
    if (!s_netif) return;
    if (s_cfg.dhcp) {
        const esp_err_t e = esp_netif_dhcpc_start(s_netif);
        if (e == ESP_OK) s_ip = 0;           /* новый адрес придёт событием */
        ESP_LOGI(TAG, "адрес - от роутера (DHCP)");
        return;
    }
    esp_netif_dhcpc_stop(s_netif);           /* уже остановлен - не ошибка */
    esp_netif_ip_info_t ii = { 0 };
    ii.ip.addr      = s_cfg.ip;
    ii.netmask.addr = s_cfg.mask;
    ii.gw.addr      = s_cfg.gw;
    esp_err_t e = esp_netif_set_ip_info(s_netif, &ii);
    esp_netif_dns_info_t d = { 0 };
    d.ip.type = ESP_IPADDR_TYPE_V4;
    d.ip.u_addr.ip4.addr = s_cfg.dns ? s_cfg.dns : s_cfg.gw;
    if (e == ESP_OK) e = esp_netif_set_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &d);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "статический адрес не поставлен: %s", esp_err_to_name(e));
        return;
    }
    if (s_link) s_ip = s_cfg.ip;
    const esp_ip4_addr_t a = { .addr = s_cfg.ip };
    ESP_LOGI(TAG, "статический адрес: http://" IPSTR "/", IP2STR(&a));
}

/* Раз в 100 мс: отложенное применение, отсчёт до возврата прежней
 * настройки и кнопка BOOT (GPIO0). */
static void net_task(void *arg)
{
    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << GPIO_NUM_0,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    int boot_ms = 0, tick = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_apply_ms >= 0 && (s_apply_ms -= 100) < 0) cfg_apply();
        if (s_pend_s > 0 && ++tick >= 10) {
            tick = 0;
            if (--s_pend_s == 0) {
                ESP_LOGW(TAG, "новый адрес не подтверждён - возвращаю прежнюю настройку");
                s_cfg = s_saved;
                cfg_apply();
            }
        }
        boot_ms = gpio_get_level(GPIO_NUM_0) == 0 ? boot_ms + 100 : 0;
        if (boot_ms == 5000) {
            ESP_LOGW(TAG, "кнопка BOOT 5 с - адрес снова от роутера (DHCP)");
            s_pend_s = 0;
            s_cfg = (mca_net_cfg_t){ .dhcp = 1 };
            cfg_save(&s_cfg);
            cfg_apply();
        }
    }
}

esp_err_t mca_eth_set_cfg(const mca_net_cfg_t *c, int delay_ms, int confirm_s)
{
    if (!s_netif) return ESP_ERR_INVALID_STATE;
    if (!c->dhcp && (!c->ip || !c->mask)) return ESP_ERR_INVALID_ARG;
    s_cfg = *c;
    if (c->dhcp) {
        s_pend_s = 0;
        cfg_save(c);
    } else {
        s_pend_s = confirm_s;
    }
    s_apply_ms = delay_ms;
    return ESP_OK;
}

bool mca_eth_confirm(void)
{
    if (s_pend_s <= 0) return false;
    s_pend_s = 0;
    return cfg_save(&s_cfg) == ESP_OK;
}

int mca_eth_pending_s(void) { return s_pend_s; }

void mca_eth_get_cfg(mca_net_cfg_t *cur, mca_net_cfg_t *saved)
{
    *cur = s_cfg;
    *saved = s_saved;
}

void mca_eth_ip_info(uint32_t *ip, uint32_t *mask, uint32_t *gw)
{
    esp_netif_ip_info_t ii = { 0 };
    if (s_netif) esp_netif_get_ip_info(s_netif, &ii);
    *ip = s_ip;
    *mask = ii.netmask.addr;
    *gw = ii.gw.addr;
}

static void eth_ev(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == ETH_EVENT && id == ETHERNET_EVENT_CONNECTED) {
        s_link = true;
        if (s_cfg.dhcp) ESP_LOGI(TAG, "Ethernet: кабель подключён, жду адрес от роутера");
        else cfg_apply();                    /* статический - сразу */
    } else if (base == ETH_EVENT && id == ETHERNET_EVENT_DISCONNECTED) {
        s_link = false;
        s_ip   = 0;
        ESP_LOGW(TAG, "Ethernet: кабель отключён");
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        s_ip = e->ip_info.ip.addr;
        ESP_LOGI(TAG, "Ethernet подключён: http://" IPSTR "/",
                 IP2STR(&e->ip_info.ip));
    } else if (base == IP_EVENT && id == IP_EVENT_ETH_LOST_IP) {
        s_ip = 0;
        ESP_LOGW(TAG, "Ethernet: адрес потерян");
    }
}

esp_err_t mca_eth_start(void)
{
#if !MCA_ETH_ENABLE
    ESP_LOGI(TAG, "Ethernet выключен в mca_config.h (MCA_ETH_ENABLE)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    esp_err_t err;

    /* Прерывание W5500 обслуживает общий сервис прерываний GPIO. Если он
     * уже установлен кем-то ещё - это не ошибка. */
    bool isr_by_us = false;
    if (PIN_ETH_INT >= 0) {
        err = gpio_install_isr_service(0);
        if (err == ESP_OK) {
            isr_by_us = true;
        } else if (err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "сервис прерываний GPIO: %s", esp_err_to_name(err));
            return err;
        }
    }

    const spi_bus_config_t bus = {
        .miso_io_num   = PIN_ETH_MISO,
        .mosi_io_num   = PIN_ETH_MOSI,
        .sclk_io_num   = PIN_ETH_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "шина SPI для W5500: %s", esp_err_to_name(err));
        if (isr_by_us) gpio_uninstall_isr_service();
        return err;
    }

    spi_device_interface_config_t dev = {
        .mode           = 0,
        .clock_speed_hz = MCA_ETH_SPI_MHZ * 1000 * 1000,
        .queue_size     = 20,
        .spics_io_num   = PIN_ETH_CS,
    };
    eth_w5500_config_t wcfg = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &dev);
    wcfg.int_gpio_num   = PIN_ETH_INT;
    wcfg.poll_period_ms = PIN_ETH_INT >= 0 ? 0 : 10;

    /* Задачу приёма - на ядро, где создаётся драйвер: сеть поднимается из
     * app_main, это ядро 0. Иначе задача с приоритетом 15 могла бы
     * вытеснять обработку спектра (приоритет 10) на ядре 1. */
    eth_mac_config_t mcfg = ETH_MAC_DEFAULT_CONFIG();
    mcfg.flags |= ETH_MAC_FLAG_PIN_TO_CORE;
    eth_phy_config_t pcfg = ETH_PHY_DEFAULT_CONFIG();
    pcfg.phy_addr       = 1;
    pcfg.reset_gpio_num = PIN_ETH_RST;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&wcfg, &mcfg);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&pcfg);
    esp_eth_handle_t h = NULL;
    if (mac && phy) {
        esp_eth_config_t ecfg = ETH_DEFAULT_CONFIG(mac, phy);
        err = esp_eth_driver_install(&ecfg, &h);
    } else {
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        /* Модуля нет или он не отвечает: драйвер после сброса читает
         * версию чипа и не находит её. Всё созданное убираем. */
        ESP_LOGW(TAG, "W5500 не найден или не отвечает (%s) - работаем "
                      "без Ethernet", esp_err_to_name(err));
        if (mac) mac->del(mac);
        if (phy) phy->del(phy);
        spi_bus_free(SPI2_HOST);
        if (isr_by_us) gpio_uninstall_isr_service();
        return err;
    }

    /* У W5500 нет своего MAC-адреса - берём адрес Ethernet из ESP32. */
    uint8_t addr[6];
    if (esp_read_mac(addr, ESP_MAC_ETH) == ESP_OK)
        esp_eth_ioctl(h, ETH_CMD_S_MAC_ADDR, addr);

    const esp_netif_config_t ncfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *netif = esp_netif_new(&ncfg);
    esp_netif_attach(netif, esp_eth_new_netif_glue(h));
    s_netif = netif;
    cfg_load();
    if (!s_cfg.dhcp) cfg_apply();            /* до кабеля - адрес готов */
    esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_ev, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_ev, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_LOST_IP, eth_ev, NULL);

    err = esp_eth_start(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "запуск Ethernet: %s", esp_err_to_name(err));
        return err;
    }
    s_eth = h;
    xTaskCreatePinnedToCore(net_task, "net", 3072, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "W5500 найден (SPI %d МГц), адрес - %s", MCA_ETH_SPI_MHZ,
             s_cfg.dhcp ? "по DHCP" : "статический");
    return ESP_OK;
#endif
}

bool     mca_eth_present(void) { return s_eth != NULL; }
bool     mca_eth_link_up(void) { return s_link; }
uint32_t mca_eth_ip(void)      { return s_ip; }
