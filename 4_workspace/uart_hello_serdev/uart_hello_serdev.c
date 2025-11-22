#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/string.h>

#define DRIVER_NAME  "uart_hello"
#define AUTHOR       "IgMehle"

/* Buffer para acumular una línea completa hasta '\n' */
#define RX_LINE_BUF_SIZE 128
static char  rx_line_buf[RX_LINE_BUF_SIZE];
static size_t rx_line_len;

/*
 * Callback de recepción: vamos acumulando carácter por carácter
 * hasta encontrar '\n'. Recién ahí imprimimos la línea completa.
 */
static size_t uart_hello_receive(struct serdev_device *serdev,
                                 const unsigned char *buf,
                                 size_t size)
{
    size_t i;

    for (i = 0; i < size; i++) {
        unsigned char ch = buf[i];

        if (ch == '\n') {
            /* Cerrar string */
            if (rx_line_len < RX_LINE_BUF_SIZE)
                rx_line_buf[rx_line_len] = '\0';
            else
                rx_line_buf[RX_LINE_BUF_SIZE - 1] = '\0';

            /* Si termina en '\r', lo recortamos */
            if (rx_line_len > 0 && rx_line_buf[rx_line_len - 1] == '\r') {
                rx_line_buf[rx_line_len - 1] = '\0';
            }

            dev_info(&serdev->dev,
                     "Linea completa recibida por UART: '%s'\n",
                     rx_line_buf);

            /* Reseteamos para la próxima línea */
            rx_line_len = 0;
        } else {
            /* Acumular mientras haya espacio */
            if (rx_line_len < RX_LINE_BUF_SIZE - 1) {
                rx_line_buf[rx_line_len++] = ch;
            } else {
                /*
                 * Overflow del buffer de línea:
                 * opcionalmente podríamos loguear que se truncó.
                 * Por ahora simplemente descartamos lo que sobra.
                 */
            }
        }
    }

    /* Indicamos que consumimos todos los bytes */
    return size;
}

static const struct serdev_device_ops uart_hello_ops = {
    .receive_buf = uart_hello_receive,
    /* .write_wakeup opcional, no la necesitamos para write_buf */
};

static int uart_hello_probe(struct serdev_device *serdev)
{
    int ret;
    const char msg[] = "HELLO desde kernel via serdev!\r\n";

    /* Inicializamos el buffer de línea */
    rx_line_len = 0;

    dev_info(&serdev->dev, "uart_hello_probe: dispositivo encontrado\n");

    /* Asociamos nuestras operaciones (RX) al serdev */
    serdev_device_set_client_ops(serdev, &uart_hello_ops);

    /* Abrimos el dispositivo UART */
    ret = serdev_device_open(serdev);
    if (ret) {
        dev_err(&serdev->dev,
                "No se pudo abrir el dispositivo serdev: %d\n", ret);
        return ret;
    }

    /* Configuramos UART: 115200, sin control de flujo */
    serdev_device_set_baudrate(serdev, 115200);
    serdev_device_set_flow_control(serdev, false);
    /* Paridad opcional:
     * serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
     */

    dev_info(&serdev->dev,
             "Enviando mensaje inicial por UART (write_buf): \"%s\"\n", msg);

    /* Usamos write_buf, igual que en el ejemplo EGB */
    ret = serdev_device_write_buf(serdev, msg, strlen(msg));
    if (ret < 0) {
        dev_err(&serdev->dev,
                "Error al enviar datos por UART (write_buf): %d\n", ret);
        serdev_device_close(serdev);
        return ret;
    } else {
        dev_info(&serdev->dev,
                 "Se escribieron %d bytes por UART\n", ret);
    }

    dev_info(&serdev->dev,
             "uart_hello_serdev listo. Lineas terminadas en \\n se mostraran completas en dmesg.\n");

    return 0;
}

static void uart_hello_remove(struct serdev_device *serdev)
{
    dev_info(&serdev->dev, "uart_hello_remove: cerrando dispositivo\n");
    serdev_device_close(serdev);
}

static const struct of_device_id uart_hello_of_match[] = {
    { .compatible = "igmehle,uart_hello" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uart_hello_of_match);

static struct serdev_device_driver uart_hello_driver = {
    .probe  = uart_hello_probe,
    .remove = uart_hello_remove,
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = of_match_ptr(uart_hello_of_match),
    },
};

module_serdev_device_driver(uart_hello_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Demo serdev: Hello UART TX+RX con acumulacion por linea en Raspberry Pi");
