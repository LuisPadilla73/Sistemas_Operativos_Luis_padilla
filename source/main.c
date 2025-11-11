/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* NXP / Board */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_port.h"
#include "fsl_gpio.h"
#include "fsl_adc16.h"

/* Pantalla / SPI / Draw */
#include "LCD_nokia.h"
#include "LCD_nokia_images.h"
#include "SPI.h"
#include "nokia_draw.h"

/* === Driver ADC integrado === */
#include "ADC.h"
#include "GPIO_D.h"
/* =================== Definiciones =================== */
#define hello_task_PRIORITY    (configMAX_PRIORITIES - 1)
#define ADC_PRIORITY      (configMAX_PRIORITIES - 2)
#define GrapNumb_PRIORITY      (configMAX_PRIORITIES - 3)
#define X_INCREMENT_DEFAULT    1
#define INIT_DISPLAY both

/* ----- Tipos propios (como en tu P3.txt) ----- */
typedef struct{
    uint8_t x;
    uint8_t y;
} point_str;

enum {
    heart = 0,
    temp,
    both
};
/* =================== Recursos FreeRTOS =================== */
static TimerHandle_t SendFBTimer;

static QueueHandle_t TimeScaleMailbox;
static QueueHandle_t CurrentIDmailbox;
static QueueHandle_t AdcConversionQueue;
static QueueHandle_t PointQueueHR;
static QueueHandle_t PointQueueTEMP;

/* Colas numéricas nuevas, formateables para display */
static QueueHandle_t NumberQueueHR;     /* HR en centésimas de mV (0..300) */
static QueueHandle_t NumberQueueTEMP;   /* TEMP en décimas de °C (340..400) */

/* =================== Prototipos =================== */
static void LCDprint_thread(void *pvParameters);
static void GraphProcess_thread(void *pvParameters);
static void NumberProcess_thread(void *pvParameters);

/* Nueva: reenvía del driver ADC -> AdcConversionQueue (dos veces) */
static void AdcForwarder_task(void *pvParameters);

/* Helpers de impresión formateada */
static void LCD_PrintCentimV(uint8_t x, uint8_t y, uint16_t centimV);
static void LCD_PrintDeciC(uint8_t x, uint8_t y, uint16_t deciC);

/* Init de subsistemas */
static void ScreenInit(void);
static void SWInit(void);

/* Callbacks de timers */
static void SendFBCallback(TimerHandle_t ARHandle);

///* ISR botón */
//void BOARD_SW3_IRQ_HANDLER(void);
//void BOARD_SW2_IRQ_HANDLER(void);

/* =================== Código =================== */

static void ScreenInit(void)
{
    SPI_config();
    LCD_nokia_init();
    LCD_nokia_clear();
}




void BOARD_SW3_IRQ_HANDLER(void) {
    /* Clear the interrupt flag for PTA4 using SDK function */

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        static uint8_t x_increment = 1;
#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT)
    GPIO_GpioClearInterruptFlags(BOARD_SW3_GPIO, 1U << BOARD_SW3_GPIO_PIN);
#else

        /* Mejor sería:
           static uint8_t steps[] = {1,5,10};
           static uint8_t idx = 0;
           x_increment = steps[idx];
           idx = (idx + 1) % 3;
        */
        if (x_increment < 10) {
            x_increment += 5;
        } else {
            x_increment = 1;
        }

        xQueueOverwriteFromISR(TimeScaleMailbox, &x_increment, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken)
        GPIO_PortClearInterruptFlags(GPIOA, (1U << 4));
#endif



    /* Change state of button */
    SDK_ISR_EXIT_BARRIER;
}



