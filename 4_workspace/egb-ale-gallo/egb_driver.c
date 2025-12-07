#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/serdev.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/string.h>


// Autor del modulo
#define AUTHOR				"Alejandro"
#define DRIVER_NAME 		"egb_kernel"
#define DEVICE_NAME  		"egb"
#define CLASS_NAME   		"egb_class"
// Char device name
//#define CDEV_NAME	"egb_driver_uart"
// Cantidad maxima de bytes para el buffer de usuario
#define SHARED_BUFF_SIZE	128
#define BAUDRATE 			115200

// Estructura para manejar el char device
/* typedef struct {
	struct cdev cdev;			// Guarda el char device
	dev_t cdev_number;			// Guarda el major y minor number
	unsigned int cdev_major;	// Numero mayor
	struct class *cdev_class;	// Clase del char device
} egb_cdev_t; 

// Variable para mi char device
egb_cdev_t egb_cdev; 
*/

static char rx_buffer[SHARED_BUFF_SIZE];
static size_t rx_len = 0;

static char tx_buffer[SHARED_BUFF_SIZE];

static DEFINE_MUTEX(tx_mutex);
static DEFINE_MUTEX(rx_mutex);
static DECLARE_WAIT_QUEUE_HEAD(rx_wq);
static int rx_ready = 0;

static char last_tx[SHARED_BUFF_SIZE];
static size_t last_tx_len = 0;

static struct class *egb_class;
static struct cdev egb_cdev;
static dev_t dev_number;

// Puntero global para UART
static struct serdev_device *g_serdev = NULL;


/* ============================================================
 * SERDEV - UART RECEIVE (modificado)
 * ============================================================ */
static size_t egb_serdev_receive(struct serdev_device *serdev, const unsigned char *buf, size_t count)
{
    size_t i;

    /* Bloqueamos tx_mutex primero (evita races/leer last_tx seguro), luego rx_mutex */
    mutex_lock(&tx_mutex);
    mutex_lock(&rx_mutex);

    for (i = 0; i < count; i++) {
        char ch = buf[i];

        if (ch == '\n' || ch == '\r') {
            if (rx_len > 0) {
                rx_buffer[rx_len] = '\0';

                /* crear copia trimmeada de la linea recibida */
                char line_tmp[SHARED_BUFF_SIZE];
                size_t j;
                strncpy(line_tmp, rx_buffer, SHARED_BUFF_SIZE);
                line_tmp[SHARED_BUFF_SIZE-1] = '\0';
                for (j = strlen(line_tmp); j > 0; j--) {
                    if (line_tmp[j-1] == '\n' || line_tmp[j-1] == '\r' || line_tmp[j-1] == ' ')
                        line_tmp[j-1] = '\0';
                    else
                        break;
                }

                /* Si coincide con last_tx, considerarlo eco y descartarlo */
                if (last_tx_len > 0 && strcmp(line_tmp, last_tx) == 0) {
                    dev_info(&serdev->dev, "Descartando ECO: '%s'\n", line_tmp);
                    /* limpiar rx_len para empezar a recibir la siguiente línea */
                    rx_len = 0;
                    /* opcional: resetear last_tx_len si querés evitar dobles coincidencias */
                    last_tx_len = 0;
                    /* NO wake_up; ignoramos esta línea */
                } else {
                    /* Línea válida -> pasar a userspace */
                    dev_info(&serdev->dev, "Linea completa recibida por UART: '%s'\n", rx_buffer);
                    rx_ready = 1;
                    wake_up_interruptible(&rx_wq);
                    /* limpiar rx_len para la siguiente línea */
                    rx_len = 0;
                }
            }
        } else {
            if (rx_len < SHARED_BUFF_SIZE - 1)
                rx_buffer[rx_len++] = ch;
            /* si el buffer se llena, lo truncamos (podés añadir manejo de overflow) */
        }
    }

    mutex_unlock(&rx_mutex);
    mutex_unlock(&tx_mutex);
    return count;
}

static const struct serdev_device_ops egb_serdev_ops = {
    .receive_buf = egb_serdev_receive,
};


/* ============================================================
 *         CHAR DEVICE: READ (bloqueante hasta recibir UART)
 *         Versión sin usar *offset, permite múltiples lecturas
 * ============================================================ */

static ssize_t egb_read(struct file *f, char __user *buf, size_t size, loff_t *offset) {
    int ret;
    ssize_t copied;
    size_t len;
    char line[SHARED_BUFF_SIZE + 2];

    /* Esperar hasta que haya una linea completa desde UART */
    ret = wait_event_interruptible(rx_wq, rx_ready == 1);
    if (ret < 0)
        return ret;

    mutex_lock(&rx_mutex);

    /* Copiamos la línea completa desde rx_buffer a 'line' */
    copied = strscpy(line, rx_buffer, SHARED_BUFF_SIZE);
    if (copied < 0) {
        line[SHARED_BUFF_SIZE - 1] = '\0';
        len = SHARED_BUFF_SIZE - 1;
    } else {
        len = copied;
    }

    /* Agregar '\n' para que cat /dev/egb se vea prolijo */
    if (len < SHARED_BUFF_SIZE - 1) {
        line[len] = '\n';
        len++;
        line[len] = '\0';
    }

    if (len > size)
        len = size;

    /* Copiamos al espacio de usuario */
    if (copy_to_user(buf, line, len)) {
        mutex_unlock(&rx_mutex);
        return -EFAULT;
    }

    /* Reset del estado: ahora que ya entregamos la línea la limpiamos */
    rx_ready = 0;
    rx_len = 0;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    /* Mensaje testigo para el kernel: usar 'line', no 'buf' */
    printk(KERN_INFO "%s: Leido sobre /dev/%s - %s\n", DRIVER_NAME, DEVICE_NAME, line);

    mutex_unlock(&rx_mutex);
    return len;
}

