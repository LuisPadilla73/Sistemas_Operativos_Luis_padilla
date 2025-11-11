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

/* =================== Definiciones de Botones =================== */

#ifndef BOARD_SW2_GPIO
#define BOARD_SW2_GPIO        GPIOC
#define BOARD_SW2_GPIO_PIN    6U
#define BOARD_SW2_PORT        PORTC
#define BOARD_SW2_IRQ         PORTC_IRQn
#define BOARD_SW2_IRQ_HANDLER PORTC_IRQHandler
#endif

#ifndef BOARD_SW3_GPIO
#define BOARD_SW3_GPIO        GPIOA
#define BOARD_SW3_GPIO_PIN    4U
#define BOARD_SW3_PORT        PORTA
#define BOARD_SW3_IRQ         PORTA_IRQn
#define BOARD_SW3_IRQ_HANDLER PORTA_IRQHandler
#endif

/* Modos de visualización del gráfico */
typedef enum {
    GRAPH_MODE_HR = 0,    /* Ritmo cardíaco */
    GRAPH_MODE_TEMP = 1   /* Temperatura */
} graph_mode_t;

/* ----- Tipos propios ----- */
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

static QueueHandle_t GraphModeMailbox;  /* Nuevo: modo de gráfico actual */

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
    /* Config de SW3 con IRQ (escala temporal) */
    gpio_pin_config_t sw3_config = {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic = 0U
    };

    /* Puerto y pin para SW3 */
    CLOCK_EnableClock(kCLOCK_PortA);
    PORT_SetPinMux(BOARD_SW3_PORT, BOARD_SW3_GPIO_PIN, kPORT_MuxAsGpio);
    PORT_SetPinInterruptConfig(BOARD_SW3_PORT, BOARD_SW3_GPIO_PIN, kPORT_InterruptFallingEdge);
    EnableIRQ(BOARD_SW3_IRQ);
    NVIC_SetPriority(BOARD_SW3_IRQ, 3);
    GPIO_PinInit(BOARD_SW3_GPIO, BOARD_SW3_GPIO_PIN, &sw3_config);

    /* ===== NUEVO: Configuración de SW2 (cambio de modo) ===== */
    gpio_pin_config_t sw2_config = {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic = 0U
    };

    /* Puerto y pin para SW2 */
    CLOCK_EnableClock(kCLOCK_PortC);
    PORT_SetPinMux(BOARD_SW2_PORT, BOARD_SW2_GPIO_PIN, kPORT_MuxAsGpio);
    PORT_SetPinInterruptConfig(BOARD_SW2_PORT, BOARD_SW2_GPIO_PIN, kPORT_InterruptFallingEdge);
    EnableIRQ(BOARD_SW2_IRQ);
    NVIC_SetPriority(BOARD_SW2_IRQ, 3);
    GPIO_PinInit(BOARD_SW2_GPIO, BOARD_SW2_GPIO_PIN, &sw2_config);
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

