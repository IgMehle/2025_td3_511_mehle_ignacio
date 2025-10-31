
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// Pines del encoder
#define ENCODER_A 14
#define ENCODER_B 15

#define LED 25
#define LED_ON 1
#define LED_OFF 0

// Variables para estado anterior
volatile uint8_t last_state = 0;

// Rutina de interrupción común para ambos pines
void encoder_isr(uint gpio, uint32_t events) {
    // Antirrebote
    sleep_ms(100);
    // Leer ambos estados
    bool a = gpio_get(ENCODER_A);
    bool b = gpio_get(ENCODER_B);
    uint8_t new_state = (a << 1) | b;

    // Combinaciones posibles
    if ((last_state == 0b00 && new_state == 0b01) ||
        (last_state == 0b01 && new_state == 0b11) ||
        (last_state == 0b11 && new_state == 0b10) ||
        (last_state == 0b10 && new_state == 0b00)) {
        printf("H");  // Horario
    } else if (
        (last_state == 0b00 && new_state == 0b10) ||
        (last_state == 0b10 && new_state == 0b11) ||
        (last_state == 0b11 && new_state == 0b01) ||
        (last_state == 0b01 && new_state == 0b00)) {
        printf("A");  // Antihorario
    }

    // gpio_put(LED, LED_ON);
    // sleep_ms(50);
    // gpio_put(LED, LED_OFF);
    last_state = new_state;
}

int main() {
    stdio_usb_init();  // Inicializa consola USB
    sleep_ms(1000);    // Espera a que el host USB esté listo

    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    gpio_put(LED, false);

    gpio_init(ENCODER_A);
    gpio_set_dir(ENCODER_A, GPIO_IN);
    gpio_pull_up(ENCODER_A);
    gpio_set_irq_enabled_with_callback(ENCODER_A, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &encoder_isr);

    gpio_init(ENCODER_B);
    gpio_set_dir(ENCODER_B, GPIO_IN);
    gpio_pull_up(ENCODER_B);
    gpio_set_irq_enabled(ENCODER_B, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    // Estado inicial
    last_state = (gpio_get(ENCODER_A) << 1) | gpio_get(ENCODER_B);

    while (true) {
        tight_loop_contents();  // Espera activa
    }
}
