/*
 * UART_SDK.h
 *
 *  Created on: 7 mar. 2025
 *      Author: Gabriel
 */


#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_uart.h"




#ifndef DRIVERS_UART_SDK_H_
#define DRIVERS_UART_SDK_H_

enum {
	INITSTATE = 0,
	RTC_OPTION,
	DAC_SIGNALS,
	CLEAR,
	INOPRERATIVE_STATE

};
/*******************************************************************************
 * Definitions
 ******************************************************************************/
/* UART instance and clock */
#define DEMO_UART            UART0
#define DEMO_UART_CLKSRC     UART0_CLK_SRC
#define DEMO_UART_CLK_FREQ   CLOCK_GetFreq(UART0_CLK_SRC)
#define DEMO_UART_IRQn       UART0_RX_TX_IRQn
#define DEMO_UART_IRQHandler UART0_RX_TX_IRQHandler

void UART_init();
void UART_show_option(uint8_t option);

void terminal_send(volatile uint8_t *string, uint8_t size);


#endif /* DRIVERS_UART_SDK_H_ */
