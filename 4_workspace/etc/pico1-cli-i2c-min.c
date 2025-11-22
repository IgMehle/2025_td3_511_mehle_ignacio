#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// ---- CONFIG ----
#define I2C_PORT        i2c0
#define I2C_SDA_PIN     4
#define I2C_SCL_PIN     5
#define SLAVE_ADDR      0x20   // Dirección del slave Pico 2

#define BUF_SIZE 128

int main() {
    stdio_init_all();
    sleep_ms(1500);
    printf("=== Pico1 I2C MASTER Test ===\n");
    printf("Escribe un comando y ENTER\n");
    printf("Ejemplo: get lux\n\n");

    // ---- Init I2C MASTER ----
    i2c_init(I2C_PORT, 100 * 1000);  // 100 kHz
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    char tx_buffer[BUF_SIZE];
    char rx_buffer[BUF_SIZE];

    while (true) {
        printf(">> ");
        fflush(stdout);

        // Leer línea por USB-CDC (terminal)
        int idx = 0;
        while (1) {
            int c = getchar_timeout_us(0);

            if (c != PICO_ERROR_TIMEOUT) {
                if (c == '\r' || c == '\n') {
                    tx_buffer[idx] = '\n';   // aseguramos terminar en \n
                    idx++;
                    tx_buffer[idx] = 0;
                    break;
                } else if (idx < BUF_SIZE - 2) {
                    tx_buffer[idx++] = (char)c;
                    putchar(c); // eco local
                }
            }
        }
        printf("\n");

        // ---- WRITE hacia el SLAVE ----
        int len = strlen(tx_buffer);
        int r = i2c_write_blocking(I2C_PORT, SLAVE_ADDR,
                                   (uint8_t*)tx_buffer, len, false);

        if (r < 0) {
            printf("[ERR] Fallo la escritura I2C\n");
            continue;
        }

        // ---- READ desde el SLAVE ----
        sleep_ms(20);  // pequeño delay para que el slave prepare el reply

        memset(rx_buffer, 0, sizeof(rx_buffer));
        int read_len = i2c_read_timeout_us(I2C_PORT, SLAVE_ADDR,
                                           (uint8_t*)rx_buffer,
                                           BUF_SIZE - 1,
                                           false,
                                           2000); // timeout 2ms

        if (read_len < 0) {
            printf("[ERR] No se recibió respuesta del slave\n");
        } else {
            rx_buffer[read_len] = 0;
            printf("[RX SLAVE] %s\n", rx_buffer);
        }

        sleep_ms(50);
    }
}