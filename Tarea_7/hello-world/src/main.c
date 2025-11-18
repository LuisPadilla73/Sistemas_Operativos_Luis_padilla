/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* Definiciones de LEDs para K64F */
#define LED_RED_NODE    DT_ALIAS(led0)
#define LED_GREEN_NODE  DT_ALIAS(led1)

/* Definición del botón para K64F */
#define BUTTON_NODE     DT_ALIAS(sw0)

/* Verificar que los dispositivos existen */
#if !DT_NODE_HAS_STATUS_OKAY(LED_RED_NODE)
#error "LED rojo no disponible"
#endif

#if !DT_NODE_HAS_STATUS_OKAY(LED_GREEN_NODE)
#error "LED verde no disponible"
#endif

#if !DT_NODE_HAS_STATUS_OKAY(BUTTON_NODE)
#error "Botón no disponible"
#endif

/* Estructuras de GPIO */
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

/* Variable compartida para controlar qué LED debe parpadear */
static volatile bool toggle_red_led = true;

/* Mutex para proteger el acceso a la variable compartida */
K_MUTEX_DEFINE(led_mutex);

/* Definición de stacks y prioridades para los threads */
#define STACKSIZE 1024
#define PRIORITY_TOGGLE 7
#define PRIORITY_BUTTON 7

/* Declaración de threads estáticos */
K_THREAD_STACK_DEFINE(toggle_stack, STACKSIZE);
K_THREAD_STACK_DEFINE(button_stack, STACKSIZE);

static struct k_thread toggle_thread_data;
static struct k_thread button_thread_data;

/* Thread para hacer toggle del LED */
void toggle_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    bool local_toggle_red;

    printk("Thread de toggle iniciado\n");

    while (1) {
        /* Leer la variable compartida de forma segura */
        k_mutex_lock(&led_mutex, K_FOREVER);
        local_toggle_red = toggle_red_led;
        k_mutex_unlock(&led_mutex);

        /* Toggle del LED correspondiente */
        if (local_toggle_red) {
            gpio_pin_toggle_dt(&led_red);
            /* Asegurar que el otro LED está apagado */
            gpio_pin_set_dt(&led_green, 0);
        } else {
            gpio_pin_toggle_dt(&led_green);
            /* Asegurar que el otro LED está apagado */
            gpio_pin_set_dt(&led_red, 0);
        }

        /* Esperar 1 segundo */
        k_msleep(1000);
    }
}

/* Thread para leer el estado del botón */
void button_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    bool last_button_state = false;
    bool current_button_state;

    printk("Thread de botón iniciado\n");

    while (1) {
        /* Leer el estado actual del botón */
        current_button_state = gpio_pin_get_dt(&button);

        /* Detectar flanco de bajada (presión del botón) */
        if (last_button_state && !current_button_state) {
            /* Cambiar el LED que debe parpadear */
            k_mutex_lock(&led_mutex, K_FOREVER);
            toggle_red_led = !toggle_red_led;
            k_mutex_unlock(&led_mutex);

            printk("Botón presionado - Cambiando a LED %s\n", 
                   toggle_red_led ? "ROJO" : "VERDE");
        }

        last_button_state = current_button_state;

        /* Polling cada 50ms para debouncing */
        k_msleep(50);
    }
}

int main(void)
{
    int ret;

    printk("Hello world from %s\n", CONFIG_BOARD_TARGET);
    printk("Inicializando sistema de LEDs y botón...\n");

    /* Inicializar LED rojo */
    if (!gpio_is_ready_dt(&led_red)) {
        printk("Error: LED rojo no está listo\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Error configurando LED rojo\n");
        return ret;
    }
    printk("LED rojo inicializado\n");

    /* Inicializar LED verde */
    if (!gpio_is_ready_dt(&led_green)) {
        printk("Error: LED verde no está listo\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        printk("Error configurando LED verde\n");
        return ret;
    }
    printk("LED verde inicializado\n");

    /* Inicializar botón */
    if (!gpio_is_ready_dt(&button)) {
        printk("Error: Botón no está listo\n");
        return -1;
    }

    ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret < 0) {
        printk("Error configurando botón\n");
        return ret;
    }
    printk("Botón inicializado\n");

    /* Crear thread de toggle estático */
    k_thread_create(&toggle_thread_data, toggle_stack,
                    K_THREAD_STACK_SIZEOF(toggle_stack),
                    toggle_thread,
                    NULL, NULL, NULL,
                    PRIORITY_TOGGLE, 0, K_NO_WAIT);
    k_thread_name_set(&toggle_thread_data, "toggle");

    /* Crear thread de botón estático */
    k_thread_create(&button_thread_data, button_stack,
                    K_THREAD_STACK_SIZEOF(button_stack),
                    button_thread,
                    NULL, NULL, NULL,
                    PRIORITY_BUTTON, 0, K_NO_WAIT);
    k_thread_name_set(&button_thread_data, "button");

    printk("Threads creados. Sistema en funcionamiento.\n");
    printk("Presione el botón para alternar entre LEDs\n");

    return 0;
}
