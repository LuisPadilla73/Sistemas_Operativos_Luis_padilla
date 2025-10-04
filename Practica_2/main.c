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
#include "scheduler.h"
#include "scheduler_types.h"
#include "context_switch.h"

// Simulación de funciones vacías para evitar errores de linker
void thread_a(void) {}
void thread_b(void) {}
void thread_c(void) {}

// Simulación de inicialización mínima
extern thread_t thread_table[NUM_THREADS];

int main(void) {
    // Inicialización mínima para probar integración
    scheduler_init();
    scheduler_start();

    // Bucle vacío, no se ejecuta multitarea real
    while (1) {
        // Espera pasiva
    }
}
