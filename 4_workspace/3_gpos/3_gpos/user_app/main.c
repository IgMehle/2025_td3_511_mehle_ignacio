#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// Nombre de archivo
#define CDEV_NAME	"/dev/egb"
// Maximo largo
#define MAX_LEN		128

/**
 * @brief Programa principal
*/
int main(void) {
	// Variable para buffer
	char str[MAX_LEN];
	char buf[MAX_LEN];
	// Variables para el manejo de archivo
	int dev;
	// Abro archivo como lectura escritura
	dev = open(CDEV_NAME, O_RDWR);
	// Verifico que se haya podido abrir
	if(dev == -1) {
		puts("No se pudo abrir el archivo\n");
		return -1;
	}
	// Mensaje por consola
	puts("Escriba lo que quiere ver por UART: (Ctrl+C para salir):");

	/* while(1) {
		// Guardo el dato para escribir
		fgets(str, MAX_LEN, stdin);
		// Escrivo al device
		write(dev, str, MAX_LEN);
		// Leo el archivo
		read(dev, str, sizeof(str));
		// Muestro lo que fue escrito
		printf("Leido del device: %s\n", str);
	}
	return 0;
} */

	while (1) {
        if (!fgets(str, sizeof(str), stdin)) break; // EOF o error

        // quitar posible '\n' final si querés (opcional)
        size_t len = strlen(str);
        // asegurarse que haya algo que enviar
        if (len == 0) continue;

        // Enviar SOLO los bytes utiles (sin rellenar a MAX_LEN)
        ssize_t w = write(dev, str, len);
        if (w < 0) {
            perror("write");
            break;
        }

        // Leer respuesta del device - usar el return de read
        ssize_t r = read(dev, buf, sizeof(buf)-1); // dejar espacio para '\0'
        if (r < 0) {
            perror("read");
            break;
        }
        if (r == 0) {
            printf("Read retornó 0 bytes\n");
            continue;
        }

        // Asegurar terminación
        if ((size_t)r >= sizeof(buf)) r = sizeof(buf)-1;
        buf[r] = '\0';

        printf("Leido del device: %s", buf); // buf suele incluir '\n'
    }

    close(dev);
    return 0;
}


