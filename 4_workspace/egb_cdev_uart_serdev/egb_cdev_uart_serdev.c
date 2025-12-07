#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
// Headers para serdev
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/of_device.h>
// Headers para char device
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
// Headers utilitarios
#include <linux/string.h>
#include <linux/file.h>

// Labels deL modulo
#define DRIVER_NAME "egb_uart_serdev"
#define DEVICE_NAME "egb_uart"
#define CLASS_NAME  "egb_uart_class" 
#define AUTHOR      "Eirea-Mehle"

// Labels del char device
#define EGB_CDEV_MINOR  10
#define EGB_CDEV_COUNT  1

// Labels de la UART
#define UART_BUFFER_SIZE  128
#define UART_BAUDRATE   115200

// Labels del archivo rx
#define RX_FILE "/home/nacho/cli/rx.txt"

//=================================================================
//  VARIABLES GLOBALES
//=================================================================
// BUFFER DE RX
static char shared_rxbf[UART_BUFFER_SIZE];
// BUFFER DE TX
static char shared_txbf[UART_BUFFER_SIZE];

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
static size_t uart_serdev_recv( struct serdev_device *serdev, 
                                const unsigned char *buffer, 
                                size_t size)
{
    static char rx_buffer[UART_BUFFER_SIZE];
    static size_t rx_index = 0;

    // int status = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        if (rx_index < UART_BUFFER_SIZE - 2) {
            // Leo caracter del buffer de uart
            char c = buffer[i];
            // Agrego al buffer de recepcion
            rx_buffer[rx_index++] = c;

            // Si el caracter que llega es salto de linea
            if (c == '\n') {
                // Termino string (manteniendo salto de linea)
                rx_buffer[rx_index - 1] = '\n';
                rx_buffer[rx_index] = '\0';
                // Imprimo string
                pr_info("serdev_recv - Mensaje recibido: %s\n", rx_buffer);
                // Reset index
                rx_index = 0;
                // Limpio shared_rxbf
                memset(shared_rxbf, 0, sizeof(shared_rxbf));
                // Copio a shared_rxbf
                strcpy(shared_rxbf, rx_buffer);

                // ========================================================
                // ESCRITURA DE ARCHIVO RX
                // Variables necesarias para manipular archivos
                struct file *file;
                loff_t pos = 0;
                ssize_t bytes_written;

                // Abrir archivo
                file = filp_open(RX_FILE, O_RDWR | O_CREAT | O_APPEND, 0644);
                if (IS_ERR(file)) {
                    pr_err("serdev_recv - Error al abrir archivo\n");
                    return PTR_ERR(file);
                }
                // Escribo en el archivo con kernel_write()
                // file: puntero archivo
                // rx_buffer: string
                // strlen(rx_buffer) = tamaño en bytes
                // pos: offset
                bytes_written = kernel_write(file, rx_buffer, strlen(rx_buffer), &pos);
                if (bytes_written < 0) {
                    pr_err("serdev_recv - Error al escribir archivo\n");
                    filp_close(file, NULL);
                    // return bytes_written;
                }
                pr_info("serdev_recv - %zd bytes escritos en archivo %s\n", 
                    bytes_written, RX_FILE);

                // Cierro archivo
                filp_close(file, NULL);
                //==========================================================
                // Limpio rx_buffer local
                memset(rx_buffer, 0, sizeof(rx_buffer));
            }
        } else {
            pr_warn("serdev_recv - Buffer overflow!\n");
            rx_index = 0;
        }
    }
    // Echo local de debug de serdev_recv
    // return serdev_device_write_buf(serdev, rx_buffer, size);
    return size;
}

// SERDEV OPERATIONS
static const struct serdev_device_ops uart_serdev_ops = {
	.receive_buf = uart_serdev_recv,
};

