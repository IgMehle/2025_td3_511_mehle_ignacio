#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <string.h>
#include <stdio.h>

#define I2C_SLAVE_ADDRESS   0x20
#define I2C0_SDA_PIN        4
#define I2C0_SCL_PIN        5

#define BUFSIZE 128

static char rx_line[BUFSIZE];
static int rx_index = 0;

static char reply_buffer[BUFSIZE] = "OK\n";
static size_t reply_len = 3;

void i2c0_irq_handler(void) {
    i2c_inst_t *i2c = i2c0;

    while (i2c_get_read_available(i2c)) {
        uint8_t c = i2c_read_byte_raw(i2c);

        // Acumulo ASCII
        if (c == '\n' || c == '\r') {
            rx_line[rx_index] = 0;
            printf("[I2C RX] %s\n", rx_line);

            // ---- Simulación de respuesta ----
            snprintf(reply_buffer, BUFSIZE, "Echo: %s\n", rx_line);
            reply_len = strlen(reply_buffer);
            // ---------------------------------

            rx_index = 0;
        } else if (rx_index < BUFSIZE - 1) {
            rx_line[rx_index++] = c;
        }
    }

    // Master pide datos → debo transmitir
    while (i2c_get_write_available(i2c)) {
        static size_t idx = 0;

        if (idx >= reply_len)
            idx = 0;

        i2c_write_byte_raw(i2c, reply_buffer[idx++]);
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);
    printf("I2C SLAVE Echo ready\n");

    // I2C0 como slave
    i2c_init(i2c0, 100000);

    gpio_set_function(I2C0_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C0_SDA_PIN);
    gpio_pull_up(I2C0_SCL_PIN);

    i2c_set_slave_mode(i2c0, true, I2C_SLAVE_ADDRESS);

    irq_set_exclusive_handler(I2C0_IRQ, i2c0_irq_handler);
    irq_set_enabled(I2C0_IRQ, true);

    while (1)
        tight_loop_contents();
}