void BOARD_SW2_IRQ_HANDLER(void) {
	   BaseType_t xHPW = pdFALSE;
	static uint8_t id = INIT_DISPLAY;  // 0
#if (defined(FSL_FEATURE_PORT_HAS_NO_INTERRUPT) && FSL_FEATURE_PORT_HAS_NO_INTERRUPT)
    GPIO_GpioClearInterruptFlags(BOARD_SW2_GPIO, 1U << BOARD_SW2_GPIO_PIN);
#else

	if (GPIO_PinRead(GPIOC, 6) == 0) { //STOP



	    id = (uint8_t)((id + 1) % 3);  // heart->temp->both->heart...
	    xQueueOverwriteFromISR(CurrentIDmailbox, &id, &xHPW);
	    portYIELD_FROM_ISR(xHPW)


	}
	GPIO_PortClearInterruptFlags(GPIOC, (1U << 6));
#endif

    SDK_ISR_EXIT_BARRIER;
}


static void SendFBCallback(TimerHandle_t ARHandle)
{
    (void)ARHandle;
    LCD_nokia_sent_FrameBuffer();
}

/* =================== Helpers de impresión =================== */
/* HR: centésimas de mV -> "A.BC mv" (ej.: 152 => "1.52 mv") */
static void LCD_PrintCentimV(uint8_t x, uint8_t y, uint16_t centimV)
{
    uint8_t A = (uint8_t)((centimV / 100U) % 10U);
    uint8_t B = (uint8_t)((centimV / 10U)  % 10U);
    uint8_t C = (uint8_t)( centimV         % 10U);
    PRINTF("%d%d.%d\n\r",A,B,C);
    /* Limpia zona (ancho aprox ~30 cols) */
    LCD_nokia_clear_range_FrameBuffer(x, y, 35);

    LCD_nokia_write_char_xy_FB(x +  0, y, (uint8_t)('0' + A));
    LCD_nokia_write_char_xy_FB(x +  5, y, (uint8_t)('0' + B));
    LCD_nokia_write_char_xy_FB(x + 10, y, '.');
    LCD_nokia_write_char_xy_FB(x + 15, y, (uint8_t)('0' + C));
    LCD_nokia_write_char_xy_FB(x + 20, y, ' ');
    LCD_nokia_write_string_xy_FB(x + 25, y, (uint8_t*)"mv", 2);
}

/* TEMP: décimas de °C -> "AA.B °C" (ej.: 365 => "36.5 °C") */
static void LCD_PrintDeciC(uint8_t x, uint8_t y, uint16_t deciC)
{
    uint16_t entero  = deciC / 10U;   /* 34..40 */
    uint8_t  decimal = deciC % 10U;   /* 0..9   */

    uint8_t tens = (uint8_t)((entero / 10U) % 10U);
    uint8_t ones = (uint8_t)( entero        % 10U);
    PRINTF("%d%d.%d\n\r",tens,ones,decimal);
    LCD_nokia_clear_range_FrameBuffer(x, y, 40);

    LCD_nokia_write_char_xy_FB(x +  0, y, (uint8_t)('0' + tens));
    LCD_nokia_write_char_xy_FB(x +  5, y, (uint8_t)('0' + ones));
    LCD_nokia_write_char_xy_FB(x + 10, y, '.');
    LCD_nokia_write_char_xy_FB(x + 15, y, (uint8_t)('0' + decimal));
    LCD_nokia_write_char_xy_FB(x + 20, y, ' ');
    /* Si tu font no tiene '°', puedes usar 'o' o espacio */
    // LCD_nokia_write_char_xy_FB(x + 25, y, 0xDF); /* A veces 0xDF se usa como '°' en fuentes personalizadas */
    LCD_nokia_write_char_xy_FB(x + 25, y, 'C');    /* “ C” es suficiente */
}

