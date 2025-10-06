/*
 * UART_SDK.c
 *
 *  Created on: 7 mar. 2025
 *      Author: Gabriel
 */


/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "UART_SDK.h"

uint32_t * global_value;


uint32_t value;
uint8_t index = 0;
uint32_t valuesArray[] = {0,0};
uint8_t optionSelected = 0;
uint8_t selection[1] = {0};
volatile uint8_t * currentRXState = NULL;
volatile uint8_t * uartPrintFlag = NULL;


void terminal_send(volatile uint8_t *string, uint8_t size) {
	UART_WriteBlocking(DEMO_UART, string, size);
}


/*!
 * @brief Main function
 */
void UART_init(uint32_t*  value_to_main, uint8_t* stateVariable, uint8_t* printFlag){
	uart_config_t config;
	    BOARD_InitBootPins();
	    BOARD_InitBootClocks();

	    /*
	     * config.baudRate_Bps = 115200U;
	     * config.parityMode = kUART_ParityDisabled;
	     * config.stopBitCount = kUART_OneStopBit;
	     * config.txFifoWatermark = 0;
	     * config.rxFifoWatermark = 1;
	     * config.enableTx = false;
	     * config.enableRx = false;
	     */
	    UART_GetDefaultConfig(&config);
	    config.baudRate_Bps = BOARD_DEBUG_UART_BAUDRATE;
	    config.enableTx     = true;
	    config.enableRx     = true;

	    UART_Init(DEMO_UART, &config, DEMO_UART_CLK_FREQ);

	    /* Send g_tipString out. */

	    /* Enable RX interrupt. */



}