/* ============================================================
 *           CHAR DEVICE: WRITE (envía por UART)
 * ============================================================ */

static ssize_t egb_write(struct file *f, const char __user *buf, size_t size, loff_t *offset)
{
    size_t len = (size > SHARED_BUFF_SIZE - 2) ? SHARED_BUFF_SIZE - 2 : size;

    if (copy_from_user(tx_buffer, buf, len))
        return -EFAULT;

    /* añadir newline si no existe */
    if (len == 0 || (tx_buffer[len-1] != '\n' && tx_buffer[len-1] != '\r')) {
        tx_buffer[len++] = '\n';
        tx_buffer[len] = '\0';
    } else {
        tx_buffer[len] = '\0';
    }

    /* Guardar copia trimmeada en last_tx para filtrar eco */
    mutex_lock(&tx_mutex);
    {
        /* crear copia sin CR/LF al final */
        size_t i;
        size_t l = (len >= SHARED_BUFF_SIZE) ? SHARED_BUFF_SIZE - 1 : len;
        /* copiar tx_buffer a last_tx */
        memcpy(last_tx, tx_buffer, l);
        last_tx[l] = '\0';
        /* recortar CR/LF/espacios finales */
        for (i = l; i > 0; i--) {
            if (last_tx[i-1] == '\n' || last_tx[i-1] == '\r' || last_tx[i-1] == ' ')
                last_tx[i-1] = '\0';
            else
                break;
        }
        last_tx_len = strlen(last_tx);

        /* Enviar */
        if (g_serdev) serdev_device_write_buf(g_serdev, tx_buffer, len);
    }
    mutex_unlock(&tx_mutex);

    return len;
}

/* static unsigned int egb_poll(struct file *f, poll_table *wait)
{
    unsigned int mask = 0;

    poll_wait(f, &rx_wq, wait);

    mutex_lock(&rx_mutex);
    if (rx_ready)
        mask |= POLLIN | POLLRDNORM;
    mutex_unlock(&rx_mutex);

    return mask;
} */

// Estructura para implementacion de operaciones con archivos
static const struct file_operations egb_fops = {
    .owner = THIS_MODULE,
    .read  = egb_read,
    .write = egb_write,
    //.poll  = egb_poll,
};


/* ============================================================
 *                        SERDEV PROBE
 * ============================================================ */

static int egb_serdev_probe(struct serdev_device *serdev)
{
    int ret;
    const char hello_msg[] = "EGB Driver conectado\r\n";

    pr_info("%s: dispositivo encontrado\n", DRIVER_NAME);

    g_serdev = serdev;

	/* Asociamos nuestras operaciones (RX) al serdev */
    serdev_device_set_client_ops(serdev, &egb_serdev_ops);

	/* Abrimos el dispositivo UART */
    ret = serdev_device_open(serdev);
    if (ret)
        return ret;

	// UART - Setup
    serdev_device_set_baudrate(serdev, BAUDRATE);
    serdev_device_set_flow_control(serdev, false);

    serdev_device_write_buf(serdev, hello_msg, strlen(hello_msg));

    return 0;
}

static void egb_serdev_remove(struct serdev_device *serdev)
{
    //pr_info("egb_kernel: serdev remove\n");
	pr_info("%s: Cerrando dispositivo\n", DRIVER_NAME);
    serdev_device_close(serdev);
}


/* ============================================================
 *      DEVICE TREE (overlay UART)
 * ============================================================ */

// IDs de serial devices
static const struct of_device_id egb_of_match[] = {
    { .compatible = "frankie,egb-driver" },
    {},
};
MODULE_DEVICE_TABLE(of, egb_of_match);

// Estructura de implementacion del driver
static struct serdev_device_driver egb_serdev_driver = {
	.probe = egb_serdev_probe,
	.remove = egb_serdev_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(egb_of_match),
	},
};


/* ============================================================
 *                  INIT / EXIT DEL MÓDULO
 * ============================================================ */

static int __init egb_driver_init(void)
{
	int ret;

	/* Reservar major/minor dinámicamente */
    ret = alloc_chrdev_region(&dev_number, 0, 1, DEVICE_NAME);
    if (ret < 0) return ret;

	/* Crear clase */
    egb_class = class_create(CLASS_NAME);
    if (IS_ERR(egb_class)) {
        unregister_chrdev_region(dev_number, 1);
        return PTR_ERR(egb_class);
    }

	/* Crear /dev/egb */
    device_create(egb_class, NULL, dev_number, NULL, DEVICE_NAME);

	/* Inicializar cdev */
    cdev_init(&egb_cdev, &egb_fops);
    ret = cdev_add(&egb_cdev, dev_number, 1);
    if (ret < 0) return ret;

    ret = serdev_device_driver_register(&egb_serdev_driver);
    if (ret < 0)
        return ret;

    pr_info("%s: Driver cargado (/dev/egb + UART)\n", DRIVER_NAME);
    return 0;
}

static void __exit egb_driver_exit(void)
{
	serdev_device_driver_unregister(&egb_serdev_driver);

    cdev_del(&egb_cdev);
    device_destroy(egb_class, dev_number);
    class_destroy(egb_class);
    unregister_chrdev_region(dev_number, 1);

    pr_info("%s: Driver removido\n", DRIVER_NAME);
}


// Registro funciones de inicializacion y saiida del driver
module_init(egb_driver_init);
module_exit(egb_driver_exit);

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Kernel Space EGB: CDEV + SERDEV");
