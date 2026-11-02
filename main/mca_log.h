#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Лог прибора для страницы «Диагностика»: последние MCA_LOG_LINES строк
 * ESP_LOG в кольце (PSRAM). Перехват стоит поверх консоли: строка всегда
 * попадает в кольцо, а в консоль - если она не выключена (эмуляция MCA на
 * UART0 выключает её, чтобы журнал не смешивался с пакетами протокола). */
#define MCA_LOG_LINES 256
#define MCA_LOG_WIDTH 200

/* Первым делом в app_main: всё, что выведено раньше, в кольцо не попадёт. */
void mca_log_init(void);

/* Печатать ли лог в консоль UART0 (в кольцо он идёт всегда). */
void mca_log_console(bool on);

/* Номер следующей строки и самой старой из ещё лежащих в кольце. */
void mca_log_range(uint32_t *first, uint32_t *next);

/* Строка с номером seq: уровень ('E', 'W', 'I', 'D', 'V' или ' ' - не из
 * ESP_LOG) и текст без перевода строки. false - строку уже затёрли. */
bool mca_log_get(uint32_t seq, char *lvl, char *txt, size_t cap);
