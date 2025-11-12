/*
 * GPIO_D.h
 *
 *  Created on: Mar 11, 2025
 *      Author: DOOMSLAYER
 */

#ifndef DRIVERS_GPIO_D_H_
#define DRIVERS_GPIO_D_H_
#include <board.h>
#include "fsl_debug_console.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_common.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"


#define RED 22U
#define BLUE 21U
#define GREEN 26U
#define SW2 6u
#define SW3 4u

void flags_array(uint8_t *array);


void GPIO_setInputPins(void);
void GPIO_setOutputPins(void);
void GPIO_BOARD_INIT(void);
void allLedsINIT();
void redSet();
void redClear();
void allOFF();
void whiteToggle();
void redToggle();
void greenToggle();
void blueToggle();
void yellowToggle();
void cianToggle();
void magentaToggle();





#endif /* DRIVERS_GPIO_D_H_ */
