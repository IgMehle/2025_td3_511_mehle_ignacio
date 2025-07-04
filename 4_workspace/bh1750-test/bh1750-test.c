#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "bh1750/bh1750.h"

#define SDA_PIN 4
#define SCL_PIN 5
#define I2C_FREQ 100000

#define LED_PIN 25
#define LED_ON 	1
#define LED_OFF	0

int main()
{
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, true);
    gpio_put(LED_PIN, LED_OFF);

    // I2C INIT
    // I2C0 (DEFAULT) a 100khz
    i2c_init(i2c0, I2C_FREQ);
    // I2C0_SDA en GPIO4
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    // I2C0_SCL en GPIO5
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    // Pongo pullups a 3V3
    // gpio_pull_up(SDA_PIN);
    // gpio_pull_up(SCL_PIN);

    uint16_t medicion;
    bool stat;
    stat = bh1750_init();
    if(!stat){
        while(1){
            printf("Init error.../n");
            sleep_ms(1000);
        }
    } 
    sleep_ms(200);

    while (true) {
        medicion = bh1750_read();
        printf("Lux: %d\n", medicion);
        gpio_put(LED_PIN, LED_ON);
        sleep_ms(500);
        gpio_put(LED_PIN, LED_OFF);
        sleep_ms(500);
    }
}
