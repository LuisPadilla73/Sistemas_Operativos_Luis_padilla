/*
 * thread_init.h
 *
 *  Created on: 5 oct 2025
 *      Author: luisg
 */

#ifndef THREAD_INIT_H_
#define THREAD_INIT_H_

#include <stdint.h>
#include "scheduler_types.h"

// Inicializa el stack de un thread dado su estructura y función
void init_thread_stack(thread_t *thread, void (*thread_function)(void));

// Función trap para inicializar LR con dirección válida
void thread_trap(void);

#endif /* THREAD_INIT_H_ */
