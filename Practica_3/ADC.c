/*
 * ADC.c
 *
 *  Created on: 9 nov 2025
 *      Author: luisg
 */

#include "ADC.h"
#include "fsl_common.h"

/* Barrera para salir de ISR si el SDK no la define */
#ifndef SDK_ISR_EXIT_BARRIER
#define SDK_ISR_EXIT_BARRIER __DSB(); __ISB()
#endif

/* ======= Contexto interno ======= */
typedef struct
{
    adc16_channel_config_t chan_cfg;
    QueueHandle_t          hQueue;
    TimerHandle_t          hTimer;
    uint32_t               period_ms;
    bool                   started;
    uint8_t                next_src;  /* 0=HEART, 1=TEMP; alterna cada tick */
} adc_ctx_t;

static adc_ctx_t s_adc;

/* ======= Prototipos locales ======= */
static void prv_config_adc_12bit(void);
static void prv_timer_cb(TimerHandle_t xTimer);

/* ======= Implementación ======= */
bool ADC_Init(uint8_t queue_len)
{
    s_adc.started   = false;
    s_adc.period_ms = 100; /* 100 ms por requisito */
    s_adc.next_src  = (uint8_t)ADC_SRC_HEART;

    if (queue_len == 0u) queue_len = 10u;

    /* Cola ISR -> tareas (mismo tipo que tu P3.txt) */
    s_adc.hQueue = xQueueCreate(queue_len, sizeof(adcConv_str));
    if (s_adc.hQueue == NULL)
    {
        return false;
    }

    /* Timer de muestreo periódico */
    s_adc.hTimer = xTimerCreate("adc_100ms",
                                pdMS_TO_TICKS(s_adc.period_ms),
                                pdTRUE,
                                NULL,
                                prv_timer_cb);
    if (s_adc.hTimer == NULL)
    {
        return false;
    }

    /* ADC0: 12 bits, SW trigger */
    prv_config_adc_12bit();

    /* Config común de canal (variamos channelNumber en cada tick) */
    s_adc.chan_cfg.enableInterruptOnConversionCompleted = true;
#if defined(FSL_FEATURE_ADC16_HAS_DIFF_MODE) && FSL_FEATURE_ADC16_HAS_DIFF_MODE
    s_adc.chan_cfg.enableDifferentialConversion = false;
#endif

    /* Prioridad/enable de IRQ (seguro para FreeRTOS FromISR) */
    NVIC_SetPriority(HM_ADC16_IRQn, 3);
    EnableIRQ(HM_ADC16_IRQn);

    return true;
}

bool ADC_Start(void)
{
    if (s_adc.started) return true;
    if (xTimerStart(s_adc.hTimer, 0) != pdPASS)
    {
        return false;
    }
    s_adc.started = true;
    return true;
}

void ADC_Stop(void)
{
    if (!s_adc.started) return;
    (void)xTimerStop(s_adc.hTimer, 0);
    s_adc.started = false;
}

bool ADC_SetPeriodMs(uint32_t new_period_ms)
{
    if (new_period_ms == 0u) return false;
    s_adc.period_ms = new_period_ms;
    return (xTimerChangePeriod(s_adc.hTimer, pdMS_TO_TICKS(new_period_ms), 0) == pdPASS);
}

bool ADC_Receive(adcConv_str *out, TickType_t ticks_to_wait)
{
    if (!out) return false;
    return (xQueueReceive(s_adc.hQueue, out, ticks_to_wait) == pdTRUE);
}

QueueHandle_t ADC_GetQueueHandle(void)
{
    return s_adc.hQueue;
}

/* ======= IRQ handler ======= */
void ADC_IrqHandler(void)
{
    BaseType_t xHPW = pdFALSE;
    adcConv_str msg;

    /* Leer valor -> limpia flag de fin de conversión */
    uint32_t val = ADC16_GetChannelConversionValue(HM_HEART_ADC16_BASE,
                                                   HM_ADC16_CHANNEL_GROUP);

    /* ¿Cuál canal se convirtió? El timer preparó A y cambió next_src a B,
       así que aquí el convertido es el PREVIO a next_src. */
    uint8_t converted_src = (s_adc.next_src == (uint8_t)ADC_SRC_HEART)
                          ? (uint8_t)ADC_SRC_TEMP
                          : (uint8_t)ADC_SRC_HEART;

    msg.convSource = converted_src;          /* 0=HEART, 1=TEMP */
    msg.data       = (uint16_t)val;

    (void)xQueueSendFromISR(s_adc.hQueue, &msg, &xHPW);

    portYIELD_FROM_ISR(xHPW);
    SDK_ISR_EXIT_BARRIER;
}

/* ======= Estáticos locales ======= */
static void prv_config_adc_12bit(void)
{
    adc16_config_t cfg;
    ADC16_GetDefaultConfig(&cfg);

    cfg.resolution = kADC16_ResolutionSE12Bit;
#ifdef BOARD_ADC_USE_ALT_VREF
    cfg.referenceVoltageSource = kADC16_ReferenceVoltageSourceValt;
#endif

    ADC16_Init(HM_HEART_ADC16_BASE, &cfg);
    ADC16_EnableHardwareTrigger(HM_HEART_ADC16_BASE, false);

#if defined(FSL_FEATURE_ADC16_HAS_CALIBRATION) && FSL_FEATURE_ADC16_HAS_CALIBRATION
    (void)ADC16_DoAutoCalibration(HM_HEART_ADC16_BASE);
#endif
}

/* Timer 100 ms: alterna canal y lanza conversión por SW */
static void prv_timer_cb(TimerHandle_t xTimer)
{
    /* Selecciona canal a convertir y alterna para el próximo tick */
    if (s_adc.next_src == (uint8_t)ADC_SRC_HEART)
    {
        s_adc.chan_cfg.channelNumber = HM_ADC16_HEART_CHANNEL; /* ADC0_SE12 */
        s_adc.next_src = (uint8_t)ADC_SRC_TEMP;
    }
    else
    {
        s_adc.chan_cfg.channelNumber = HM_ADC16_TEMP_CHANNEL;  /* ADC0_SE13 */
        s_adc.next_src = (uint8_t)ADC_SRC_HEART;
    }

    /* Dispara conversión: al terminar, cae a ISR -> cola */
    ADC16_SetChannelConfig(HM_HEART_ADC16_BASE,
                           HM_ADC16_CHANNEL_GROUP,
                           &s_adc.chan_cfg);
}
