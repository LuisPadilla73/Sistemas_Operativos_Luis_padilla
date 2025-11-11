/*
 * alarm_manager.c
 *
 *  Created on: 10 nov 2025
 *      Author: luisg
 */

#include "alarm_manager.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "GPIO.h"

#include "fsl_device_registers.h"
#include "fsl_gpio.h"

#define ALARM_ACTIVATE_SAMPLES   (50u)
#define ALARM_DEACTIVATE_SAMPLES (20u)

static QueueHandle_t sAlarmInputQ = NULL;

static SemaphoreHandle_t sMutex = NULL;

static alarm_flags_t sFlags = {false,false,false,false};

static alarm_limits_t sLimits = {
    .hr_low_centi_mV   = 15,   /* 0.15 mV */
    .hr_high_centi_mV  = 285,  /* 2.85 mV */
    .t_low_deci_C      = 350,  /* 35.0 °C */
    .t_high_deci_C     = 375   /* 37.5 °C */
};

static void AlarmManager_Task(void *pv);

static void set_led_heart(bool on);
static void set_led_temp(bool on);

static inline uint16_t map_hr_centi_mV(uint16_t adc)
{
    return (uint16_t)((adc * 300u) / 4095u);
}
static inline uint16_t map_temp_deci_C(uint16_t adc)
{
    return (uint16_t)(340u + ((adc * 60u) / 4095u));
}

/* ======= API ======= */
void AlarmManager_Init(void)
{
    if (sAlarmInputQ == NULL) {
        sAlarmInputQ = xQueueCreate(8, sizeof(adcConv_str));
    }
    if (sMutex == NULL) {
        sMutex = xSemaphoreCreateMutex();
    }

    /* Apaga LEDs al iniciar */
    set_led_heart(false);
    set_led_temp(false);

    /* Crea la tarea del gestor de alarmas */
    (void)xTaskCreate(AlarmManager_Task, "AlarmMgr",
                      configMINIMAL_STACK_SIZE + 160,
                      NULL, tskIDLE_PRIORITY + 2, NULL);
}

QueueHandle_t AlarmManager_GetInputQueue(void)
{
    return sAlarmInputQ;
}

void AlarmManager_GetFlags(alarm_flags_t *out)
{
    if (!out) return;
    if (xSemaphoreTake(sMutex, portMAX_DELAY) == pdTRUE) {
        *out = sFlags;
        xSemaphoreGive(sMutex);
    }
}

void AlarmManager_SetLimits(const alarm_limits_t *limits)
{
    if (!limits) return;
    if (xSemaphoreTake(sMutex, portMAX_DELAY) == pdTRUE) {
        sLimits = *limits;
        xSemaphoreGive(sMutex);
    }
}

void AlarmManager_GetLimits(alarm_limits_t *limits)
{
    if (!limits) return;
    if (xSemaphoreTake(sMutex, portMAX_DELAY) == pdTRUE) {
        *limits = sLimits;
        xSemaphoreGive(sMutex);
    }
}

/* ======= Estado por canal ======= */
typedef struct {
    uint16_t act_cnt_high; /* consecutivas arriba del alto */
    uint16_t act_cnt_low;  /* consecutivas abajo del bajo  */
    uint16_t deact_cnt;    /* consecutivas dentro de rango */
    bool alarm_high_on;
    bool alarm_low_on;
} alarm_chan_ctx_t;

