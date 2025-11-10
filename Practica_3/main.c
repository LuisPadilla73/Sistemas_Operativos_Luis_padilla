/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * main.c - Práctica 3 con integración del driver ADC (HEART/TEMP alternado)
 *
 * Basado en tu P3.txt original. Cambios clave:
 *  - Se elimina ADCInit() y el timer ADCConversionTimer.
 *  - Se integra ADC_Init()/ADC_Start() del driver nuevo (ADC.h/ADC.c).
 *  - Se agrega AdcForwarder_task que reenvía a AdcConversionQueue (dos veces).
 *  - La ISR de ADC ahora llama a ADC_IrqHandler() (driver).
 */

/*
 * main.c - Práctica 3 con integración del driver ADC (HEART/TEMP alternado)
 * - Elimina ADCInit() y ADCConversionTimer.
 * - Integra ADC_Init()/ADC_Start() del driver (ADC.h/ADC.c).
 * - Agrega AdcForwarder_task (reenvía a AdcConversionQueue como antes).
 * - ISR de ADC delega a ADC_IrqHandler() del driver.
 */

/*
 * main.c - Práctica 3 con integración del driver ADC (HEART/TEMP alternado)
 * - Elimina ADCInit() y ADCConversionTimer.
 * - Integra ADC_Init()/ADC_Start() del driver (ADC.h/ADC.c).
 * - Agrega AdcForwarder_task (reenvía a AdcConversionQueue como antes).
 * - ISR de ADC delega a ADC_IrqHandler() del driver.
 */

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

/* === Driver ADC integrado === */
#include "ADC.h"

/* =================== Definiciones =================== */
#define hello_task_PRIORITY    (configMAX_PRIORITIES - 1)
#define GrapNumb_PRIORITY      (configMAX_PRIORITIES - 2)
#define X_INCREMENT_DEFAULT    1

/* ----- Tipos propios que usabas en tu P3.txt ----- */
typedef struct{
    uint8_t x;
    uint8_t y;
} point_str;

/* =================== Recursos FreeRTOS =================== */
static TimerHandle_t SendFBTimer;

static QueueHandle_t TimeScaleMailbox;
static QueueHandle_t AdcConversionQueue;
static QueueHandle_t PointQueue;
static QueueHandle_t NumberQueue;

/* =================== Prototipos =================== */
static void LCDprint_thread(void *pvParameters);
static void GraphProcess_thread(void *pvParameters);
static void NumberProcess_thread(void *pvParameters);

/* Nueva: reenvía del driver ADC -> AdcConversionQueue (dos veces) */
static void AdcForwarder_task(void *pvParameters);

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
    /* Config de SW3 con IRQ (igual que en tu P3.txt) */
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

/* ISR SW3: mantiene tu lógica de zoom (sug: 1->5->10->1) */
void BOARD_SW3_IRQ_HANDLER(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static uint8_t x_increment = 1;

#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT)
    GPIO_GpioClearInterruptFlags(BOARD_SW3_GPIO, 1U << BOARD_SW3_GPIO_PIN);
#else
    /* Sugerencia (mejor): reemplaza por steps 1->5->10:
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

/* ISR de ADC: delega al driver (no usar variables globales) */
void ADC0_IRQHandler(void)
{
    ADC_IrqHandler();
}

static void SendFBCallback(TimerHandle_t ARHandle)
{
    (void)ARHandle;
    LCD_nokia_sent_FrameBuffer();
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
    NumberQueue        = xQueueCreate(5, sizeof(uint16_t));
    TimeScaleMailbox   = xQueueCreate(1, sizeof(uint8_t));

    /* ========= Timers ========= */
    SendFBTimer = xTimerCreate(
        "WriteFB",
        pdMS_TO_TICKS(33),   /* 33 ms ~ 30 FPS */
        pdTRUE,
        0,
        SendFBCallback);

    /* ======= Driver ADC: crea su cola interna y timer 100ms, arranca ======= */
    ADC_Init(10);    /* profundidad de cola interna del driver */
    ADC_Start();     /* alterna HEART/TEMP cada 100ms */

    /* ======= Arranque de timers ======= */
    xTimerStart(SendFBTimer, 0);

    /* Publica time-scale inicial */
    xQueueOverwrite(TimeScaleMailbox, &init_time_scale);

    /* ========= Tareas ========= */

    if (xTaskCreate(LCDprint_thread, "LCDprint_thread",
                    configMINIMAL_STACK_SIZE + 100, NULL, hello_task_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("LCDprint_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(GraphProcess_thread, "GraphProcess_thread",
                    configMINIMAL_STACK_SIZE + 100, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("GraphProcess_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(NumberProcess_thread, "NumberProcess_thread",
                    configMINIMAL_STACK_SIZE + 100, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("NumberProcess_thread creation failed!\r\n");
        while (1) {}
    }

    /* Nueva: forwarder del driver hacia tu cola original (dos envíos) */
    if (xTaskCreate(AdcForwarder_task, "AdcForwarder_task",
                    configMINIMAL_STACK_SIZE + 100, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("AdcForwarder_task creation failed!\r\n");
        while (1) {}
    }

    vTaskStartScheduler();

    for (;;){}
}

/* =================== Tareas existentes =================== */

static void LCDprint_thread(void *pvParameters)
{
    (void)pvParameters;

    point_str point_a = {0,0};
    point_str point_b = {0,0};
    uint16_t ypointGraph;
    uint16_t ypointNumber;
    uint8_t x_increment;
    uint8_t onedigit = 0;
    uint8_t secdigit = 0;
    uint8_t thrdigit = 0;

    for (;;)
    {
        /* Recibir para gráfica */
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

        /* Recibir para número (tu formato actual con 1 decimal) */
        if (xQueueReceive(NumberQueue, &ypointNumber, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            LCD_nokia_clear_range_FrameBuffer(0, 1, 20);
            onedigit = ((ypointNumber / 100) % 10) + 0x30;
            secdigit = ((ypointNumber / 10)  % 10) + 0x30;
            thrdigit =  (ypointNumber % 10) + 0x30;

            LCD_nokia_write_char_xy_FB(0,  1, onedigit);
            LCD_nokia_write_char_xy_FB(5,  1, secdigit);
            LCD_nokia_write_char_xy_FB(10, 1, '.');
            LCD_nokia_write_char_xy_FB(15, 1, thrdigit);
            LCD_nokia_write_char_xy_FB(20, 1, ' ');
            LCD_nokia_write_string_xy_FB(25, 1, (uint8_t*)"mv", 2);
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
        /* El forwarder reenvía aquí lo que emite el driver */
        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
            /* Escala a 0..47 para gráfica (ajústalo a tu preferencia) */
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
    uint16_t processedVal;
    for (;;)
    {
        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
            /* Escala numérica tal cual tu P3 (0..327 -> 3.27 "mv")
               Nota: más adelante separa por convSource para HR(2 dec) / TEMP(1 dec) */
            processedVal = (uint16_t)((adcConvVal.data * 8U) / 100U);
            xQueueSend(NumberQueue, &processedVal, portMAX_DELAY);
            taskYIELD();
        }
    }
}

/* =================== Nueva: Forwarder del driver a tus colas =================== */
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
