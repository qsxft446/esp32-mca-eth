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

size_t mca_dsp_get_spectrum(uint32_t *dst, size_t from, size_t count);

void mca_dsp_get_stats(mca_stats_t *out);
void mca_dsp_tick_1s(void);

/* ---- ФИЛЬТР ФОРМЫ ----
 * Мера формы - эффективная ширина импульса: площадь / высота, в отсчётах,
 * в окне «до / после вершины» (int_rise, int_fall). У сцинтилляций она
 * почти одна при любой энергии, у шумовых всплесков ФЭУ - заметно меньше,
 * у наложений - больше. Карта: число событий по амплитуде (столбцы - канал
 * по логарифму, 8 на октаву) и ширине (строки - от 0 до окна). */
#define SHAPE_XB      104
#define SHAPE_YB      64
#define SHAPE_LEARN_N 2000      /* событий в эталон */

typedef struct {
    int32_t  learn_n;           /* набрано событий в эталон            */
    int32_t  learn_need;        /* сколько нужно; 0 - набор не идёт     */
    int32_t  p10, p50, p90;     /* последний набор: ширина x10, процентили */
    int32_t  win;               /* окно, на котором строится карта      */
    int32_t  active;            /* фильтр реально отбрасывает: включён,
                                   эталон есть, окно и частота те же     */
} mca_shape_info_t;

/* Начать (true) или прервать набор эталона. Идёт, пока набирается спектр;
 * в эталон берутся события выше трёх порогов. */
void mca_dsp_shape_learn(bool start);
/* Очистить карту (её чистит и «Сброс» спектра). */
void mca_dsp_shape_clear_map(void);
/* Страница смотрит карту: мерить форму ещё 10 с, даже при выключенном
 * фильтре (иначе мера не считается вовсе и ядро не тратится). */
void mca_dsp_shape_watch(void);
void mca_dsp_shape_info(mca_shape_info_t *o);
/* Копия карты: SHAPE_YB строк по SHAPE_XB слов, строка 0 - ширина 0. */
void mca_dsp_shape_map(uint16_t *dst);
/* Из секундного тика: досчитать законченный набор эталона. true - эталон
 * записан в параметры, их надо сохранить. */
bool mca_dsp_shape_poll(void);
