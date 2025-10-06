/*
 * Copyright 2016-2025 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file    PRACTICA2.c
 * @brief   Application entry point.
 */
#include "board.h"
#include "clock_config.h"
#include "pin_mux.h"
#include "scheduler.h"
#include "scheduler_types.h"
#include "thread_init.h"
#include "threads.h"
#include "UART_SDK.h"

// Tabla global de threads
extern thread_t thread_table[NUM_THREADS];

int main(void) {
    // Inicialización básica del sistema
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    // Inicializar UART (opcional si usas terminal_send)
    UART_init(NULL, NULL, NULL);

    // Inicializar stacks de los threads
    init_thread_stack(&thread_table[0], thread_a);
    init_thread_stack(&thread_table[1], thread_b);
    init_thread_stack(&thread_table[2], thread_c);

    // Inicializar y arrancar el planificador
    scheduler_init();
    scheduler_start();

    // Nunca se regresa aquí, los threads toman control
    while (1) {}
}
