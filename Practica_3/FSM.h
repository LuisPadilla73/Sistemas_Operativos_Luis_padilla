/*
 * FSM.h
 *
 *  Created on: 9 nov 2025
 *      Author: luisg
 */

#ifndef FSM_H_
#define FSM_H_

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "timers.h"

/* SDK NXP */
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_clock.h"

/* ================== Pines de botones (según tu board) ==================
 *  - SW3 = PTA4  -> MODO
 *  - SW2 = PTC6  -> ZOOM
 */
#ifndef UI_SW3_PORT
#  define UI_SW3_PORT     PORTA
#  define UI_SW3_GPIO     GPIOA
#  define UI_SW3_PIN      4U
#  define UI_SW3_IRQn     PORTA_IRQn
#endif

#ifndef UI_SW2_PORT
#  define UI_SW2_PORT     PORTC
#  define UI_SW2_GPIO     GPIOC
#  define UI_SW2_PIN      6U
#  define UI_SW2_IRQn     PORTC_IRQn
#endif

typedef enum {
    UI_MODE_HR   = 0,
    UI_MODE_TEMP = 1,
    UI_MODE_BOTH = 2
} ui_mode_t;

bool FSM_Init(void);

QueueHandle_t FSM_GetZoomMailbox(void);

QueueHandle_t FSM_GetModeMailbox(void);

void FSM_SetMode(ui_mode_t mode);

void FSM_SetZoom(uint8_t zoom);

void PORTA_IRQHandler(void);  /* SW3 -> MODO */
void PORTC_IRQHandler(void);  /* SW2 -> ZOOM */

#endif /* FSM_H_ */
