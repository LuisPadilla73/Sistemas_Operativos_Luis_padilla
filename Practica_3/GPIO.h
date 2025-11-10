/*
 * GPIO.h
 *
 *  Created on: 4 may. 2025
 *      Author: Gabriel
 */

#ifndef DRIVERS_GPIO_H_
#define DRIVERS_GPIO_H_


#include <board.h>
#include "fsl_debug_console.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_common.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"

#define GPIO_PORT_A_Handler PORTA_IRQHandler
#define GPIO_PORT_B_Handler PORTB_IRQHandler
#define GPIO_PORT_C_Handler PORTC_IRQHandler
#define GPIO_PORT_D_Handler	PORTD_IRQHandler


#define RED 22U
#define BLUE 21U
#define GREEN 26U
#define SW2 6u
#define SW3 4u

/* sets the inputs a gpio for specified pins*/
void GPIO_setInputPins(void);
/*Set output pins as GPIO including leds */
void GPIO_setOutputPins(void);
/*initializes GPIO in general*/
void GPIO_BOARD_INIT(void);
/*initializes leds */
void allLedsINIT();
/*turns off all the leds*/
void allOFF();
//* self explanatory*/
void whiteToggle();
void redToggle();
void greenToggle();
void blueToggle();
void yellowToggle();
void cianToggle();
void magentaToggle();
void redOFF();
void redON();


#endif /* DRIVERS_GPIO_H_ */
