/*
 * scheduler.c
 *
 *  Created on: 1 oct 2025
 *      Author: luisg
 */

#include "scheduler.h"

static uint8_t current_thread = 0;
static uint32_t tick_counter = 0;
static uint8_t started = 0;


void thread_a(void){
 while(1){
	 uint8_t string[]="Executing Thread A\r\n";
	 terminal_send(string,sizeof(string));
 }
}

void thread_b(void){
 while(1){
	 uint8_t string[]="Executing Thread B\r\n";
	 terminal_send(string,sizeof(string));
 }
}

void thread_c(void){
 while(1){
	 uint8_t string[]="Executing Thread C\r\n";
	 terminal_send(string,sizeof(string));
 }
}

void scheduler_init(void) {
	wr_main_stack_ptr(0xFFFFFFFD);
	UART_init();
	uint8_t initmsg[] = "initializing";
	terminal_send(initmsg,sizeof(initmsg));
    current_thread = 0;
    tick_counter = 0;
}
uint8_t taskA[] = "TASK A";
uint8_t taskB[] = "TASK B";
uint8_t taskC[] = "TASK C";

lp_rtos_task_t lp_rtos_tasks_database[] = {
    { "TASK A", NULL, thread_a, READY, { {0} }, NULL },
    { "TASK B", NULL, thread_b, READY, { {0} }, NULL },
    { "TASK C", NULL, thread_c, READY, { {0} }, NULL },
};
void lp_rtos_trap(void) {
    while (1) {
    	__asm volatile ("NOP");
    }
}

static void wr_main_stack_ptr(uint32_t val)
{
	__asm__ ("MSR msp, %0\n\t" : : "r" (val) );
}



void init_task_stack(uint32_t task_id)
{
	lp_rtos_task_t* task_db = lp_rtos_tasks_database;
	uint8_t* stack_base_ptr;
	stack_frame_t* stack_frame_ptr;
	// Calculate the task's stack base pointer
	stack_base_ptr = (uint8_t*)(((uint32_t)(&task_db[task_id].stack[0]) +
	STACK_SIZE_WORDS - 1) & (0xFFFFFFF8));
	// Calculate the task's stack frame pointer
	stack_frame_ptr = (stack_frame_t*)(stack_base_ptr -
	sizeof(stack_frame_t));
	task_db[task_id].psp = (uint8_t*)stack_frame_ptr;
	// Initialize the stack frame with zeros
	memset(stack_frame_ptr, 0, sizeof(stack_frame_t));

	task_db[task_id].StackFrameView =
	(stack_frame_t*)stack_frame_ptr;
	stack_frame_ptr->lr = (uint32_t)lp_rtos_trap;
	stack_frame_ptr->r0 = (uint32_t)task_id;
	stack_frame_ptr->pc = (uint32_t)task_db[task_id].task_function;
	stack_frame_ptr->psr = 0x01000000;
}

void scheduler_start(void) {
    // Configurar prioridad más baja para PendSV

	init_task_stack(0);
	init_task_stack(1);
	init_task_stack(2);

    NVIC_SetPriority(PendSV_IRQn, 0xFF);
    // Configurar SysTick para 1ms
    SysTick_Config(SystemCoreClock / 1000);




    // Primer cambio de contexto
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;
}

uint8_t scheduler_next_thread(void) {
    return (current_thread + 1) % NUM_THREADS;
}

void SysTick_Handler(void) {
    tick_counter++;
    if (tick_counter >= THREAD_SWITCH_MS) {
        tick_counter = 0;
        SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk; // Solicita cambio de contexto
    }
}

void PendSV_Handler(void) {
    // Guardar contexto del thread actual
	if (!started) {
	        started = 1;
	    } else {
	        lp_rtos_tasks_database[current_thread].psp = cmcm_push_context();

	    }
	current_thread = scheduler_next_thread();
	// Cargar contexto del siguiente thread
	// Al salir, el hardware restaura el resto del contexto automáticamente
	cmcm_pop_context(lp_rtos_tasks_database[current_thread].psp);



    }
