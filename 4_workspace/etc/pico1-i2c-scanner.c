#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5

int main() {
    stdio_init_all();
    sleep_ms(1500);

    printf("=== I2C Scanner CORRECTO ===\n");

    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    while (1) {
        printf("Escaneando...\n");
        for (uint8_t addr = 1; addr < 0x7F; addr++) {
            uint8_t dummy = 0x00;
            int res = i2c_write_blocking(I2C_PORT, addr, &dummy, 1, true);
            if (res == 1) {
                printf("  Encontrado dispositivo en 0x%02X\n", addr);
            }
        }
        printf("Fin de scan.\n\n");
        sleep_ms(2000);
    }
}
