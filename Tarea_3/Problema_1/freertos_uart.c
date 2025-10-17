/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"

/* Freescale includes. */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"

#include "fsl_uart.h"

#define DEMO_UART UART0
#define DEMO_UART_CLKSRC UART0_CLK_SRC
#define DEMO_UART_CLK_FREQ CLOCK_GetFreq(UART0_CLK_SRC)
#define DEMO_UART_RX_TX_IRQn UART0_RX_TX_IRQn
#define DEMO_UART_IRQHandler UART0_RX_TX_IRQHandler

#define uart_task_PRIORITY (configMAX_PRIORITIES - 1)

SemaphoreHandle_t uartSemaphore;
volatile uint8_t receivedChar;

/* Prototipos */
static void uart_echo_task(void *pvParameters);
void UART_InitCustom(void);

/* MAIN */
int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    uartSemaphore = xSemaphoreCreateBinary();
    if (uartSemaphore == NULL)
    {
        PRINTF("Error al crear semáforo!\r\n");
        while (1);
    }

    UART_InitCustom();

    if (xTaskCreate(uart_echo_task, "UART_ECHO", configMINIMAL_STACK_SIZE + 100, NULL,
                    uart_task_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("Error al crear tarea!\r\n");
        while (1);
    }

    vTaskStartScheduler();
    while (1);
}

/* Inicialización UART sin RTOS wrapper */
void UART_InitCustom(void)
{
    uart_config_t config;
    UART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200;
    config.enableTx = true;
    config.enableRx = true;

    UART_Init(DEMO_UART, &config, DEMO_UART_CLK_FREQ);
    UART_EnableInterrupts(DEMO_UART, kUART_RxDataRegFullInterruptEnable);
    EnableIRQ(DEMO_UART_RX_TX_IRQn);
}

/* ISR de UART */
void DEMO_UART_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (UART_GetStatusFlags(DEMO_UART) & kUART_RxDataRegFullFlag)
    {
        receivedChar = UART_ReadByte(DEMO_UART);
        xSemaphoreGiveFromISR(uartSemaphore, &xHigherPriorityTaskWoken);
        UART_ClearStatusFlags(DEMO_UART, kUART_RxDataRegFullFlag);
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* Tarea que espera semáforo y hace echo */
static void uart_echo_task(void *pvParameters)
{
    for (;;)
    {
        if (xSemaphoreTake(uartSemaphore, portMAX_DELAY) == pdTRUE)
        {
            PRINTF("Carácter recibido: %c\r\n", receivedChar);
        }
    }
}
