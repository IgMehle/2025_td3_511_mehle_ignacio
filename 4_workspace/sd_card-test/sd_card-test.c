#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "sd_card.h"

int main() {
    stdio_init_all();
    sleep_ms(2000);

    if (!sd_init()) {
        printf("No se pudo montar la SD\n");
        return 1;
    }

    if (sd_append_line("log.txt", "Nueva línea de log\r\n")) {
        printf("Log escrito correctamente\n");
    } else {
        printf("Error al escribir en el log\n");
    }

    sd_unmount();
    return 0;
}