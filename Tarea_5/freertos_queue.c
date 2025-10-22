/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*System includes.*/
/* System includes */
#include <stdio.h>

/* Kernel includes */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Freescale includes */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define MAX_LOG_LENGTH 20

typedef struct {
    uint8_t thread_id;
    uint16_t data;
} Message_t;

/*******************************************************************************
 * Globals
 ******************************************************************************/
/* Logger queue handle */
static QueueHandle_t log_queue = NULL;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void tx_0_task(void *pvParameters);
static void tx_1_task(void *pvParameters);
static void tx_2_task(void *pvParameters);
static void log_task(void *pvParameters);

/*******************************************************************************
 * Code
 ******************************************************************************/

int main(void)
{
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    /* Create queue for Message_t items */
    log_queue = xQueueCreate(10, sizeof(Message_t));
    if (log_queue != NULL)
    {
        vQueueAddToRegistry(log_queue, "LogQ");
    }

    /* Create receiver task */
    if (xTaskCreate(log_task, "log_task", configMINIMAL_STACK_SIZE + 166, NULL, tskIDLE_PRIORITY + 3, NULL) != pdPASS)
    {
        PRINTF("Task creation failed!.\r\n");
        while (1);
    }

    /* Create transmitter tasks */
    if (xTaskCreate(tx_0_task, "TX0", configMINIMAL_STACK_SIZE + 166, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
    {
        PRINTF("Task creation failed!.\r\n");
        while (1);
    }

    if (xTaskCreate(tx_1_task, "TX1", configMINIMAL_STACK_SIZE + 166, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
    {
        PRINTF("Task creation failed!.\r\n");
        while (1);
    }

    if (xTaskCreate(tx_2_task, "TX2", configMINIMAL_STACK_SIZE + 166, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS)
    {
        PRINTF("Task creation failed!.\r\n");
        while (1);
    }

    vTaskStartScheduler();

    for (;;);
}

/*******************************************************************************
 * Transmitter tasks
 ******************************************************************************/
static void tx_0_task(void *pvParameters)
{
    Message_t msg;
    msg.thread_id = 0;
    for (uint16_t i = 0; i <= 65535; i++)
    {
        msg.data = i;
        xQueueSend(log_queue, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskSuspend(NULL);
}

static void tx_1_task(void *pvParameters)
{
    Message_t msg;
    msg.thread_id = 1;
    for (int i = 65535; i >= 0; i--)
    {
        msg.data = i;
        xQueueSend(log_queue, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskSuspend(NULL);
}

static void tx_2_task(void *pvParameters)
{
    Message_t msg;
    msg.thread_id = 2;
    for (uint16_t i = 0; i <= 65535; i += 2)
    {
        msg.data = i;
        xQueueSend(log_queue, &msg, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    vTaskSuspend(NULL);
}

/*******************************************************************************
 * Receiver task
 ******************************************************************************/
static void log_task(void *pvParameters)
{
    Message_t msg;
    while (1)
    {
        if (xQueueReceive(log_queue, &msg, portMAX_DELAY) == pdTRUE)
        {
            PRINTF("Datos recibidos del Th%d = %u\r\n", msg.thread_id, msg.data);
        }
    }
}