// Puntero global al serdev
struct serdev_device *g_serdev = NULL;

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

    // Guardo puntero al serdev global
    g_serdev = serdev;

    pr_info("uart_serdev - Configuring UART\n");
    // Configuramos la UART
    serdev_device_set_baudrate(serdev, UART_BAUDRATE);
    serdev_device_set_flow_control(serdev, false);
    serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

    // Envio comando de bienvenida
    status = serdev_device_write_buf(serdev, "UART HABILITADA!\n", sizeof("UART HABILITADA!\n"));
    pr_info("uart_serdev - UART configurado!\n");

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
// CHAR DEVICE EGB_UART
//////////////////////////////////////////////////////////////////

// VARIABLES DEL CHAR DEVICE
static struct cdev egb_cdev;
static dev_t egb_cdev_number;
static struct class *egb_class;

// PROTOTIPOS DEL CHAR DEVICE
static ssize_t egb_uart_read(struct file *f, char __user *user_bf, size_t len, loff_t *offset);
static ssize_t egb_uart_write(struct file *f, const char __user *user_bf, size_t len, loff_t *offset);

// CHAR DEVICE FILE OPERATIONS
static struct file_operations egb_fops = {
    .owner = THIS_MODULE,
    .read = egb_uart_read,
    .write = egb_uart_write,
};

//////////////////////////////////////////////////////////////////
// CHAR DEVICE EGB_CDEV_READ()
//////////////////////////////////////////////////////////////////
static ssize_t egb_uart_read(   struct file *f, 
                                char __user *user_bf, 
                                size_t len, 
                                loff_t *offset)
{
    int not_copied;
    int delta;
    int to_copy;

    //pr_info("uart_read - Leyendo %d bytes, offset = %lld\n",
    //        to_copy, *offset);

    // Si el offset es mayor que el tamaño del buffer, no hay mas datos para leer
    if (*offset >= sizeof(shared_rxbf))
		return 0;

    // Reviso cuanto se puede leer
    // to_copy = min(size, sizeof(shared_rxbf) - *offset);
    to_copy = (len) < (sizeof(shared_rxbf) - *offset) ? len : (sizeof(shared_rxbf) - *offset);
    // Copio a userspace
    not_copied = copy_to_user(user_bf, shared_rxbf + *offset, to_copy);
    delta = to_copy - not_copied;
    pr_info("uart_read - Se copia a user el mensaje: %s", shared_rxbf);

    if (not_copied) 
		pr_warn("uart_read - Solo se copiaron %d bytes\n", delta);
    
    *offset += delta;
    // Limpio buffer de recepcion
    memset(shared_rxbf, 0, sizeof(shared_rxbf));
    // Devuelvo cantidad de bytes leidos
    return delta;
}

//////////////////////////////////////////////////////////////////
// CHAR DEVICE EGB_CDEV_WRITE()
//////////////////////////////////////////////////////////////////
static ssize_t egb_uart_write(  struct file *f, 
                                const char __user *user_bf, 
                                size_t len, 
                                loff_t *offset)
{
    int not_copied;
    int delta;
    int to_copy = (len + *offset) < sizeof(shared_rxbf) ? len : (sizeof(shared_rxbf) - *offset);

    pr_info("uart_write - Escribiendo %d bytes, offset = %lld\n",
            to_copy, *offset);
    
    if (*offset >= sizeof(shared_txbf))
		return 0;
    
    not_copied = copy_from_user(&shared_txbf[*offset], user_bf, to_copy);
    delta = to_copy - not_copied;

    if (not_copied) 
		pr_warn("uart_write - Solo se copiaron %d bytes\n", delta);
    
    *offset += delta;

    // Copio delta bytes de shared_txbf a serdev
    serdev_device_write_buf(g_serdev, shared_txbf, delta);
    // Limpio buffer de escritura
    memset(shared_txbf, 0, sizeof(shared_txbf));
    // Retorno cantidad de bytes escritos
    return delta;
}

