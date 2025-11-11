/*
 * ADC.h
 *
 *  Created on: 9 nov 2025
 *      Author: luisg
 */

#ifndef ADC_H_
#define ADC_H_

#include <stdint.h>
#include <stdbool.h>

/* FreeRTOS */
#include "FreeRTOS.h"
#include "queue.h"
#include "timers.h"

/* NXP SDK */
#include "fsl_adc16.h"

/* ======= Macros compatibles con tu P3.txt ======= */
#define HM_HEART_ADC16_BASE            ADC0
#define HM_ADC16_CHANNEL_GROUP         0U
#define HM_ADC16_HEART_CHANNEL         12U   /* PTB2, ADC0_SE12 */
#define HM_ADC16_TEMP_CHANNEL          13U   /* PTB3, ADC0_SE13 */
#define HM_ADC16_IRQn                  ADC0_IRQn
#define HM_ADC16_IRQ_HANDLER_FUNC      ADC0_IRQHandler

/* Identificador para distinguir la fuente en la cola */
typedef enum
{
    ADC_SRC_HEART = 0,
    ADC_SRC_TEMP  = 1
} adc_src_t;

/* MISMA estructura que usas (con convSource poblado) */
typedef struct
{
    uint8_t  convSource;  /* 0 = HEART, 1 = TEMP */
    uint16_t data;        /* Valor crudo 12 bits (0..4095) */
} adcConv_str;

/* ======= API ======= */
bool ADC_Init(uint8_t queue_len);
bool ADC_Start(void);
void ADC_Stop(void);
bool ADC_SetPeriodMs(uint32_t new_period_ms);
bool ADC_Receive(adcConv_str *out, TickType_t ticks_to_wait);
QueueHandle_t ADC_GetQueueHandle(void);

/* Llama desde tu vector real: */
void ADC_IrqHandler(void);

#endif /* ADC_H_ */
