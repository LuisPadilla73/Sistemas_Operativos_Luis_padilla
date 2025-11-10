/*
 * GPIO.c
 *
 *  Created on: 4 may. 2025
 *      Author: Gabriel
 */

#include "GPIO.h"



void GPIO_setOutputPins(void) {
	const gpio_pin_config_t gpio_output_config = {
	        kGPIO_DigitalOutput,  // Set as output
	        0                     // Initial value LOW
	    };





		GPIO_PinInit(GPIOC,0U, &gpio_output_config);
		GPIO_PinInit(GPIOD,1U, &gpio_output_config);
		GPIO_PinInit(GPIOC,2U, &gpio_output_config);
		GPIO_PinInit(GPIOC,3U, &gpio_output_config);

	 /* Init output LED GPIO. */
		  GPIO_PinInit(GPIOB, RED, &gpio_output_config);
		  GPIO_PinInit(GPIOB, BLUE, &gpio_output_config);
		  GPIO_PinInit(GPIOE, GREEN, &gpio_output_config);


	/* PORTB22 (pin 68) is configured as PTB22 */
	       PORT_SetPinMux(PORTB, RED, kPORT_MuxAsGpio);
	       PORT_SetPinMux(PORTB, BLUE, kPORT_MuxAsGpio);
	       PORT_SetPinMux(PORTE, GREEN, kPORT_MuxAsGpio);
	       allOFF();

	       PORT_SetPinMux(PORTA, 4U, kPORT_MuxAsGpio); //PTA4

	       PORT_SetPinMux(PORTC, 0U, kPORT_MuxAsGpio);
	       PORT_SetPinMux(PORTD, 1U, kPORT_MuxAsGpio);
	       PORT_SetPinMux(PORTC, 2U, kPORT_MuxAsGpio);
	       PORT_SetPinMux(PORTC, 3U, kPORT_MuxAsGpio);




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
	CLOCK_EnableClock(kCLOCK_PortD);


    PORT_SetPinConfig(PORTA, 4U, &pcr_port_config);  // PTA4  SW3
    PORT_SetPinConfig(PORTC, 6U, &pcr_port_config);  // PTC6 SW2

    PORT_SetPinConfig(PORTC, 10U, &pcr_port_config); // PTC10
	PORT_SetPinConfig(PORTC, 11U, &pcr_port_config); // PTC11
	PORT_SetPinConfig(PORTB, 11U, &pcr_port_config); // PTB11
	PORT_SetPinConfig(PORTD,0U, &pcr_port_config);




	GPIO_PinInit(GPIOA, 4U, &gpio_input_config);
	GPIO_PinInit(GPIOC, 6U, &gpio_input_config);

	GPIO_PinInit(GPIOC, 10U, &gpio_input_config); // PTC10 in
	GPIO_PinInit(GPIOC, 11U, &gpio_input_config); // PTC11 in
	GPIO_PinInit(GPIOB, 11U, &gpio_input_config); // PTB11 in
	GPIO_PinInit(GPIOD, 0U, &gpio_input_config); // PTD0 in




}


void GPIO_BOARD_INIT(void) {
    /* Init board hardware. */
    //BOARD_InitBootPins();
    BOARD_InitBootClocks();



    GPIO_setInputPins();
    GPIO_setOutputPins();

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

void redOFF(){
	  GPIO_PortSet(GPIOB, 1u << RED);
}


void redON(){
	  GPIO_PortClear(GPIOB, 1u << RED);
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








