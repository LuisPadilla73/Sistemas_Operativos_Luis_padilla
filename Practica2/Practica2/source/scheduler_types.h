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
#define TASKS_STACK_SIZE    (STACK_SIZE_WORDS)
#define THREAD_SWITCH_MS    5
#define INITIAL_PSR         0x01000000

typedef void (*lp_task_entry_t)(void);


typedef struct {
    // Guardados por software
    uint32_t r4, r5, r6, r7, r8, r9, r10, r11;
    // Guardados por hardware
    uint32_t r0, r1, r2, r3, r12, lr, pc, psr;
} stack_frame_t;

typedef enum{
	STANDBY = 0,
	READY,
	EXECUTE

}ThreadState;

typedef struct {
    const char*                name;
    void*                      psp;
    lp_task_entry_t            task_function;
    uint8_t                    ThreadState;
    uint8_t                    stack[TASKS_STACK_SIZE];
    stack_frame_t				*StackFrameView;
} lp_rtos_task_t;

// Base de datos
extern lp_rtos_task_t lp_rtos_tasks_database[];
#endif
