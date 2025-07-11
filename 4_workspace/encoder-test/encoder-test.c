#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"

#define LED_PIN 25
#define LED_ON 	1
#define LED_OFF	0

#define ENC_A_PIN   14
#define ENC_B_PIN   15
#define ENC_PULS_PIN 13

typedef struct {
    char dir;
    uint8_t get;
} giro_t;

giro_t giro = {'D',0};

void irq_encoder_a_down()
{
    sleep_ms(20);
    // Si el pin B esta en 1, el giro es horario
    if(gpio_get(ENC_B_PIN)){
        giro.dir = 'D';
        giro.get = 1;
    }
    // Si el pin B esta en 0, el giro es antihorario
    else {
        giro.dir = 'I';
        giro.get = 1;
    }
    gpio_put(LED_PIN, LED_ON);
    sleep_ms(50);
    gpio_put(LED_PIN, LED_OFF);
}

void irq_encoder_a_up()
{
    sleep_ms(20);
    // Si el pin B esta en 0, el giro es horario
    if(!gpio_get(ENC_B_PIN)){
        giro.dir = 'D';
        giro.get = 1;
    }
    // Si el pin B esta en 1, el giro es antihorario
    else {
        giro.dir = 'I';
        giro.get = 1;
    }
    gpio_put(LED_PIN, LED_ON);
    sleep_ms(50);
    gpio_put(LED_PIN, LED_OFF);
}

void irq_encoder_b_down()
{
    sleep_ms(20);
    // Si el pin A esta en 0, el giro es horario
    if(!gpio_get(ENC_A_PIN)){
        giro.dir = 'D';
        giro.get = 1;
    }
    // Si el pin A esta en 1, el giro es antihorario
    else {
        giro.dir = 'I';
        giro.get = 1;
    }
    gpio_put(LED_PIN, LED_ON);
    sleep_ms(50);
    gpio_put(LED_PIN, LED_OFF);
}

void irq_encoder_b_up()
{
    sleep_ms(20);
    // Si el pin A esta en 1, el giro es horario
    if(gpio_get(ENC_A_PIN)){
        giro.dir = 'D';
        giro.get = 1;
    }
    // Si el pin A esta en 0, el giro es antihorario
    else {
        giro.dir = 'I';
        giro.get = 1;
    }
    gpio_put(LED_PIN, LED_ON);
    sleep_ms(50);
    gpio_put(LED_PIN, LED_OFF);
}

int main()
{
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, true);
    gpio_put(LED_PIN, LED_OFF);

    // INIT GPIO INPUT PINS
    gpio_init(ENC_A_PIN);
    gpio_init(ENC_B_PIN);
    // GPIO como entradas
    gpio_set_dir(ENC_A_PIN, false);
    gpio_set_dir(ENC_B_PIN, false);
    // Set pullups
    gpio_set_pulls(ENC_A_PIN, true, false);
    gpio_set_pulls(ENC_B_PIN, true, false);
    // Habilito las IRQ
    gpio_set_irq_enabled_with_callback(ENC_A_PIN, GPIO_IRQ_EDGE_FALL, true, &irq_encoder_a_down);
    gpio_set_irq_enabled_with_callback(ENC_A_PIN, GPIO_IRQ_EDGE_RISE, true, &irq_encoder_a_up);
    gpio_set_irq_enabled_with_callback(ENC_B_PIN, GPIO_IRQ_EDGE_FALL, true, &irq_encoder_b_down);
    gpio_set_irq_enabled_with_callback(ENC_B_PIN, GPIO_IRQ_EDGE_RISE, true, &irq_encoder_b_up);

    while (true) {
        if(giro.get){
            giro.get = 0;
            printf("%c ", giro.dir);
        }
    }
}
