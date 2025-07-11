#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "at24c32/at24c32.h"

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

    int resp;
    uint8_t stat;
    uint8_t data[8];
    data[0] = 'H';
    data[1] = 'O';
    data[2] = 'L';
    data[3] = 'A';
    data[4] = 'p';
    data[5] = 'i';
    data[6] = 'c';
    data[7] = 'o';
    uint16_t eeprom_address = 0x0000;
    uint8_t bytes[16];

    // uint8_t len = 4 + 2;
    // // Frame = Address + Data
    // uint8_t frame[len];
    // // Desempaqueto la direccion de memoria a la que escribir
    // frame[0] = (uint8_t)((eeprom_address >> 8) & 0x00FF);
    // frame[1] = (uint8_t)(eeprom_address & 0x00FF);
    // // Cargo los datos en la trama
    // //for(uint8_t i = 0; i<bytes; i++) frame[i+2] = data[i];
    // frame[2] = data[0];
    // frame[3] = data[1];
    // frame[4] = data[2];
    // frame[5] = data[3];
    // // Envio bytes a escribir
    // resp = i2c_write_blocking(i2c0, AT24C32_ADDR, frame, len, false);
    // // Tiempo de escritura en eeprom
    // sleep_ms(10);
    // if (resp != len){
    //     while(true){
    //         printf("Error de escritura.\n");
    //         sleep_ms(1000);
    //     }
    // }
    stat = eeprom_write(data, eeprom_address, 8);
    if (stat){
        while(true){
            printf("Error de escritura.\n");
            sleep_ms(1000);
        }
    }

    while (true) {
        stat = eeprom_read(bytes, 0x0000, 16);
        for (uint8_t i = 0; i < 16; i++) printf("%c", bytes[i]);
        printf("\n");
        gpio_put(LED_PIN, LED_ON);
        sleep_ms(500);
        gpio_put(LED_PIN, LED_OFF);
        sleep_ms(500);
    }
}
