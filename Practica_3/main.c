/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
/*SW 3 ZOOM	*/
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* NXP / Board */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_adc16.h"

/* Pantalla / SPI / Draw */
#include "LCD_nokia.h"
#include "LCD_nokia_images.h"
#include "SPI.h"
#include "nokia_draw.h"
#include "ADC.h"

/* =================== Definiciones =================== */
#define hello_task_PRIORITY    (configMAX_PRIORITIES - 1)
#define GrapNumb_PRIORITY      (configMAX_PRIORITIES - 2)
#define X_INCREMENT_DEFAULT    1

/* ----- Tipos propios (como en tu P3.txt) ----- */
typedef struct{
    uint8_t x;
    uint8_t y;
} point_str;

/* =================== Recursos FreeRTOS =================== */
static TimerHandle_t SendFBTimer;

static QueueHandle_t TimeScaleMailbox;
static QueueHandle_t AdcConversionQueue;
static QueueHandle_t PointQueue;

/* Colas numéricas nuevas, formateables para display */
static QueueHandle_t NumberQueueHR;     /* HR en centésimas de mV (0..300) */
static QueueHandle_t NumberQueueTEMP;   /* TEMP en décimas de °C (340..400) */

/* =================== Prototipos =================== */
static void LCDprint_thread(void *pvParameters);
static void GraphProcess_thread(void *pvParameters);
static void NumberProcess_thread(void *pvParameters);

/* Nueva: reenvía del driver ADC -> AdcConversionQueue (dos veces) */
static void AdcForwarder_task(void *pvParameters);

/* Helpers de impresión formateada */
static void LCD_PrintCentimV(uint8_t x, uint8_t y, uint16_t centimV);
static void LCD_PrintDeciC(uint8_t x, uint8_t y, uint16_t deciC);

/* Init de subsistemas */
static void ScreenInit(void);
static void SWInit(void);

/* Callbacks de timers */
static void SendFBCallback(TimerHandle_t ARHandle);

/* ISR botón */
void BOARD_SW3_IRQ_HANDLER(void);

/* ISR ADC -> delega al driver */
void ADC0_IRQHandler(void);

/* =================== Código =================== */

static void ScreenInit(void)
{
    SPI_config();
    LCD_nokia_init();
    LCD_nokia_clear();
}

