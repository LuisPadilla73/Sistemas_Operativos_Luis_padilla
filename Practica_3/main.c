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
#include "alarm_manager.h"

#define hello_task_PRIORITY    (configMAX_PRIORITIES - 1)
#define GrapNumb_PRIORITY      (configMAX_PRIORITIES - 2)
#define X_INCREMENT_DEFAULT    1

typedef struct{
    uint8_t x;
    uint8_t y;
} point_str;

static TimerHandle_t SendFBTimer;
static QueueHandle_t TimeScaleMailbox;
static QueueHandle_t AdcConversionQueue;
static QueueHandle_t PointQueue;
static QueueHandle_t NumberQueueHR;     /* HR en centésimas de mV  */
static QueueHandle_t NumberQueueTEMP;   /* TEMP en décimas de °C  */

static void LCDprint_thread(void *pvParameters);
static void GraphProcess_thread(void *pvParameters);
static void NumberProcess_thread(void *pvParameters);
static void AdcForwarder_task(void *pvParameters);
static void LCD_PrintCentimV(uint8_t x, uint8_t y, uint16_t centimV);
static void LCD_PrintDeciC(uint8_t x, uint8_t y, uint16_t deciC);
static void ScreenInit(void);
static void SWInit(void);
static void SendFBCallback(TimerHandle_t ARHandle);

void BOARD_SW3_IRQ_HANDLER(void);

void ADC0_IRQHandler(void);


static void ScreenInit(void)
{
    SPI_config();
    LCD_nokia_init();
    LCD_nokia_clear();
}

static void SWInit(void)
{
    gpio_pin_config_t sw3_config = (gpio_pin_config_t){
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic  = 0,
    };

    PORT_SetPinInterruptConfig(BOARD_SW3_PORT, BOARD_SW3_GPIO_PIN, kPORT_InterruptFallingEdge);
    NVIC_SetPriority(BOARD_SW3_IRQ, 3);
    EnableIRQ(BOARD_SW3_IRQ);
    NVIC_SetPriority(BOARD_SW3_IRQ, 3);
    GPIO_PinInit(BOARD_SW3_GPIO, BOARD_SW3_GPIO_PIN, &sw3_config);
}

/* ISR SW3 */
void BOARD_SW3_IRQ_HANDLER(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static uint8_t x_increment = 1;

#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT)
    GPIO_GpioClearInterruptFlags(BOARD_SW3_GPIO, 1U << BOARD_SW3_GPIO_PIN);
#else

    if (x_increment < 10) {
        x_increment += 5;
    } else {
        x_increment = 1;
    }

    xQueueOverwriteFromISR(TimeScaleMailbox, &x_increment, &xHigherPriorityTaskWoken);
    GPIO_PortClearInterruptFlags(BOARD_SW3_GPIO, 1U << BOARD_SW3_GPIO_PIN);
#endif
    SDK_ISR_EXIT_BARRIER;
}

void ADC0_IRQHandler(void)
{
    ADC_IrqHandler();
}

static void SendFBCallback(TimerHandle_t ARHandle)
{
    (void)ARHandle;
    LCD_nokia_sent_FrameBuffer();
}

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

int main(void)
{
    uint8_t init_time_scale = X_INCREMENT_DEFAULT;

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    ScreenInit();
    SWInit();

    AdcConversionQueue = xQueueCreate(5, sizeof(adcConv_str));
    PointQueue         = xQueueCreate(5, sizeof(uint16_t));
    TimeScaleMailbox   = xQueueCreate(1, sizeof(uint8_t));

    NumberQueueHR      = xQueueCreate(5, sizeof(uint16_t));  /* centésimas de mV */
    NumberQueueTEMP    = xQueueCreate(5, sizeof(uint16_t));  /* décimas de °C   */

    SendFBTimer = xTimerCreate(
        "WriteFB",
        pdMS_TO_TICKS(33),   /* 33 ms ~ 30 FPS */
        pdTRUE,
        0,
        SendFBCallback);

    ADC_Init(10);
    ADC_Start();

    /* ======= timers ======= */
    xTimerStart(SendFBTimer, 0);

    xQueueOverwrite(TimeScaleMailbox, &init_time_scale);

    if (xTaskCreate(LCDprint_thread, "LCDprint_thread",
                    configMINIMAL_STACK_SIZE + 120, NULL, hello_task_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("LCDprint_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(GraphProcess_thread, "GraphProcess_thread",
                    configMINIMAL_STACK_SIZE + 120, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("GraphProcess_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(NumberProcess_thread, "NumberProcess_thread",
                    configMINIMAL_STACK_SIZE + 120, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("NumberProcess_thread creation failed!\r\n");
        while (1) {}
    }

    /* Forwarder del driver hacia tu cola original  */
    if (xTaskCreate(AdcForwarder_task, "AdcForwarder_task",
                    configMINIMAL_STACK_SIZE + 100, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("AdcForwarder_task creation failed!\r\n");
        while (1) {}
    }

    AlarmManager_Init();
    vTaskStartScheduler();

    for (;;){}
}

/* =================== Tareas =================== */

static void LCDprint_thread(void *pvParameters)
{
    (void)pvParameters;

    point_str point_a = {0,0};
    point_str point_b = {0,0};
    uint16_t ypointGraph;
    uint16_t hr_centimV;
    uint16_t t_deciC;
    uint8_t  x_increment;

    for (;;)
    {
        if (xQueueReceive(PointQueue, &ypointGraph, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            if (xQueuePeek(TimeScaleMailbox, &x_increment, pdMS_TO_TICKS(5)) == pdTRUE)
            {
                point_a = point_b;
                point_b.y = ypointGraph;

                if (point_b.x < 84) {
                    point_b.x += x_increment;
                } else {
                    point_b.x = 0;
                    LCD_nokia_clear_range_FrameBuffer(0, 3, 252);
                }

                drawline(point_a.x, point_a.y, point_b.x, point_b.y, 50);
            }
        }

        if (xQueueReceive(NumberQueueHR, &hr_centimV, pdMS_TO_TICKS(1)) == pdTRUE)
        {
            LCD_PrintCentimV(0, 1, hr_centimV);
        }

        if (xQueueReceive(NumberQueueTEMP, &t_deciC, pdMS_TO_TICKS(1)) == pdTRUE)
        {
            LCD_PrintDeciC(0, 2, t_deciC);
        }
    }
}

static void GraphProcess_thread(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str adcConvVal;
    uint16_t processedVal;

    for (;;)
    {
        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
            processedVal = (uint16_t)((adcConvVal.data * 48U) / 4096U);
            xQueueSend(PointQueue, &processedVal, portMAX_DELAY);
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
    QueueHandle_t qAlarm = AlarmManager_GetInputQueue();

    for (;;)
    {
        if (ADC_Receive(&m, portMAX_DELAY))
        {
            /* Reenvía a la cola original (dos veces) tal como ya hacías */
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);
            taskYIELD();
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);

            /* Tercera copia hacia el AlarmManager (no bloqueante) */
            if (qAlarm != NULL)
            {
                (void)xQueueSend(qAlarm, &m, 0);
            }
        }
    }
}