/* =================== main() =================== */
int main(void)
{






	uint8_t init_time_scale = X_INCREMENT_DEFAULT;
    uint8_t init_display = INIT_DISPLAY;

    /* Init HW base */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

    ScreenInit();
    GPIO_BOARD_INIT();



    /* ========= COLAS ========= */
    AdcConversionQueue = xQueueCreate(5, sizeof(adcConv_str));
    PointQueueHR      = xQueueCreate(1, sizeof(uint16_t));
    PointQueueTEMP    = xQueueCreate(1, sizeof(uint16_t));
    TimeScaleMailbox   = xQueueCreate(1, sizeof(uint8_t));
    CurrentIDmailbox = xQueueCreate(1, sizeof(uint8_t));
    /* NUEVAS colas de números formateables */
    NumberQueueHR      = xQueueCreate(1, sizeof(uint16_t));  /* centésimas de mV */
    NumberQueueTEMP    = xQueueCreate(1, sizeof(uint16_t));  /* décimas de °C   */

    /* ========= Timer de pantalla ========= */
    SendFBTimer = xTimerCreate(
        "WriteFB",
        pdMS_TO_TICKS(33),   /* 33 ms ~ 30 FPS */
        pdTRUE,
        0,
        SendFBCallback);

    /* ======= Driver ADC: su cola interna + timer 100ms ======= */
    ADC_Init(10);
    ADC_Start();

    /* ======= Arranque de timers ======= */
    xTimerStart(SendFBTimer, 0);

    /* Publica time-scale inicial */
    xQueueOverwrite(TimeScaleMailbox, &init_time_scale);
    xQueueOverwrite(CurrentIDmailbox, &init_display);

    /* ========= Tareas ========= */

    if (xTaskCreate(LCDprint_thread, "LCDprint_thread",
                    configMINIMAL_STACK_SIZE + 120, NULL, hello_task_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("LCDprint_thread creation failed!\r\n");
        while (1) {}
    }
    /* Forwarder del driver hacia tu cola original (dos envíos) */
     if (xTaskCreate(AdcForwarder_task, "AdcForwarder_task",
                     configMINIMAL_STACK_SIZE + 100, NULL, ADC_PRIORITY, NULL) != pdPASS)
     {
         PRINTF("AdcForwarder_task creation failed!\r\n");
         while (1) {}
     }

    if (xTaskCreate(GraphProcess_thread, "GraphProcess_thread",
                    configMINIMAL_STACK_SIZE + 120, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("GraphProcess_thread creation failed!\r\n");
        while (1) {}
    }

    if (xTaskCreate(NumberProcess_thread, "NumberProcess_thread",
                    configMINIMAL_STACK_SIZE + 120, NULL, GrapNumb_PRIORITY, NULL) != pdPASS)
    {
        PRINTF("NumberProcess_thread creation failed!\r\n");
        while (1) {}
    }



    vTaskStartScheduler();

    for (;;){}
}


/* =================== Tareas =================== */
/* =================== Tareas =================== */
static void LCDprint_thread(void *pvParameters)
{
    (void)pvParameters;

    point_str a_hr = {0,0}, b_hr = {0,0};
    point_str a_tp = {0,0}, b_tp = {0,0};

    uint16_t y_hr, y_tp;
    uint16_t hr_centimV, t_deciC;

    /* Conserva el último valor de escala si no hay nueva escritura */
    static uint8_t x_increment = X_INCREMENT_DEFAULT;

    uint8_t mode = INIT_DISPLAY;
    uint8_t last_mode = 0xFF; /* fuerza refresh inicial */

    for (;;)
    {
        /* 1) Lee (si hay) la escala de tiempo una sola vez por iteración */
        (void)xQueuePeek(TimeScaleMailbox, &x_increment, pdMS_TO_TICKS(15));

        /* 2) Observa el modo (sin consumir) y detecta cambio */
        (void)xQueuePeek(CurrentIDmailbox, &mode, pdMS_TO_TICKS(15));

        if (mode != last_mode)
        {
            LCD_nokia_clear();
            LCD_nokia_clear_range_FrameBuffer(0, 0, 252);
            LCD_nokia_clear_range_FrameBuffer(0,3,252);
            a_hr = (point_str){0,0}; b_hr = (point_str){0,0};
            a_tp = (point_str){0,0}; b_tp = (point_str){0,0};

        }

        /* 3) Impresión de números */


        /* 4) Trazos */
        if (mode == heart || mode == both)
        {
            if (xQueueReceive(PointQueueHR, &y_hr, pdMS_TO_TICKS(15)) == pdTRUE)
            {
                a_hr = b_hr;
                if (mode != both) {
                	y_hr = y_hr - 24U;
				}
                b_hr.y = y_hr;
                if ((uint8_t)(b_hr.x + x_increment) < 84) { b_hr.x += x_increment; }
                else { b_hr.x  = 0;
                if(mode !=both){
                	 LCD_nokia_clear_range_FrameBuffer(0,3,252);
                }else{
                	LCD_nokia_clear_range_FrameBuffer(0, 0, 252);}
                }
                drawline(a_hr.x, a_hr.y, b_hr.x, b_hr.y, 50);
            }
        }

        if (mode == temp || mode == both)
        {
            if (xQueueReceive(PointQueueTEMP, &y_tp, pdMS_TO_TICKS(15)) == pdTRUE)
            {
                a_tp = b_tp;

                b_tp.y = y_tp;

                if ((uint8_t)(b_tp.x + x_increment) < 84) { b_tp.x += x_increment; }
                else                                      { b_tp.x  = 0; LCD_nokia_clear_range_FrameBuffer(0,3,252);}
                drawline(a_tp.x, a_tp.y, b_tp.x, b_tp.y, 50);
            }
        }


        if (mode == heart || mode == both)
           {
               if (xQueueReceive(NumberQueueHR, &hr_centimV, pdMS_TO_TICKS(15)) == pdTRUE)
               {
                   LCD_PrintCentimV(0, 0, hr_centimV);
               }
           }

           if (mode == temp || mode == both)
           {
               if (xQueueReceive(NumberQueueTEMP, &t_deciC, pdMS_TO_TICKS(15)) == pdTRUE)
               {
                   LCD_PrintDeciC(0, (mode == both) ? 3 : 0, t_deciC);
               }
           }
           last_mode = mode;

        /* Cede CPU; el frame se envía por el timer a ~30 FPS */
        taskYIELD();
    }
}

static void GraphProcess_thread(void *pvParameters)
{
    (void)pvParameters;
    adcConv_str m;
    uint16_t y;

    for (;;)
    {
        if (xQueueReceive(AdcConversionQueue, &m, portMAX_DELAY) == pdTRUE)
        {
        	if (m.convSource == heart) {
        		y = (uint16_t)((m.data * 24U) / 4096U) + 24U; //de 24 a 27

        	    xQueueOverwrite(PointQueueHR, &y);
        	} else {
        		 y = (uint16_t)((m.data * 24U) / 4096U); // de 0 a 24

        	    xQueueOverwrite(PointQueueTEMP, &y);
        	}
            taskYIELD();
        }
    }
}
static void NumberProcess_thread(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str adcConvVal;
    uint16_t outValue;

    for (;;)
    {
        if (xQueueReceive(AdcConversionQueue, &adcConvVal, portMAX_DELAY) == pdTRUE)
        {
        	if (adcConvVal.convSource == 0) {
        	    outValue = (uint16_t)((adcConvVal.data * 300U) / 4095U);
        	    xQueueOverwrite(NumberQueueHR, &outValue);
        	} else {
        	    outValue = (uint16_t)(340U + ((adcConvVal.data * 60U) / 4095U));
        	    xQueueOverwrite(NumberQueueTEMP, &outValue);
        	}
            taskYIELD();
        }
    }
}

/* =================== Forwarder del driver a tus colas =================== */
static void AdcForwarder_task(void *pvParameters)
{
    (void)pvParameters;

    adcConv_str m;
    for (;;)
    {
        /* Leer del driver (ISR->cola interna del driver) */
        if (ADC_Receive(&m, portMAX_DELAY))
        {
            /* Reenviar a tu cola original, dos veces, como hacía tu ISR previa */
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);
            taskYIELD();
            xQueueSend(AdcConversionQueue, &m, portMAX_DELAY);
        }
    }
}
