/*
 * scheduler_type.h
 *
 *  Created on: 1 oct 2025
 *      Author: luisg
 */

#ifndef SCHEDULER_TYPES_H
#define SCHEDULER_TYPES_H

#include <stdint.h>

#define NUM_THREADS         3
#define STACK_SIZE_WORDS    128
#define THREAD_SWITCH_MS    5
#define INITIAL_PSR         0x01000000

// Estructura que representa el marco de pila completo de un thread
typedef struct {
    // Guardados por software
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
    // Guardados por hardware
    uint32_t r0, r1, r2, r3, r12, lr, pc, psr;
} stack_frame_t;

// Estructura de control del thread (TCB)
typedef struct {
    void *psp;                         // Process Stack Pointer
    uint32_t stack[STACK_SIZE_WORDS]; // Stack reservado para el thread
} thread_t;

#endif
