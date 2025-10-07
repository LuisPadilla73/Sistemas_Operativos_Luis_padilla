/*
 * context_switch.c
 *
 *  Created on: 1 oct 2025
 *      Author: luisg
 */

#include "context_switch.h"

void *cmcm_push_context(void) {
    void *psp;
    __asm volatile (
        "MRS %0, psp\n"           // Obtener el PSP actual
        "STMDB %0!, {r4,r5,r6,r7,r8,r9,r10,r11}\n"   // Guardar R4–R11 en el stack
        "MSR psp, %0\n"           // Actualizar PSP
        : "=r"(psp)
    );
    return psp;
}

void cmcm_pop_context(void *psp) {
    __asm volatile (
        "LDMIA %0!, {r4,r5,r6,r7,r8,r9,r10,r11}\n"   // Cargar R4–R11 desde el stack
        "MSR psp, %0\n"           // Actualizar PSP
        :
        : "r"(psp)
    );
}


