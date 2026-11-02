#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Ethernet на WIZnet W5500 по SPI - единственный путь к прибору (WiFi нет).
 *
 * Вызывать после esp_netif_init() и esp_event_loop_create_default().
 * ESP_OK - модуль найден, драйвер запущен, адрес придёт по DHCP. Любая
 * ошибка - модуля нет или он не отвечает: всё, что успели создать,
 * убрано, прибор работает без Ethernet. */
esp_err_t mca_eth_start(void);

bool      mca_eth_present(void);   /* модуль найден и запущен      */
bool      mca_eth_link_up(void);   /* кабель подключён             */
uint32_t  mca_eth_ip(void);        /* IPv4 (как в esp_ip4_addr_t), 0 - адреса нет */

/* Настройка адреса. Адреса - как в esp_ip4_addr_t (первый байт адреса -
 * младший). dns = 0 - DNS берётся равным шлюзу. */
typedef struct {
    uint8_t  dhcp;              /* 1 - адрес от роутера, 0 - статический */
    uint32_t ip, mask, gw, dns;
} mca_net_cfg_t;

/* Действующая настройка (*cur) и записанная в память (*saved). */
void      mca_eth_get_cfg(mca_net_cfg_t *cur, mca_net_cfg_t *saved);
/* Адрес, маска и шлюз, которые сейчас на интерфейсе. */
void      mca_eth_ip_info(uint32_t *ip, uint32_t *mask, uint32_t *gw);

/* Применить настройку через delay_ms (чтобы ответ на запрос успел уйти).
 * DHCP записывается в память сразу. Статический адрес - только после
 * mca_eth_confirm() в течение confirm_s секунд; не подтвердили - прибор
 * сам возвращает записанную настройку. */
esp_err_t mca_eth_set_cfg(const mca_net_cfg_t *c, int delay_ms, int confirm_s);
/* Подтвердить новую настройку: записать в память. false - нечего. */
bool      mca_eth_confirm(void);
/* Сколько секунд осталось до возврата прежней настройки, 0 - не ждём. */
int       mca_eth_pending_s(void);
