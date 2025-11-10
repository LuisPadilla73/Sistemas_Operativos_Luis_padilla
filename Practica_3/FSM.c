/*
 * FSM.c
 *
 *  Created on: 9 nov 2025
 *      Author: luisg
 */

#include "FSM.h"

static QueueHandle_t sZoomMailbox  = NULL;
static QueueHandle_t sModeMailbox  = NULL;
static TimerHandle_t sDebounceA    = NULL; /* PTA4 -> SW3 (MODO) */
static TimerHandle_t sDebounceC    = NULL; /* PTC6 -> SW2 (ZOOM) */
static volatile bool sBusyA        = false;
static volatile bool sBusyC        = false;

static const uint8_t sZoomSteps[]  = {1, 5, 10};
static uint8_t sZoomIdx            = 0;

static void prv_init_buttons(void);
static void prv_post_initial_values(void);
static void prv_debounceA_cb(TimerHandle_t xTimer); /* SW3 (MODO)  */
static void prv_debounceC_cb(TimerHandle_t xTimer); /* SW2 (ZOOM)  */
static void prv_publish_zoom_next(void);
static void prv_publish_mode_next(void);

static inline void prv_zoom_overwrite(uint8_t z)
{
    if (sZoomMailbox) { (void)xQueueOverwrite(sZoomMailbox, &z); }
}
static inline void prv_mode_overwrite(ui_mode_t m)
{
    if (sModeMailbox) { (void)xQueueOverwrite(sModeMailbox, &m); }
}

bool FSM_Init(void)
{
    sZoomMailbox = xQueueCreate(1, sizeof(uint8_t));
    sModeMailbox = xQueueCreate(1, sizeof(ui_mode_t));
    if (!sZoomMailbox || !sModeMailbox) return false;

    sDebounceA = xTimerCreate("debA", pdMS_TO_TICKS(60), pdFALSE, NULL, prv_debounceA_cb);
    sDebounceC = xTimerCreate("debC", pdMS_TO_TICKS(60), pdFALSE, NULL, prv_debounceC_cb);
    if (!sDebounceA || !sDebounceC) return false;

    prv_init_buttons();

    prv_post_initial_values();

    return true;
}

QueueHandle_t FSM_GetZoomMailbox(void) { return sZoomMailbox; }
QueueHandle_t FSM_GetModeMailbox(void) { return sModeMailbox; }

void FSM_SetMode(ui_mode_t mode) { prv_mode_overwrite(mode); }

void FSM_SetZoom(uint8_t zoom)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(sZoomSteps)/sizeof(sZoomSteps[0])); i++)
    {
        if (sZoomSteps[i] == zoom) { sZoomIdx = i; break; }
    }
    prv_zoom_overwrite(sZoomSteps[sZoomIdx]);
}

static void prv_init_buttons(void)
{
    /* Clocks de puertos */
    CLOCK_EnableClock(kCLOCK_PortA);
    CLOCK_EnableClock(kCLOCK_PortC);

    /* GPIO input */
    gpio_pin_config_t in_cfg = {
        .pinDirection = kGPIO_DigitalInput,
        .outputLogic  = 0
    };
    GPIO_PinInit(UI_SW3_GPIO, UI_SW3_PIN, &in_cfg);
    GPIO_PinInit(UI_SW2_GPIO, UI_SW2_PIN, &in_cfg);

    const port_pin_config_t pcr = {
        .pullSelect            = kPORT_PullUp,
        .slewRate              = kPORT_FastSlewRate,
        .passiveFilterEnable   = kPORT_PassiveFilterEnable,
        .openDrainEnable       = kPORT_OpenDrainDisable,
        .driveStrength         = kPORT_LowDriveStrength,
        .mux                   = kPORT_MuxAsGpio,
        .lockRegister          = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(UI_SW3_PORT, UI_SW3_PIN, &pcr); /* PTA4 */
    PORT_SetPinConfig(UI_SW2_PORT, UI_SW2_PIN, &pcr); /* PTC6 */

    /* Flanco descendente */
    PORT_SetPinInterruptConfig(UI_SW3_PORT, UI_SW3_PIN, kPORT_InterruptFallingEdge);
    PORT_SetPinInterruptConfig(UI_SW2_PORT, UI_SW2_PIN, kPORT_InterruptFallingEdge);

    /* NVIC */
    NVIC_SetPriority(UI_SW3_IRQn, 3);
    NVIC_SetPriority(UI_SW2_IRQn, 3);
    EnableIRQ(UI_SW3_IRQn);
    EnableIRQ(UI_SW2_IRQn);
}

static void prv_post_initial_values(void)
{
    sZoomIdx = 0;
    prv_zoom_overwrite(sZoomSteps[sZoomIdx]);

    prv_mode_overwrite(UI_MODE_HR);
}


void PORTA_IRQHandler(void)
{
    uint32_t flags = PORT_GetPinsInterruptFlags(UI_SW3_PORT);

    if (flags & (1u << UI_SW3_PIN))
    {
        PORT_ClearPinsInterruptFlags(UI_SW3_PORT, (1u << UI_SW3_PIN));

        if (!sBusyA)
        {
            sBusyA = true;
            (void)xTimerStartFromISR(sDebounceA, NULL);
        }
    }
    __DSB(); __ISB();
}

void PORTC_IRQHandler(void)
{
    uint32_t flags = PORT_GetPinsInterruptFlags(UI_SW2_PORT);

    if (flags & (1u << UI_SW2_PIN))
    {
        PORT_ClearPinsInterruptFlags(UI_SW2_PORT, (1u << UI_SW2_PIN));

        if (!sBusyC)
        {
            sBusyC = true;
            (void)xTimerStartFromISR(sDebounceC, NULL);
        }
    }
    __DSB(); __ISB();
}

static void prv_debounceA_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    prv_publish_mode_next();
    sBusyA = false;
}

static void prv_debounceC_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    prv_publish_zoom_next();
    sBusyC = false;
}


static void prv_publish_zoom_next(void)
{
    sZoomIdx = (uint8_t)((sZoomIdx + 1u) % (uint8_t)(sizeof(sZoomSteps)/sizeof(sZoomSteps[0])));
    prv_zoom_overwrite(sZoomSteps[sZoomIdx]);
}

static void prv_publish_mode_next(void)
{
    ui_mode_t mode = UI_MODE_HR;
    (void)xQueuePeek(sModeMailbox, &mode, 0);

    if (mode == UI_MODE_HR)        mode = UI_MODE_TEMP;
    else if (mode == UI_MODE_TEMP) mode = UI_MODE_BOTH;
    else                           mode = UI_MODE_HR;

    prv_mode_overwrite(mode);
}

