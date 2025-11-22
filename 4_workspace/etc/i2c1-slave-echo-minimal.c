#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define SDA_PIN 2
#define SCL_PIN 3
#define SLAVE_ADDR 0x20
#define BUF_SIZE 128

static char rx_buf[BUF_SIZE];
static int rx_idx = 0;

static char reply[BUF_SIZE];
static size_t reply_len = 0;
static size_t tx_idx = 0;

void i2c_slave_irq_handler(void) {
    i2c_inst_t *i2c = i2c1;

    // MASTER → SLAVE (WRITE)
    while (i2c_get_read_available(i2c)) {
        uint8_t c = i2c_read_byte_raw(i2c);

        if (c == '\n' || c == '\r') {
            rx_buf[rx_idx] = 0;

            printf("[SLAVE RX] %s\n", rx_buf);

            // Preparo respuesta
            snprintf(reply, BUF_SIZE, "ECHO: %s\n", rx_buf);
            reply_len = strlen(reply);
            tx_idx = 0;
            rx_idx = 0;
        }
        else if (rx_idx < BUF_SIZE - 1) {
            rx_buf[rx_idx++] = c;
        }
    }

    // SLAVE → MASTER (READ)
    while (i2c_get_write_available(i2c)) {
        if (reply_len == 0) {
            i2c_write_byte_raw(i2c, 0);
            continue;
        }

        i2c_write_byte_raw(i2c, reply[tx_idx++]);

        if (tx_idx >= reply_len)
            tx_idx = 0;
    }
}

int main() {
    stdio_init_all();
    sleep_ms(1000);

    printf("I2C SLAVE ECHO READY\n");

    i2c_init(i2c1, 100000);

    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    i2c_set_slave_mode(i2c1, true, SLAVE_ADDR);

    irq_set_exclusive_handler(I2C1_IRQ, i2c1_slave_irq_handler);
    irq_set_enabled(I2C1_IRQ, true);

    while (1)
        tight_loop_contents();
}
