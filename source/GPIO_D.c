/*
 * GPIO_D.c
 *
 *  Created on: Mar 11, 2025
 *      Author: DOOMSLAYER
 */


/*
 * GPIO.c
 *
 *  Created on: 11 mar. 2025
 *      Author: Gabriel
 */


#include "GPIO_D.h"




void GPIO_BOARD_INIT() {

    GPIO_setInputPins();
    GPIO_setOutputPins();
}


void GPIO_setOutputPins(void) {
	const gpio_pin_config_t gpio_output_config = {
	        kGPIO_DigitalOutput,  // Set as output
	        0                     // Initial value LOW
	    };


	 /* Init output LED GPIO. */
		  GPIO_PinInit(GPIOB, RED, &gpio_output_config);
		  GPIO_PinInit(GPIOB, BLUE, &gpio_output_config);
		  GPIO_PinInit(GPIOE, GREEN, &gpio_output_config);


	/* PORTB22 (pin 68) is configured as PTB22 */
	       PORT_SetPinMux(PORTB, RED, kPORT_MuxAsGpio);
	       PORT_SetPinMux(PORTB, BLUE, kPORT_MuxAsGpio);
	       PORT_SetPinMux(PORTE, GREEN, kPORT_MuxAsGpio);
	       allOFF();



}


void GPIO_setInputPins(void) {
	const gpio_pin_config_t gpio_input_config = {
		        kGPIO_DigitalInput,  // Set as output
		        0                     // Initial value LOW
		    };
	 /*Config push button switches SW2*/
    const port_pin_config_t pcr_port_config = {
									kPORT_PullUp,
									kPORT_FastSlewRate,
									kPORT_PassiveFilterEnable,
									kPORT_OpenDrainDisable,
									kPORT_LowDriveStrength,
									kPORT_MuxAsGpio,
									kPORT_UnlockRegister};

    CLOCK_EnableClock(kCLOCK_PortA);
	CLOCK_EnableClock(kCLOCK_PortB);
	CLOCK_EnableClock(kCLOCK_PortC);
	CLOCK_EnableClock(kCLOCK_PortE);

#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT)

    GPIO_SetPinInterruptConfig(BOARD_SW3_GPIO, BOARD_SW3_GPIO_PIN, kGPIO_InterruptFallingEdge);

    GPIO_SetPinInterruptConfig(GPIOC, 6U, kGPIO_InterruptFallingEdge);

#else
    PORT_SetPinInterruptConfig(BOARD_SW3_PORT, BOARD_SW3_GPIO_PIN, kPORT_InterruptFallingEdge);


    PORT_SetPinInterruptConfig(PORTC, 6U, kPORT_InterruptFallingEdge);


#endif


    PORT_SetPinConfig(PORTA, 4U, &pcr_port_config);  // PTA4

	PORT_SetPinConfig(PORTC, 6U, &pcr_port_config);  // PTC6




	GPIO_PinInit(GPIOA, 4U, &gpio_input_config);

	GPIO_PinInit(GPIOC, 6U, &gpio_input_config);


	  NVIC_SetPriority(BOARD_SW3_IRQ, 6);
	     NVIC_SetPriority(BOARD_SW2_IRQ, 6);

    EnableIRQ(PORTA_IRQn);

    EnableIRQ(PORTC_IRQn);
     NVIC_SetPriority(BOARD_SW3_IRQ, 6);
     NVIC_SetPriority(BOARD_SW2_IRQ, 6);


}



void allOFF(){
	GPIO_PortSet(GPIOB, 1u << RED);
	GPIO_PortSet(GPIOE, 1u << GREEN);
	GPIO_PortSet(GPIOB, 1u << BLUE);
}

void whiteToggle(){

    GPIO_PortToggle(GPIOB, 1u << RED);
    GPIO_PortToggle(GPIOE, 1u << GREEN);
    GPIO_PortToggle(GPIOB, 1u << BLUE);
}

void redToggle(){
	  GPIO_PortToggle(GPIOB, 1u << RED);
}

void greenToggle(){
	GPIO_PortToggle(GPIOE, 1u << GREEN);
}

void blueToggle(){
	GPIO_PortToggle(GPIOB, 1u << BLUE);

}

void yellowToggle(){

    GPIO_PortToggle(GPIOB, 1u << RED);
    GPIO_PortToggle(GPIOE, 1u << GREEN);
}


void cianToggle(){
	GPIO_PortToggle(GPIOE, 1u << GREEN);
	GPIO_PortToggle(GPIOB, 1u << BLUE);

}


void magentaToggle(){
	 GPIO_PortToggle(GPIOB, 1u << RED);
	 GPIO_PortToggle(GPIOB, 1u << BLUE);

}







