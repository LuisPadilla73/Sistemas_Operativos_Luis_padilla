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

// Inicializa el planificador y los hilos (puede llamarse desde main)
void scheduler_init(void);

// Arranca el scheduler (puede invocar SVC si se usa)
void scheduler_start(void);

// Retorna el índice del siguiente thread según Round Robin
uint8_t scheduler_next_thread(void);

// Tabla global de threads (puede usarse en otros módulos)
extern thread_t thread_table[NUM_THREADS];

#endif // SCHEDULER_DRIVER_H
