/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_adc16.h"

#include "LCD_nokia.h"
#include "LCD_nokia_images.h"
#include "SPI.h"
#include "nokia_draw.h"

#include "ADC.h"
#include "FSM.h"

#define hello_task_PRIORITY    (configMAX_PRIORITIES - 1)
#define GrapNumb_PRIORITY      (configMAX_PRIORITIES - 2)

typedef struct{
    uint8_t x;
    uint8_t y;
} point_str;

static TimerHandle_t SendFBTimer;

static QueueHandle_t AdcConversionQueue;
static QueueHandle_t PointQueue;
static QueueHandle_t PointQueueTEMP;

static QueueHandle_t NumberQueueHR;
static QueueHandle_t NumberQueueTEMP;

static void LCDprint_thread(void *pvParameters);
static void GraphProcess_thread(void *pvParameters);
static void NumberProcess_thread(void *pvParameters);

static void AdcForwarder_task(void *pvParameters);

static void LCD_PrintCentimV(uint8_t x, uint8_t y, uint16_t centimV);
static void LCD_PrintDeciC(uint8_t x, uint8_t y, uint16_t deciC);

static inline uint8_t map_to_band(uint16_t y_norm, uint8_t y_min, uint8_t y_max);

static void ScreenInit(void);

static void SendFBCallback(TimerHandle_t ARHandle);

void ADC0_IRQHandler(void);

static void ScreenInit(void)
{
    SPI_config();
    LCD_nokia_init();
    LCD_nokia_clear();
}

static void SendFBCallback(TimerHandle_t ARHandle)
{
    (void)ARHandle;
    LCD_nokia_sent_FrameBuffer();
}

/* =================== impresión =================== */
static void LCD_PrintCentimV(uint8_t x, uint8_t y, uint16_t centimV)
{
    uint8_t A = (uint8_t)((centimV / 100U) % 10U);
    uint8_t B = (uint8_t)((centimV / 10U)  % 10U);
    uint8_t C = (uint8_t)( centimV         % 10U);

    LCD_nokia_clear_range_FrameBuffer(x, y, 35);
    LCD_nokia_write_char_xy_FB(x +  0, y, (uint8_t)('0' + A));
    LCD_nokia_write_char_xy_FB(x +  5, y, (uint8_t)('0' + B));
    LCD_nokia_write_char_xy_FB(x + 10, y, '.');
    LCD_nokia_write_char_xy_FB(x + 15, y, (uint8_t)('0' + C));
    LCD_nokia_write_char_xy_FB(x + 20, y, ' ');
    LCD_nokia_write_string_xy_FB(x + 25, y, (uint8_t*)"mv", 2);
}

static void LCD_PrintDeciC(uint8_t x, uint8_t y, uint16_t deciC)
{
    uint16_t entero  = deciC / 10U;
    uint8_t  decimal = deciC % 10U;

    uint8_t tens = (uint8_t)((entero / 10U) % 10U);
    uint8_t ones = (uint8_t)( entero        % 10U);

    LCD_nokia_clear_range_FrameBuffer(x, y, 40);
    LCD_nokia_write_char_xy_FB(x +  0, y, (uint8_t)('0' + tens));
    LCD_nokia_write_char_xy_FB(x +  5, y, (uint8_t)('0' + ones));
    LCD_nokia_write_char_xy_FB(x + 10, y, '.');
    LCD_nokia_write_char_xy_FB(x + 15, y, (uint8_t)('0' + decimal));
    LCD_nokia_write_char_xy_FB(x + 20, y, ' ');
    LCD_nokia_write_char_xy_FB(x + 25, y, 'C');
}

static inline uint8_t map_to_band(uint16_t y_norm, uint8_t y_min, uint8_t y_max)
{
    uint8_t band_h = (uint8_t)(y_max - y_min + 1U);
    uint8_t y = (uint8_t)((y_norm * band_h) / 48U);
    if (y >= band_h) y = band_h - 1U;
    return (uint8_t)(y_min + y);
}

