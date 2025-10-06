/*
 * scheduler.h
 *
 *  Created on: 1 oct 2025
 *      Author: luisg
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>

// Inicializa SysTick y PendSV
void scheduler_init(void);

// Inicia el scheduler y salta al primer thread
void scheduler_start(void);

#endif /* SCHEDULER_DRIVER_H_ */
