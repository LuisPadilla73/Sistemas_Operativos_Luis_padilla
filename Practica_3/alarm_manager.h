/*
 * alarm_manager.h
 *
 *  Created on: 10 nov 2025
 *      Author: luisg
 */

#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "ADC.h"

typedef struct {
    bool heart_high;
    bool heart_low;
    bool temp_high;
    bool temp_low;
} alarm_flags_t;

typedef struct {
    uint16_t hr_low_centi_mV;    /* default 15  => 0.15 mV */
    uint16_t hr_high_centi_mV;   /* default 285 => 2.85 mV */
    uint16_t t_low_deci_C;       /* default 350 => 35.0 °C */
    uint16_t t_high_deci_C;      /* default 375 => 37.5 °C */
} alarm_limits_t;

void AlarmManager_Init(void);
QueueHandle_t AlarmManager_GetInputQueue(void);
void AlarmManager_GetFlags(alarm_flags_t *out);
void AlarmManager_SetLimits(const alarm_limits_t *limits);
void AlarmManager_GetLimits(alarm_limits_t *limits);

#endif /* ALARM_MANAGER_H */
