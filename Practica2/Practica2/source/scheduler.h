/*
 * scheduler.h
 *
 *  Created on: 1 oct 2025
 *      Author: luisg
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include "scheduler_types.h"
#include "context_switch.h"
#include "scheduler_types.h"
#include "fsl_device_registers.h" // Para SCB->ICSR
#include "UART_SDK.h"



// Inicializa el planificador y los hilos (puede llamarse desde main)
void scheduler_init(void);
void lp_rtos_trap(void);
static void wr_main_stack_ptr(uint32_t val);
// Arranca el scheduler (puede invocar SVC si se usa)
void scheduler_start(void);

// Retorna el índice del siguiente thread según Round Robin
uint8_t scheduler_next_thread(void);

void thread_a(void);
void thread_b(void);
void thread_c(void);

// Tabla global de threads (puede usarse en otros módulos)
extern lp_rtos_task_t thread_table[NUM_THREADS];

#endif // SCHEDULER_DRIVER_H
