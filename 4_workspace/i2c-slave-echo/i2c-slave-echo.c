#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/i2c.h"

#define UART_ID    uart0
#define UART_TXPIN 0
#define UART_RXPIN 1

#define SDA_PIN    4    // I2C0 SDA
#define SCL_PIN    5    // I2C0 SCL
#define SLAVE_ADDR 0x20

#define BUF_SIZE   128

static char rx_buf[BUF_SIZE];
static int  rx_idx = 0;

static char reply[BUF_SIZE];
static size_t reply_len = 0;
static size_t tx_idx = 0;

static bool led_state = false;

void i2c_slave_irq_handler(void) {
    i2c_inst_t *i2c = i2c0;

    // MASTER → SLAVE (WRITE)
    while (i2c_get_read_available(i2c)) {
        uint8_t c = i2c_read_byte_raw(i2c);

        if (c == '\n' || c == '\r') {
            if (rx_idx > 0) {
                rx_buf[rx_idx] = 0;

                // Debug: mostrar lo recibido
                printf("[SLAVE RX] %s\n", rx_buf);

                // Preparo respuesta
                snprintf(reply, BUF_SIZE, "ECHO: %s\n", rx_buf);
                reply_len = strlen(reply);
                tx_idx = 0;
                rx_idx = 0;
            }
        }
        else if (rx_idx < BUF_SIZE - 1) {
            rx_buf[rx_idx++] = (char)c;
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
            tx_idx = 0;   // opcionalmente podés dejarlo en reply_len para enviar solo una vez
    }

    // Toggle LED para ver actividad I2C
#ifdef PICO_DEFAULT_LED_PIN
    led_state = !led_state;
    gpio_put(PICO_DEFAULT_LED_PIN, led_state);
#endif
}

int main() {
    // ⚠ NO usar stdio_init_all(): rompe el I2C0 slave por USB-CDC
    stdio_uart_init_full(UART_ID, 115200, UART_TXPIN, UART_RXPIN);
    sleep_ms(500);
    printf("Pico2 I2C0 SLAVE echo + UART0 debug\n");

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif

    // ---- Init I2C0 como SLAVE ----
    i2c_init(i2c0, 100000);

    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    printf("SDA FUNC=%d SCL FUNC=%d\n",
           gpio_get_function(SDA_PIN),
           gpio_get_function(SCL_PIN));

    i2c_set_slave_mode(i2c0, true, SLAVE_ADDR);

    irq_set_exclusive_handler(I2C0_IRQ, i2c_slave_irq_handler);
    irq_set_enabled(I2C0_IRQ, true);

    printf("I2C0 SLAVE listo en addr 0x%02X\n", SLAVE_ADDR);
    printf("Esperando comandos desde master...\n");

    while (1) {
        tight_loop_contents();
    }
}
