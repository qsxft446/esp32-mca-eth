#pragma once
#include "mca_config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mca_dsp_init(void);
void mca_dsp_reset_spectrum(void);

/* Обработать чанк. Состояние (кольцо, аккумулятор трапеции,
 * автомат детектора) сохраняется между вызовами. */
void mca_dsp_process(const uint16_t *data, size_t n);

/* Сбросить состояние фильтра: вызывать после разрыва потока,
 * иначе скачок на стыке будет засчитан как событие. */
void mca_dsp_flush(void);

/* Выполнить запросы других задач (очистка спектра, перезапуск фильтра).
 * Зовёт только задача обработки: на каждом чанке (idle = false) и при
 * простое захвата (idle = true). */
void mca_dsp_service(bool idle);

/* Самопроверка векторной разности трапеции (ESP32-S3, PIE). Вызывать из
 * задачи обработки до первого mca_dsp_process. false - векторный путь не
 * совпал с обычной формулой и выключен, обработка идёт обычным кодом. */
bool mca_dsp_vec_init(void);
bool mca_dsp_vec_ok(void);

/* true - импульсы идут вниз (параметр «Полярность» = 1). */
bool mca_dsp_polarity_neg(void);

void mca_dsp_get_params(mca_params_t *out);
/* Итог последней секунды (после mca_dsp_tick_1s) для истории CPS: событий
 * в спектре и сколько миллисекунд шёл набор (0 - набор не шёл). */
void mca_dsp_last_second(uint32_t *counts, uint32_t *dur_ms);
void mca_dsp_set_params(const mca_params_t *in);
/* Частота АЦП изменилась: окна L, G, поиск и перезапуск заданы во
 * времени (поля *_ns) и пересчитываются в отсчёты на новой частоте.
 * Звать после каждой смены частоты - adc_clk_get_freq(). */
void mca_dsp_set_freq(uint32_t hz);
/* Перевод времени окна в отсчёты на частоте hz, с округлением до
 * ближайшего - тем же, что в mca_dsp_set_params (и в tools/dspmodel.py). */
int32_t mca_dsp_ns_to_samples(int32_t ns, uint32_t hz);

size_t mca_dsp_get_spectrum(uint32_t *dst, size_t from, size_t count);

void mca_dsp_get_stats(mca_stats_t *out);
void mca_dsp_tick_1s(void);
