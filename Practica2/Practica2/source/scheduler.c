/*
 * scheduler.c
 *
 *  Created on: 1 oct 2025
 *      Author: luisg
 */

#include "scheduler.h"

static uint8_t current_thread = 0;
static uint32_t tick_counter = 0;


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

void thread_b(void){
 while(1){
	 uint8_t string[]="Executing Thread C\r\n";
	 terminal_send(string,sizeof(string));
 }
}

void scheduler_init(void) {
    // Se inicializan los threads (no los tengo todavía)
    // Por ahora solo reiniciamos el índice y contador
    current_thread = 0;
    tick_counter = 0;
}

void scheduler_start(void) {
    // Configurar prioridad más baja para PendSV
    NVIC_SetPriority(PendSV_IRQn, 0xFF);
    // Configurar SysTick para 1ms
    SysTick_Config(SystemCoreClock / 1000);
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
    thread_table[current_thread].psp = cmcm_push_context();

    // Seleccionar siguiente thread
    current_thread = scheduler_next_thread();

    // Cargar contexto del siguiente thread
    // Al salir, el hardware restaura el resto del contexto automáticamente
    cmcm_pop_context(thread_table[current_thread].psp);
    }