static void SWInit(void)
{
    /* Config de SW3 con IRQ */
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

/* ISR SW3: sugerido cambiar a 1->5->10->1 (aquí dejo tu 1->6->1 original adaptado) */
void BOARD_SW3_IRQ_HANDLER(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static uint8_t x_increment = 1;

#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT)
    GPIO_GpioClearInterruptFlags(BOARD_SW3_GPIO, 1U << BOARD_SW3_GPIO_PIN);
#else
    /* Mejor sería:
       static uint8_t steps[] = {1,5,10};
       static uint8_t idx = 0;
       x_increment = steps[idx];
       idx = (idx + 1) % 3;
    */
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

/* ISR de ADC: delega al driver (no variables globales) */
void ADC0_IRQHandler(void)
{
    ADC_IrqHandler();
}

static void SendFBCallback(TimerHandle_t ARHandle)
{
    (void)ARHandle;
    LCD_nokia_sent_FrameBuffer();
}

/* =================== Helpers de impresión =================== */
/* HR: centésimas de mV -> "A.BC mv" (ej.: 152 => "1.52 mv") */
static void LCD_PrintCentimV(uint8_t x, uint8_t y, uint16_t centimV)
{
    uint8_t A = (uint8_t)((centimV / 100U) % 10U);
    uint8_t B = (uint8_t)((centimV / 10U)  % 10U);
    uint8_t C = (uint8_t)( centimV         % 10U);

    /* Limpia zona (ancho aprox ~30 cols) */
    LCD_nokia_clear_range_FrameBuffer(x, y, 35);

    LCD_nokia_write_char_xy_FB(x +  0, y, (uint8_t)('0' + A));
    LCD_nokia_write_char_xy_FB(x +  5, y, (uint8_t)('0' + B));
    LCD_nokia_write_char_xy_FB(x + 10, y, '.');
    LCD_nokia_write_char_xy_FB(x + 15, y, (uint8_t)('0' + C));
    LCD_nokia_write_char_xy_FB(x + 20, y, ' ');
    LCD_nokia_write_string_xy_FB(x + 25, y, (uint8_t*)"mv", 2);
}

/* TEMP: décimas de °C -> "AA.B °C" (ej.: 365 => "36.5 °C") */
static void LCD_PrintDeciC(uint8_t x, uint8_t y, uint16_t deciC)
{
    uint16_t entero  = deciC / 10U;   /* 34..40 */
    uint8_t  decimal = deciC % 10U;   /* 0..9   */

    uint8_t tens = (uint8_t)((entero / 10U) % 10U);
    uint8_t ones = (uint8_t)( entero        % 10U);

    LCD_nokia_clear_range_FrameBuffer(x, y, 40);

    LCD_nokia_write_char_xy_FB(x +  0, y, (uint8_t)('0' + tens));
    LCD_nokia_write_char_xy_FB(x +  5, y, (uint8_t)('0' + ones));
    LCD_nokia_write_char_xy_FB(x + 10, y, '.');
    LCD_nokia_write_char_xy_FB(x + 15, y, (uint8_t)('0' + decimal));
    LCD_nokia_write_char_xy_FB(x + 20, y, ' ');
    /* Si tu font no tiene '°', puedes usar 'o' o espacio */
    // LCD_nokia_write_char_xy_FB(x + 25, y, 0xDF); /* A veces 0xDF se usa como '°' en fuentes personalizadas */
    LCD_nokia_write_char_xy_FB(x + 25, y, 'C');    /* “ C” es suficiente */
}

/* =================== main() =================== */
int main(void)
{
    uint8_t init_time_scale = X_INCREMENT_DEFAULT;

    /* Init HW base */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    ScreenInit();
    SWInit();

    /* ========= COLAS ========= */
    AdcConversionQueue = xQueueCreate(5, sizeof(adcConv_str));
    PointQueue         = xQueueCreate(5, sizeof(uint16_t));
    TimeScaleMailbox   = xQueueCreate(1, sizeof(uint8_t));

    /* NUEVAS colas de números formateables */
    NumberQueueHR      = xQueueCreate(5, sizeof(uint16_t));  /* centésimas de mV */
    NumberQueueTEMP    = xQueueCreate(5, sizeof(uint16_t));  /* décimas de °C   */

    /* ========= Timer de pantalla ========= */
    SendFBTimer = xTimerCreate(
        "WriteFB",
        pdMS_TO_TICKS(33),   /* 33 ms ~ 30 FPS */
        pdTRUE,
        0,
        SendFBCallback);

    /* ======= Driver ADC: su cola interna + timer 100ms ======= */
    ADC_Init(10);
    ADC_Start();

    /* ======= Arranque de timers ======= */
    xTimerStart(SendFBTimer, 0);

    /* Publica time-scale inicial */
    xQueueOverwrite(TimeScaleMailbox, &init_time_scale);

    /* ========= Tareas ========= */
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

    /* Forwarder del driver hacia tu cola original (dos envíos) */
    if (xTaskCreate(AdcForwarder_task, "AdcForwarder_task",
                    configMINIMAL_STACK_SIZE + 100, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("AdcForwarder_task creation failed!\r\n");
        while (1) {}
    }

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
        /* --- GRAFICADO (igual que ya lo tienes) --- */
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

        /* --- NÚMERO HR (mV con 2 decimales) en fila 1 --- */
        if (xQueueReceive(NumberQueueHR, &hr_centimV, pdMS_TO_TICKS(1)) == pdTRUE)
        {
            LCD_PrintCentimV(0, 1, hr_centimV);
        }

        /* --- NÚMERO TEMP (°C con 1 decimal) en fila 2 --- */
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
        /* El forwarder reenvía aquí lo que emite el driver (dos veces) */
        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
            /* Escala a 0..47 para gráfica (ajústalo si quieres) */
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
            if (adcConvVal.convSource == 0) /* ADC_SRC_HEART */
            {
                /* HR: 0..4095 -> 0..300 (centésimas de mV) */
                outValue = (uint16_t)((adcConvVal.data * 300U) / 4095U);
                xQueueSend(NumberQueueHR, &outValue, portMAX_DELAY);
            }
            else /* ADC_SRC_TEMP */
            {
                /* TEMP: 0..4095 -> 340..400 (décimas de °C) */
                outValue = (uint16_t)(340U + ((adcConvVal.data * 60U) / 4095U));
                xQueueSend(NumberQueueTEMP, &outValue, portMAX_DELAY);
            }

            taskYIELD();
        }
    }
}

/* =================== Forwarder del driver a tus colas =================== */
static void AdcForwarder_task(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str m;
    for (;;)
    {
        /* Leer del driver (ISR->cola interna del driver) */
        if (ADC_Receive(&m, portMAX_DELAY))
        {
            /* Reenviar a tu cola original, dos veces, como hacía tu ISR previa */
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);
            taskYIELD();
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);
        }
    }
}
