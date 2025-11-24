#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/i2c_slave.h"   // <== librería i2c_slave del SDK

#define UART_ID    uart0
#define UART_TXPIN 0
#define UART_RXPIN 1

#define SDA_PIN    4    // I2C0 SDA
#define SCL_PIN    5    // I2C0 SCL
#define SLAVE_ADDR 0x20

#define BUF_SIZE   128

static char   rx_buf[BUF_SIZE];
static size_t rx_idx = 0;

static char   reply[BUF_SIZE];
static size_t reply_len = 0;
static size_t tx_idx = 0;

static bool led_state = false;

// Handler llamado DESDE la ISR del I2C, vía pico_i2c_slave
static void ISR_i2c_slave(i2c_inst_t *i2c, i2c_slave_event_t event)
{
    switch (event) {

    case I2C_SLAVE_RECEIVE:
        // El master nos está escribiendo datos
        while (i2c_get_read_available(i2c)) {
            uint8_t c = i2c_read_byte_raw(i2c);

            if (c == '\n' || c == '\r') {
                if (rx_idx > 0) {
                    rx_buf[rx_idx] = 0;

                    // Debug por UART
                    printf("[SLAVE RX] %s\n", rx_buf);

                    // Preparo respuesta
                    snprintf(reply, BUF_SIZE, "ECHO: %s\n", rx_buf);
                    reply_len = strlen(reply);
                    tx_idx    = 0;
                    rx_idx    = 0;
                }
            } else if (rx_idx < BUF_SIZE - 1) {
                rx_buf[rx_idx++] = (char)c;
            }
        }
        break;

    case I2C_SLAVE_REQUEST:
        // El master está leyendo de nosotros
        while (i2c_get_write_available(i2c)) {
            uint8_t c = 0;

            if (tx_idx < reply_len) {
                c = (uint8_t) reply[tx_idx++];
            } else {
                // Si no hay nada más que mandar, mandamos 0
                c = 0;
            }

            i2c_write_byte_raw(i2c, c);
        }
        break;

    case I2C_SLAVE_FINISH:
        // Se terminó la transacción (STOP). Acá no hace falta hacer nada,
        // pero podrías resetear índices si lo quisieras.
        break;
    }

// #ifdef PICO_DEFAULT_LED_PIN
//     // Toggle LED para ver actividad I2C
//     led_state = !led_state;
//     gpio_put(PICO_DEFAULT_LED_PIN, led_state);
// #endif
}

bool heartbeat_callback(repeating_timer_t *t) {
    static bool state = 0;
    gpio_put(PICO_DEFAULT_LED_PIN, state ^= 1);
    return true; // seguir llamando
}

int main(void)
{
    // NO usar stdio_init_all(): rompe el I2C0 slave por USB-CDC
    stdio_uart_init_full(UART_ID, 115200, UART_TXPIN, UART_RXPIN);
    sleep_ms(500);
    printf("Pico2 I2C0 SLAVE echo + UART0 debug\n");

#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif

    repeating_timer_t timer;
    add_repeating_timer_ms(500, heartbeat_callback, NULL, &timer);

    // ---- Init I2C0 en modo master (velocidad) ----
    // pico_i2c_slave se encarga después de pasarlo a slave
    i2c_init(i2c0, 100000);

    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    printf("SDA FUNC=%d SCL FUNC=%d\n",
           gpio_get_function(SDA_PIN),
           gpio_get_function(SCL_PIN));

    // ---- Pasar I2C0 a modo SLAVE usando la librería ----
    i2c_slave_init(i2c0, SLAVE_ADDR, ISR_i2c_slave);

    printf("I2C0 SLAVE listo en addr 0x%02X\n", SLAVE_ADDR);
    printf("Esperando comandos desde master...\n");

    while (1) {
        tight_loop_contents();
    }
}