int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    ScreenInit();

    /* ========= Queues ========= */
    AdcConversionQueue = xQueueCreate(8, sizeof(adcConv_str));
    PointQueue         = xQueueCreate(8, sizeof(uint16_t));
    PointQueueTEMP     = xQueueCreate(8, sizeof(uint16_t));
    NumberQueueHR      = xQueueCreate(8, sizeof(uint16_t));
    NumberQueueTEMP    = xQueueCreate(8, sizeof(uint16_t));

    /* ========= Timer ========= */
    SendFBTimer = xTimerCreate(
        "WriteFB",
        pdMS_TO_TICKS(33),
        pdTRUE,
        0,
        SendFBCallback);

    if (!FSM_Init()) {
        PRINTF("FSM_Init failed!\r\n");
        while (1) {}
    }

    ADC_Init(10);
    ADC_Start();

    /* ======= timers ======= */
    xTimerStart(SendFBTimer, 0);

    /* ========= Tareas ========= */
    if (xTaskCreate(LCDprint_thread, "LCDprint",
                    configMINIMAL_STACK_SIZE + 160, NULL, hello_task_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("LCDprint_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(GraphProcess_thread, "GraphProc",
                    configMINIMAL_STACK_SIZE + 160, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("GraphProcess_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(NumberProcess_thread, "NumberProc",
                    configMINIMAL_STACK_SIZE + 160, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("NumberProcess_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(AdcForwarder_task, "AdcFwd",
                    configMINIMAL_STACK_SIZE + 120, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("AdcForwarder_task creation failed!\r\n");
        while (1) {}
    }

    vTaskStartScheduler();

    for (;;){}
}

void ADC0_IRQHandler(void)
{
    ADC_IrqHandler();
}

/* =================== Tareas =================== */

static void LCDprint_thread(void *pvParameters)
{
    (void)pvParameters;

    point_str hr_a = {0,0}, hr_b = {0,0};
    point_str tp_a = {0,0}, tp_b = {0,0};

    uint16_t y_hr_norm;
    uint16_t y_tp_norm;

    uint8_t   x_increment = 1;
    ui_mode_t mode        = UI_MODE_HR;

    uint16_t hr_centimV;
    uint16_t t_deciC;

    for (;;)
    {
        /* Lee zoom y modo actuales */
        (void)xQueuePeek(FSM_GetZoomMailbox(), &x_increment, 0);
        (void)xQueuePeek(FSM_GetModeMailbox(), &mode, 0);

        /* ===== GRÁFICA HR ===== */
        if (xQueueReceive(PointQueue, &y_hr_norm, 0) == pdTRUE)
        {
            uint8_t y_plot = 0xFF;

            if (mode == UI_MODE_HR) {
                y_plot = (uint8_t)y_hr_norm;
            } else if (mode == UI_MODE_BOTH) {
                y_plot = map_to_band(y_hr_norm, 24, 47);
            }

            if (y_plot != 0xFF)
            {
                hr_a = hr_b;
                hr_b.y = y_plot;

                if (hr_b.x < 84) { hr_b.x += x_increment; }
                else {
                    hr_b.x = 0;
                    /* Limpia banda de HR */
                    if (mode == UI_MODE_HR) {
                        LCD_nokia_clear_range_FrameBuffer(0, 3, 252);
                    } else {
                        LCD_nokia_clear_range_FrameBuffer(0, 4, 168);
                    }
                }
                drawline(hr_a.x, hr_a.y, hr_b.x, hr_b.y, 50);
            }
        }

        /* ===== GRÁFICA TEMP ===== */
        if (xQueueReceive(PointQueueTEMP, &y_tp_norm, 0) == pdTRUE)
        {
            uint8_t y_plot = 0xFF;

            if (mode == UI_MODE_TEMP) {
                y_plot = (uint8_t)y_tp_norm;
            } else if (mode == UI_MODE_BOTH) {
                y_plot = map_to_band(y_tp_norm, 0, 23);
            }

            if (y_plot != 0xFF)
            {
                tp_a = tp_b;
                tp_b.y = y_plot;

                if (tp_b.x < 84) { tp_b.x += x_increment; }
                else {
                    tp_b.x = 0;
                    /* Limpia banda de TEMP */
                    if (mode == UI_MODE_TEMP) {
                        LCD_nokia_clear_range_FrameBuffer(0, 3, 252);
                    } else {
                        LCD_nokia_clear_range_FrameBuffer(0, 0, 168);
                    }
                }
                drawline(tp_a.x, tp_a.y, tp_b.x, tp_b.y, 50);
            }
        }

        /* ===== NÚMEROS ===== */
        if (xQueueReceive(NumberQueueHR, &hr_centimV, 0) == pdTRUE)
        {
            /* Ubica HR en fila 1 */
            if (mode == UI_MODE_HR || mode == UI_MODE_BOTH)
                LCD_PrintCentimV(0, 1, hr_centimV);
            else
                LCD_nokia_clear_range_FrameBuffer(0, 1, 35);
        }

        if (xQueueReceive(NumberQueueTEMP, &t_deciC, 0) == pdTRUE)
        {
            /* Ubica TEMP en fila 2 */
            if (mode == UI_MODE_TEMP || mode == UI_MODE_BOTH)
                LCD_PrintDeciC(0, 2, t_deciC);
            else
                LCD_nokia_clear_range_FrameBuffer(0, 2, 40);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void GraphProcess_thread(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str adcConvVal;
    uint16_t y_norm;

    for (;;)
    {
        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
            y_norm = (uint16_t)((adcConvVal.data * 48U) / 4096U);
            if (y_norm > 47U) y_norm = 47U;

            if (adcConvVal.convSource == 0) {
                xQueueSend(PointQueue, &y_norm, portMAX_DELAY);
            } else {
                xQueueSend(PointQueueTEMP, &y_norm, portMAX_DELAY);
            }

            taskYIELD();
        }
    }
}

static void NumberProcess_thread(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str adcConvVal;
    uint16_t outValue;

    for (;;)
    {
        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
            if (adcConvVal.convSource == 0)
            {
                outValue = (uint16_t)((adcConvVal.data * 300U) / 4095U);
                xQueueSend(NumberQueueHR, &outValue, portMAX_DELAY);
            }
            else
            {
                outValue = (uint16_t)(340U + ((adcConvVal.data * 60U) / 4095U));
                xQueueSend(NumberQueueTEMP, &outValue, portMAX_DELAY);
            }

            taskYIELD();
        }
    }
}

static void AdcForwarder_task(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str m;
    for (;;)
    {
        if (ADC_Receive(&m, portMAX_DELAY))
        {
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);
            taskYIELD();
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);
        }
    }
}
