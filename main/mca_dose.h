#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* МОЩНОСТЬ ДОЗЫ ПО СПЕКТРУ: каждое событие добавляет вес G(E) своего
 * канала (E - по калибровке по энергии, которую хранит прибор), сумма
 * весов в секунду - мощность дозы. G(E) - функция отклика выбранного
 * блока NaI(Tl), подогнанная по модели Монте-Карло кристалла
 * (tools/dosemodel/fit_blocks.py): две таблицы - H*(10) (амбиентный эквивалент дозы,
 * ICRP 74) и керма в воздухе (для мкР/ч). Поправка k - множитель к обеим,
 * подгоняется по показанию дозиметра.
 *
 * Доза считается, только если калибровка по энергии набрана при текущих
 * настройках (та же подпись, что проверяет страница: способ, окна, L, G,
 * плато, кодов на канал, полярность, частота). */

#define MCA_DOSE_NDET 6          /* блоки NaI: 25x25 30x40 30x70 40x40 63x63 80x80 */
extern const char *const MCA_DOSE_DET_NAME[MCA_DOSE_NDET];

typedef enum {
    MCA_DOSE_OK = 0,
    MCA_DOSE_NO_CAL,        /* калибровки нет                       */
    MCA_DOSE_OTHER_SET,     /* калибровка при других настройках     */
    MCA_DOSE_BAD_CAL,       /* калибровка не разобрана              */
    MCA_DOSE_NO_MEM,
} mca_dose_why_t;

void  mca_dose_init(void);
/* Раз в секунду из главного цикла: сверить калибровку с настройками и,
 * если что-то поменялось, пересчитать таблицы весов по каналам. */
void  mca_dose_tick_1s(void);
/* Калибровку в приборе переписали (POST /calib) - перечитать. */
void  mca_dose_calib_changed(void);
mca_dose_why_t mca_dose_state(void);
/* блок детектирования (номер в MCA_DOSE_DET_NAME) и его поправка k */
int   mca_dose_det(void);
esp_err_t mca_dose_set_det(int det);
float mca_dose_k(void);
/* Поправка текущего блока, 0.05 ... 20; пишется в NVS. */
esp_err_t mca_dose_set_k(float k);

/* мкЗв/ч и мкР/ч из сумм весов: H*(10) в пЗв, керма в пГр, за dt_s */
#define MCA_DOSE_USVH(psv, dt_s)  ((psv) * 3600e-6 / (dt_s))
/* 1 Р = 8.764 мГр кермы в воздухе (2.58e-4 Кл/кг x 33.97 Дж/Кл) */
#define MCA_DOSE_URH(pgy, dt_s)   ((pgy) * 3600e-6 / (dt_s) / 8.764e-3)
