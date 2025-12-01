#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/string.h>

#define DRIVER_NAME  "egb_uart_serdev"
#define AUTHOR       "Eirea-Mehle"

#define RX_BUFFER_SIZE 128

////////////////////////////////////////////////////////////////////
//  UART-SERDEV DEFINICIONES
////////////////////////////////////////////////////////////////////

// Callbacks del serial device
static int uart_serdev_probe(struct serdev_device *serdev);
static void uart_serdev_remove(struct serdev_device *serdev);

// Compatible devices
static struct of_device_id uart_serdev_ids[] = {
	{ .compatible = "td3-egb,uart-serdev"}, 
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uart_serdev_ids);

// SERDEV CALLBACK STRUCT
static struct serdev_device_driver uart_serdev_driver = {
	.probe = uart_serdev_probe,
	.remove = uart_serdev_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = uart_serdev_ids,
	},
};

//////////////////////////////////////////////////////////
// CALLBACK RX SERDEV
//////////////////////////////////////////////////////////
static size_t uart_serdev_recv(struct serdev_device *serdev, 
                            const unsigned char *buffer, 
                            size_t size)
{
    static char rx_buffer[RX_BUFFER_SIZE];
    static size_t rx_index = 0;

    size_t i;
    for (i = 0; i < size; i++) {
        if (rx_index < RX_BUFFER_SIZE - 1) {
            char c = buffer[i];
            rx_buffer[rx_index++] = c;

            if (c == '\n') {
                rx_buffer[rx_index - 1] = '\0'; // Termino string
                pr_info("serdev_recv - Mensaje recibido: %s\n", rx_buffer);
                rx_index = 0;
            }
        } else {
            pr_warn("serdev_recv - Buffer overflow!\n");
            rx_index = 0;
        }
    }
    // return serdev_device_write_buf(serdev, buffer, size); // echo back
    return size;
}

// SERDEV OPERATIONS
static const struct serdev_device_ops uart_serdev_ops = {
	.receive_buf = uart_serdev_recv,
};

///////////////////////////////////////////////////////////
// SERDEV - PROBE()
///////////////////////////////////////////////////////////
static int uart_serdev_probe(struct serdev_device *serdev)
{
    int status;
    // Indico que entramos en la funcion probe()
    pr_info("uart_serdev - Probe called\n");

    // Seteo client operations
    serdev_device_set_client_ops(serdev, &uart_serdev_ops);

    // Abro device
    status = serdev_device_open(serdev);
    if (status) {
        pr_err("uart_serdev - Error opening serial port (%d)\n", status);
        return -status;
    }

    pr_info("echo - Configuring UART\n");
    // Configuramos la UART
    serdev_device_set_baudrate(serdev, 115200);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

    // Envio comando de bienvenida
    status = serdev_device_write_buf(serdev, "UART HABILITADA!", sizeof("UART HABILITADA!"));
    pr_info("uart_serdev - UART configurado\n");

    return 0;
}

//////////////////////////////////////////////////////////////
// SERDEV - REMOVE()
//////////////////////////////////////////////////////////////
static void uart_serdev_remove(struct serdev_device *serdev)
{
    pr_info("uart_serdev - Removiendo UART serdev...\n");
    serdev_device_close(serdev);
}

//////////////////////////////////////////////////////////////////
// KERNEL MODULE INIT()
//////////////////////////////////////////////////////////////////
static int __init egb_init(void) {
	pr_info("EGB - Creando serdev...\n");
	if(serdev_device_driver_register(&uart_serdev_driver)) {
		printk("EGB - Error! No pudo crearse el serdev\n");
		return -1;
	}
	return 0;
}

//////////////////////////////////////////////////////////////////
// KERNEL MODULE EXIT()
//////////////////////////////////////////////////////////////////
static void __exit egb_exit(void) {
	pr_info("EGB - Borrando serdev...\n");
	serdev_device_driver_unregister(&uart_serdev_driver);
}

module_init(egb_init);
module_exit(egb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Demo serdev (UART) en Raspberry Pi");
