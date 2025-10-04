/*
 * context_switch.h
 *
 *  Created on: 1 oct 2025
 *      Author: luisg
 */

#ifndef CONTEXT_SWITCH_H
#define CONTEXT_SWITCH_H

#include <stdint.h>

// Guarda los registros R4–R11 en el stack del thread actual y retorna el nuevo PSP
void *cmcm_push_context(void);

// Carga los registros R4–R11 desde el stack del siguiente thread y actualiza el PSP
void cmcm_pop_context(void *psp);

#endif // CONTEXT_SWITCH_H
