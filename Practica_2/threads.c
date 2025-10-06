/*
 * threads.c
 *
 *  Created on: 5 oct 2025
 *      Author: luisg
 */

// threads.c
#include "threads.h"
#include "UART_SDK.h"

void thread_a(void) {
    while (1) {
        terminal_send((uint8_t *)"Thread A\n", 9);
    }
}

void thread_b(void) {
    while (1) {
        terminal_send((uint8_t *)"Thread B\n", 9);
    }
}

void thread_c(void) {
    while (1) {
        terminal_send((uint8_t *)"Thread C\n", 9);
    }
}