/* ======= Lógica por muestras ======= */
static void update_alarm_logic(uint16_t value, uint16_t low_th, uint16_t high_th,
                               alarm_chan_ctx_t *ctx, bool *out_high_on, bool *out_low_on,
                               uint16_t activate_samples, uint16_t deactivate_samples)
{
    bool in_range = (value >= low_th) && (value <= high_th);

    if (in_range) {
        ctx->act_cnt_high = 0;
        ctx->act_cnt_low  = 0;
        if (ctx->alarm_high_on || ctx->alarm_low_on) {
            if (ctx->deact_cnt < deactivate_samples) ctx->deact_cnt++;
            if (ctx->deact_cnt >= deactivate_samples) {
                ctx->alarm_high_on = false;
                ctx->alarm_low_on  = false;
                ctx->deact_cnt = 0;
            }
        } else {
            ctx->deact_cnt = 0;
        }
    } else {
        ctx->deact_cnt = 0;
        if (value > high_th) {
            ctx->act_cnt_low = 0;
            if (ctx->act_cnt_high < activate_samples) ctx->act_cnt_high++;
            if (!ctx->alarm_high_on && (ctx->act_cnt_high >= activate_samples)) {
                ctx->alarm_high_on = true;
                ctx->alarm_low_on  = false;
                ctx->act_cnt_high  = 0;
            }
        } else { /* value < low_th */
            ctx->act_cnt_high = 0;
            if (ctx->act_cnt_low < activate_samples) ctx->act_cnt_low++;
            if (!ctx->alarm_low_on && (ctx->act_cnt_low >= activate_samples)) {
                ctx->alarm_low_on  = true;
                ctx->alarm_high_on = false;
                ctx->act_cnt_low   = 0;
            }
        }
    }

    *out_high_on = ctx->alarm_high_on;
    *out_low_on  = ctx->alarm_low_on;
}

static void AlarmManager_Task(void *pv)
{
    (void)pv;

    alarm_chan_ctx_t heart = {0};
    alarm_chan_ctx_t temp  = {0};
    adcConv_str m;

    for (;;) {
        if (sAlarmInputQ && xQueueReceive(sAlarmInputQ, &m, portMAX_DELAY) == pdTRUE) {

            if (xSemaphoreTake(sMutex, portMAX_DELAY) != pdTRUE) {
                /* Si no se pudo tomar el mutex, saltamos esta muestra */
                continue;
            }

            if (m.convSource == (uint8_t)ADC_SRC_HEART) {
                uint16_t v = map_hr_centi_mV(m.data);

                bool high_on, low_on;
                update_alarm_logic(v,
                                   sLimits.hr_low_centi_mV,
                                   sLimits.hr_high_centi_mV,
                                   &heart, &high_on, &low_on,
                                   ALARM_ACTIVATE_SAMPLES, ALARM_DEACTIVATE_SAMPLES);

                sFlags.heart_high = high_on;
                sFlags.heart_low  = low_on;

            } else { /* TEMP */
                uint16_t v = map_temp_deci_C(m.data);

                bool high_on, low_on;
                update_alarm_logic(v,
                                   sLimits.t_low_deci_C,
                                   sLimits.t_high_deci_C,
                                   &temp, &high_on, &low_on,
                                   ALARM_ACTIVATE_SAMPLES, ALARM_DEACTIVATE_SAMPLES);

                sFlags.temp_high = high_on;
                sFlags.temp_low  = low_on;
            }

            /* LEDs por sensor */
            set_led_heart(sFlags.heart_high || sFlags.heart_low);
            set_led_temp (sFlags.temp_high  || sFlags.temp_low );

            xSemaphoreGive(sMutex);
        }
    }
}

/* ======= Control de LEDs con tu driver =======
 * FRDM-K64F LEDs son activos en bajo (0=ON, 1=OFF).
 * - Para HR usamos tus funciones redON()/redOFF() de GPIO.c
 * - Para TEMP usamos GPIO_PortClear/Set en GPIOB y BLUE (21U)
 *   (coincide con tu configuración en GPIO.c)
 */
static void set_led_heart(bool on)
{
    if (on) {
        redON();
    } else {
        redOFF();
    }
}

static void set_led_temp(bool on)
{
    if (on) {
        /* Activo-bajo: 0 = ON */
        GPIO_PortClear(GPIOB, 1u << BLUE);
    } else {
        /* 1 = OFF */
        GPIO_PortSet(GPIOB, 1u << BLUE);
    }
}
