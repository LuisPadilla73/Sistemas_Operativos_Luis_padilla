/*
 * threads.c
 *
 *  Created on: 5 oct 2025
 *      Author: luisg
 */

#include "threads.h"
#include "UART_SDK.h"
#include <stdio.h>

// Contadores individuales para cada thread
static uint32_t counter_a = 0;
static uint32_t counter_b = 0;
static uint32_t counter_c = 0;

/**
 * @brief Thread A: imprime mensaje y contador por UART
 */
void thread_a(void) {
    char buffer[40];
    while (1) {
        counter_a++;
        snprintf(buffer, sizeof(buffer), "Thread A - Count: %lu\r\n", counter_a);
        terminal_send((uint8_t *)buffer, (uint8_t)strlen(buffer));
    }
}

void thread_b(void) {
    char buffer[40];
    while (1) {
        counter_b++;
        snprintf(buffer, sizeof(buffer), "Thread B - Count: %lu\r\n", counter_b);
        terminal_send((uint8_t *)buffer, (uint8_t)strlen(buffer));
    }
}

void thread_c(void) {
    char buffer[40];
    while (1) {
        counter_c++;
        snprintf(buffer, sizeof(buffer), "Thread C - Count: %lu\r\n", counter_c);
        terminal_send((uint8_t *)buffer, (uint8_t)strlen(buffer));
    }
}
