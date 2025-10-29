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
#include "queue.h"
#include "timers.h"
#include "semphr.h"

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "event_groups.h"

#include "SPI.h"
#include "LCD_nokia.h"
#include "LCD_nokia_images.h"
#include <stdio.h>

// Definición de banderas para el grupo de eventos
#define BIT_SECONDS (1 << 0)
#define BIT_MINUTES (1 << 1)
#define BIT_HOURS   (1 << 2)

// Hora preestablecida para disparar la alarma
#define ALARM_SECONDS 10
#define ALARM_MINUTES 9
#define ALARM_HOURS   9

// Objetos de sincronización y comunicación de FreeRTOS
SemaphoreHandle_t seconds_semaphore;
SemaphoreHandle_t minutes_semaphore;
SemaphoreHandle_t hours_semaphore;
QueueHandle_t time_queue;
EventGroupHandle_t event_group;
SemaphoreHandle_t lcd_mutex;
TimerHandle_t timer_1s;

// Prototipos de tareas y callback del timer
void seconds_thread(void *pvParameters);
void minutes_thread(void *pvParameters);
void hours_thread(void *pvParameters);
void print_thread(void *pvParameters);
void alarm_thread(void *pvParameters);
void timer_callback(TimerHandle_t xTimer);

// Estructura para enviar mensajes de tiempo por la cola
typedef enum { seconds_type, minutes_type, hours_type } time_types_t;
typedef struct {
    time_types_t time_type;
    uint8_t value;
} time_msg_t;

int main(void)
{
    // Inicialización
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();
    SPI_config();
    LCD_nokia_init();
    LCD_nokia_clear();

    // Mostrar imagen de bienvenida
    //LCD_nokia_bitmap(ITESO);
    vTaskDelay(pdMS_TO_TICKS(2000));
    LCD_nokia_clear();

    // semaforos, cola, grupo de eventos y mutex
    seconds_semaphore = xSemaphoreCreateBinary();
    minutes_semaphore = xSemaphoreCreateBinary();
    hours_semaphore = xSemaphoreCreateBinary();
    time_queue = xQueueCreate(10, sizeof(time_msg_t));
    event_group = xEventGroupCreate();
    lcd_mutex = xSemaphoreCreateMutex();

    // Creación del timer de 1 segundo con auto-reload
    timer_1s = xTimerCreate("Timer1s", pdMS_TO_TICKS(1000), pdTRUE, NULL, timer_callback);
    xTimerStart(timer_1s, 0);

    // Creación de tareas con sus respectivas prioridades
    xTaskCreate(seconds_thread, "Seconds", configMINIMAL_STACK_SIZE + 100, NULL, 1, NULL);
    xTaskCreate(minutes_thread, "Minutes", configMINIMAL_STACK_SIZE + 100, NULL, 1, NULL);
    xTaskCreate(hours_thread, "Hours", configMINIMAL_STACK_SIZE + 100, NULL, 1, NULL);
    xTaskCreate(print_thread, "Print", configMINIMAL_STACK_SIZE + 100, NULL, 2, NULL);
    xTaskCreate(alarm_thread, "Alarm", configMINIMAL_STACK_SIZE + 100, NULL, 3, NULL);

    vTaskStartScheduler();

    while (1) {}
}

// Callback del timer libera el semáforo de segundos cada segundo
void timer_callback(TimerHandle_t xTimer) {
    xSemaphoreGive(seconds_semaphore);
}

// Tarea que cuenta segundos y dispara eventos cuando corresponde
void seconds_thread(void *pvParameters) {
    uint8_t seconds = 0;
    time_msg_t msg;

    for (;;) {
        xSemaphoreTake(seconds_semaphore, portMAX_DELAY);
        seconds++;
        if (seconds == 60) {
            seconds = 0;
            xSemaphoreGive(minutes_semaphore);
        }

        if (seconds == ALARM_SECONDS) {
            xEventGroupSetBits(event_group, BIT_SECONDS);
        }

        msg.time_type = seconds_type;
        msg.value = seconds;
        xQueueSend(time_queue, &msg, 0);
    }
}

// Tarea que cuenta minutos y dispara eventos cuando corresponde
void minutes_thread(void *pvParameters) {
    uint8_t minutes = 0;
    time_msg_t msg;

    for (;;) {
        xSemaphoreTake(minutes_semaphore, portMAX_DELAY);
        minutes++;
        if (minutes == 60) {
            minutes = 0;
            xSemaphoreGive(hours_semaphore);
        }

        if (minutes == ALARM_MINUTES) {
            xEventGroupSetBits(event_group, BIT_MINUTES);
        }

        msg.time_type = minutes_type;
        msg.value = minutes;
        xQueueSend(time_queue, &msg, 0);
    }
}
// Tarea que cuenta horas y dispara eventos cuando corresponde
void hours_thread(void *pvParameters) {
    uint8_t hours = 0;
    time_msg_t msg;

    for (;;) {
        xSemaphoreTake(hours_semaphore, portMAX_DELAY);
        hours++;
        if (hours == 24) {
            hours = 0;
        }

        if (hours == ALARM_HOURS) {
            xEventGroupSetBits(event_group, BIT_HOURS);
        }

        msg.time_type = hours_type;
        msg.value = hours;
        xQueueSend(time_queue, &msg, 0);
    }
}
// Tarea que recibe mensajes de tiempo y actualiza la pantalla LCD
void print_thread(void *pvParameters) {
    time_msg_t msg;
    uint8_t hours = 0, minutes = 0, seconds = 0;
    char buffer[16];

    for (;;) {
        if (xQueueReceive(time_queue, &msg, portMAX_DELAY) == pdPASS) {
            switch (msg.time_type) {
                case seconds_type: seconds = msg.value; break;
                case minutes_type: minutes = msg.value; break;
                case hours_type:   hours = msg.value; break;
            }

            // Formatear la hora como HH:MM:SS
            snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);

            // Mostrar la hora en la primera línea de la pantalla
            xSemaphoreTake(lcd_mutex, portMAX_DELAY);
            LCD_nokia_goto_xy(0, 0);
            LCD_nokia_send_string((uint8_t *)buffer);
            xSemaphoreGive(lcd_mutex);
        }
    }
}
// Tarea que espera que las tres banderas estén activadas para mostrar "ALARM"
void alarm_thread(void *pvParameters) {
    const EventBits_t all_flags = BIT_SECONDS | BIT_MINUTES | BIT_HOURS;

    for (;;) {
        // Espera hasta que las tres condiciones de tiempo coincidan con la alarma
        xEventGroupWaitBits(event_group, all_flags, pdTRUE, pdTRUE, portMAX_DELAY);

        // Mostrar "ALARM" en la segunda línea de la pantalla
        xSemaphoreTake(lcd_mutex, portMAX_DELAY);
        LCD_nokia_goto_xy(0, 1);
        LCD_nokia_send_string((uint8_t *)"ALARM");
        xSemaphoreGive(lcd_mutex);

    }
}


