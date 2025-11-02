#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "tusb.h"

#define UART_ID uart0
#define UART_TX_PIN 0   // GP0 = pin físico 1
#define UART_RX_PIN 1   // GP1 = pin físico 2
#define BAUD_RATE 115200

#define MAX_LINE_LENGTH 128
#define LED_PIN 25  // LED integrado en Pico

int main() {
    stdio_init_all();  // Inicializa stdio USB
    uart_init(UART_ID, BAUD_RATE);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UART_ID, false, false);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    // LED de estado
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    while (!stdio_usb_connected()) sleep_ms(100);

    printf("\n=== Terminal Pico1 ===\n");
    printf("Escribí comandos y presioná ENTER para enviarlos por UART0 (115200 8N1)\n");
    printf("Pines: TX=%d (pin 1), RX=%d (pin 2)\n\n", UART_TX_PIN, UART_RX_PIN);

    char line[MAX_LINE_LENGTH];

    while (true) {
        printf(">> ");
        fflush(stdout);  // Asegura que se muestre el prompt

        // Lee línea completa desde USB
        if (fgets(line, sizeof(line), stdin) == NULL) {
            sleep_ms(10);
            continue;
        }

        // Quita salto de línea final
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        if (len == 0)
            continue;

        // Enviar línea completa por UART0
        uart_write_blocking(UART_ID, (const uint8_t *)line, len);
        uart_putc_raw(UART_ID, '\n');

        // Blink LED para indicar envío
        gpio_put(LED_PIN, 1);
        sleep_ms(80);
        gpio_put(LED_PIN, 0);

        printf("[OK] Enviado: \"%s\"\n", line);
    }
}