/* ISR SW2: Cambia entre HR y TEMP en el gráfico */
void BOARD_SW2_IRQ_HANDLER(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    static graph_mode_t current_mode = GRAPH_MODE_HR;

    /* Alternar entre modos */
    current_mode = (current_mode == GRAPH_MODE_HR) ? GRAPH_MODE_TEMP : GRAPH_MODE_HR;

    PRINTF("SW2 pressed - Changing mode to: %s\r\n",
           (current_mode == GRAPH_MODE_HR) ? "HR" : "TEMP");

    xQueueOverwriteFromISR(GraphModeMailbox, &current_mode, &xHigherPriorityTaskWoken);

    /* Limpiar bandera de interrupción */
    GPIO_PortClearInterruptFlags(BOARD_SW2_GPIO, 1U << BOARD_SW2_GPIO_PIN);

    /* Forzar cambio de contexto si es necesario */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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
    graph_mode_t init_graph_mode = GRAPH_MODE_HR;

    /* Init HW base */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    ScreenInit();
    SWInit();

    /* ========= COLAS ========= */
    AdcConversionQueue = xQueueCreate(10, sizeof(adcConv_str));
    PointQueue         = xQueueCreate(10, sizeof(uint16_t));
    TimeScaleMailbox   = xQueueCreate(1, sizeof(uint8_t));
    GraphModeMailbox   = xQueueCreate(1, sizeof(graph_mode_t));  /* Nueva cola */

    NumberQueueHR      = xQueueCreate(5, sizeof(uint16_t));
    NumberQueueTEMP    = xQueueCreate(5, sizeof(uint16_t));

    /* ========= Timer de pantalla ========= */
    SendFBTimer = xTimerCreate(
        "WriteFB",
        pdMS_TO_TICKS(33),
        pdTRUE,
        0,
        SendFBCallback);

    /* ======= Driver ADC ======= */
    ADC_Init(10);
    ADC_Start();

    /* ======= Arranque de timers ======= */
    xTimerStart(SendFBTimer, 0);

    /* Publica valores iniciales */
    xQueueOverwrite(TimeScaleMailbox, &init_time_scale);
    xQueueOverwrite(GraphModeMailbox, &init_graph_mode);

    PRINTF("System started. SW2: Change mode, SW3: Change scale\r\n");

    /* ========= Tareas ========= */
    if (xTaskCreate(LCDprint_thread, "LCDprint_thread",
                    configMINIMAL_STACK_SIZE + 200, NULL, hello_task_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("LCDprint_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(GraphProcess_thread, "GraphProcess_thread",
                    configMINIMAL_STACK_SIZE + 200, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("GraphProcess_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(NumberProcess_thread, "NumberProcess_thread",
                    configMINIMAL_STACK_SIZE + 200, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("NumberProcess_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(AdcForwarder_task, "AdcForwarder_task",
                    configMINIMAL_STACK_SIZE + 150, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
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
    graph_mode_t current_mode = GRAPH_MODE_HR;
    graph_mode_t previous_mode = GRAPH_MODE_HR;
    char mode_str[10];

    /* Dibujar encabezados fijos */
    LCD_nokia_write_string_xy_FB(0, 0, (uint8_t*)"HR:", 3);
    LCD_nokia_write_string_xy_FB(0, 1, (uint8_t*)"TEMP:", 5);

    for (;;)
    {
        /* Verificar cambio de modo */
        if (xQueueReceive(GraphModeMailbox, &current_mode, 0) == pdTRUE)
        {
            /* Si cambió el modo, limpiar gráfico y resetear puntos */
            if (current_mode != previous_mode) {
                point_a.x = 0;
                point_a.y = 0;
                point_b.x = 0;
                point_b.y = 0;
                LCD_nokia_clear_range_FrameBuffer(0, 3, 252);
                previous_mode = current_mode;

                /* Actualizar indicador de modo */
                LCD_nokia_clear_range_FrameBuffer(60, 0, 24);
                if (current_mode == GRAPH_MODE_HR) {
                    LCD_nokia_write_string_xy_FB(60, 0, (uint8_t*)"[HR]", 4);
                } else {
                    LCD_nokia_write_string_xy_FB(60, 0, (uint8_t*)"[TEMP]", 6);
                }
            }
        }

        /* --- GRAFICADO --- */
        if (xQueueReceive(PointQueue, &ypointGraph, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            if (xQueuePeek(TimeScaleMailbox, &x_increment, 0) == pdTRUE)
            {
                point_a = point_b;
                point_b.y = 47 - ypointGraph; /* Invertir Y para que 0 esté abajo */

                if (point_b.x < 84) {
                    point_b.x += x_increment;
                } else {
                    point_b.x = 0;
                    LCD_nokia_clear_range_FrameBuffer(0, 3, 252);
                }

                drawline(point_a.x, point_a.y + 3, point_b.x, point_b.y + 3, 50);
            }
        }

        /* --- NÚMERO HR --- */
        if (xQueueReceive(NumberQueueHR, &hr_centimV, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            LCD_PrintCentimV(20, 0, hr_centimV);
        }

        /* --- NÚMERO TEMP --- */
        if (xQueueReceive(NumberQueueTEMP, &t_deciC, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            LCD_PrintDeciC(30, 1, t_deciC);
        }
    }
}

static void GraphProcess_thread(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str adcConvVal;
    uint16_t processedVal;
    graph_mode_t current_mode = GRAPH_MODE_HR;

    for (;;)
    {
        /* Actualizar modo actual (sin bloquear) */
        xQueueReceive(GraphModeMailbox, &current_mode, 0);

        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
            /* Procesar solo la señal que corresponde al modo actual */
            if ((current_mode == GRAPH_MODE_HR && adcConvVal.convSource == 0) ||
                (current_mode == GRAPH_MODE_TEMP && adcConvVal.convSource == 1))
            {
                if (current_mode == GRAPH_MODE_HR) {
                    /* HR: 0-4095 -> 0-47 pixels */
                    processedVal = (uint16_t)((adcConvVal.data * 48U) / 4096U);
                } else {
                    /* TEMP: 340-400 (décimas) -> 0-47 pixels */
                    uint16_t temp_deciC = (uint16_t)(340U + ((adcConvVal.data * 60U) / 4095U));
                    processedVal = (uint16_t)((temp_deciC - 340U) * 48U / 60U);
                    /* Asegurar que esté en el rango 0-47 */
                    if (processedVal > 47) processedVal = 47;
                }

                xQueueSend(PointQueue, &processedVal, portMAX_DELAY);
            }
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
/* Función mejorada para mostrar indicador de modo */
static void UpdateModeIndicator(graph_mode_t mode)
{
    /* Limpiar área del indicador */
    LCD_nokia_clear_range_FrameBuffer(60, 0, 24);

    if (mode == GRAPH_MODE_HR) {
        LCD_nokia_write_string_xy_FB(60, 0, (uint8_t*)"[HR]", 4);
    } else {
        LCD_nokia_write_string_xy_FB(60, 0, (uint8_t*)"[TEMP]", 6);
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
