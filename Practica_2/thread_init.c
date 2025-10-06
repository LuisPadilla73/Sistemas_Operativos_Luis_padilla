/*
 * thread_init.c
 *
 *  Created on: 5 oct 2025
 *      Author: luisg
 */

#include "thread_init.h"
#include <string.h>

void thread_trap(void) {
    while (1) {
        // Nunca se ejecuta, solo sirve como dirección válida para LR
    }
}

void init_thread_stack(thread_t *thread, void (*thread_function)(void)) {
    // Calcular la base del stack alineada a 8 bytes (requisito del ARM Cortex-M)
    uint32_t *stack_base = (uint32_t *)(((uint32_t)&thread->stack[0] +
                                         STACK_SIZE_WORDS * sizeof(uint32_t) - 1) & ~0x7);

    // Calcular el puntero al frame simulado (espacio donde se guardará el contexto)
    stack_frame_t *frame = (stack_frame_t *)(stack_base - sizeof(stack_frame_t)/sizeof(uint32_t));

    // Limpiar el frame para evitar valores basura
    memset(frame, 0, sizeof(stack_frame_t));

    // Inicializar los registros que el hardware espera al hacer POP
    frame->r0  = (uint32_t)0;                    // Argumento opcional (no usado)
    frame->lr  = (uint32_t)thread_trap;          // Dirección válida para LR
    frame->pc  = (uint32_t)thread_function;      // Punto de entrada del thread
    frame->psr = 0x01000000;                     // PSR con bit Thumb activo


    // Guardar el PSP en la estructura del thread
    thread->psp = (uint32_t *)frame;
}

