#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "tusb.h"

#define UART_ID uart0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define BAUD_RATE 115200
#define LED_PIN 25
#define MAX_LINE_LENGTH 128

int main() {
    stdio_init_all();

    uart_init(UART_ID, BAUD_RATE);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UART_ID, false, false);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    while (!stdio_usb_connected()) sleep_ms(100);

    printf("\n=== Terminal Pico1 ===\n");
    printf("Escribi comandos y presiona ENTER para enviarlos por UART0 (115200 8N1)\n");
    printf("Pines: TX=%d, RX=%d\n\n", UART_TX_PIN, UART_RX_PIN);

    char line[MAX_LINE_LENGTH];
    char rx_buf[MAX_LINE_LENGTH];
    size_t line_pos = 0;
    size_t rx_pos = 0;

    while (true) {
        printf(">> ");
        fflush(stdout);

        // Lectura manual con eco
        line_pos = 0;
        while (true) {
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) {
                // Procesar UART RX en paralelo
                if (uart_is_readable(UART_ID)) {
                    char c = uart_getc(UART_ID);
                    if (c == '\r') continue;
                    if (c == '\n' || rx_pos >= sizeof(rx_buf) - 1) {
                        rx_buf[rx_pos] = '\0';
                        if (rx_pos > 0) {
                            printf("\n[RX] \"%s\"\n", rx_buf);
                            rx_pos = 0;
                            printf(">> ");  // reimprimir prompt
                            fflush(stdout);
                        }
                    } else {
                        rx_buf[rx_pos++] = c;
                    }
                }
                tight_loop_contents();
                continue;
            }

            // Manejar ENTER
            if (ch == '\r' || ch == '\n') {
                putchar('\n');
                line[line_pos] = '\0';
                break;
            }

            // Backspace
            if (ch == 8 || ch == 127) {
                if (line_pos > 0) {
                    line_pos--;
                    printf("\b \b");
                    fflush(stdout);
                }
                continue;
            }

            // Almacenar y hacer eco
            if (line_pos < MAX_LINE_LENGTH - 1) {
                line[line_pos++] = ch;
                putchar(ch);
                fflush(stdout);
            }
        }

        if (line_pos == 0) continue;

        // Enviar línea por UART
        uart_write_blocking(UART_ID, (uint8_t*)line, line_pos);
        uart_putc_raw(UART_ID, '\n');

        gpio_put(LED_PIN, 1);
        sleep_ms(60);
        gpio_put(LED_PIN, 0);

        printf("[TX] \"%s\"\n", line);
    }
}