//////////////////////////////////////////////////////////////////
// KERNEL MODULE INIT()
//////////////////////////////////////////////////////////////////
static int __init egb_init(void) {

    int status = 0; // Respuestas de funciones

    // Reservo memoria para crear el char device
    // ALLOC_CHRDEV_REGION()
    status = alloc_chrdev_region(   &egb_cdev_number, 
                                    EGB_CDEV_MINOR,
                                    EGB_CDEV_COUNT,
                                    DEVICE_NAME);
    // Verifico creacion exitosa
    if(status){
        pr_err("egb_init - Error reservando chrdev_region\n");
        return status;
    }

    // Inicializo char device con las fops definidas - CDEV_INIT()
    cdev_init(&egb_cdev, &egb_fops);
    egb_cdev.owner = THIS_MODULE;

    // Pongo el char device como disponible - CDEV_ADD()
    status = cdev_add(  &egb_cdev,
                        egb_cdev_number,
                        EGB_CDEV_COUNT);
    // Si no se pudo crear, libero memoria
    if(status){
        pr_err("egb_init - Error en add_cdev\n");
        goto free_devnr;
    }

    // Informo que se creo el char device con exito
    pr_info("egb_init - egb_cdev registrado con Major %d y Minor %d",
            MAJOR(egb_cdev_number), MINOR(egb_cdev_number));

    // Creo clase del cdev - CLASS_CREATE()
    egb_class = class_create(CLASS_NAME);
    // Si no se pudo crear la clase, borro cdev y libero memoria
    if(!egb_class){
        pr_err("egb_init - No pudo crearse la clase egb_uart_class\n");
        status = ENOMEM;
        goto delete_cdev;
    }

    // Creo device en /dev/egb_uart - DEVICE_CREATE()
    status = device_create( egb_class,
                            NULL,
                            egb_cdev_number,
                            NULL,
                            DEVICE_NAME);
    // Si no se pudo crear, borro class, cdev y libero memoria
    if(!status){
        pr_err("egb_init - No se pudo crear device en /dev/%s\n", DEVICE_NAME);
        status = ENOMEM;
        goto delete_class;
    }

    // Informo que se creo el device en /dev
    pr_info("egb_init - Device creado en /dev/egb_uart!\n");

    // Creo el serdev
	pr_info("egb_init - Creando uart_serdev_driver...\n");
    status = (int) serdev_device_driver_register(&uart_serdev_driver);
    // Si no se pudo registrar el serdev...
	if(status < 0){
		printk("egb_init - Error! No se pudo registrar el serdev\n");
		status = -1;
        goto delete_device;
	}

    // INIT OK
    pr_info("egb_init - Driver egb_uart_serdev registrado con éxito!\n");
    return 0;

    // MANEJO DE ERRORES ACUMULATIVO (gracias Johannes!)
    delete_device:
        device_destroy(egb_class, egb_cdev_number);
    delete_class:
        class_unregister(egb_class);
        class_destroy(egb_class);
    delete_cdev:
        cdev_del(&egb_cdev);
    free_devnr:
        unregister_chrdev_region(egb_cdev_number, EGB_CDEV_COUNT);
	    return status;
}

//////////////////////////////////////////////////////////////////
// KERNEL MODULE EXIT()
//////////////////////////////////////////////////////////////////
static void __exit egb_exit(void) {
	pr_info("egb_exit - Borrando serdev...\n");
	serdev_device_driver_unregister(&uart_serdev_driver);
    pr_info("egb_exit - Borrando device...\n");
    device_destroy(egb_class, egb_cdev_number);
    pr_info("egb_exit - Borrando class y cdev...\n");
    class_unregister(egb_class);
    class_destroy(egb_class);
    cdev_del(&egb_cdev);
    pr_info("egb_exit - Liberando device number...\n");
    unregister_chrdev_region(egb_cdev_number, EGB_CDEV_COUNT);
    pr_info("egb_exit - Finalizado!\n");
}

module_init(egb_init);
module_exit(egb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Bridge serdev (UART) en Raspberry Pi");
