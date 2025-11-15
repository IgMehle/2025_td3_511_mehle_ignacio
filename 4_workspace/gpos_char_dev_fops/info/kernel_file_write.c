#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>

static int __init my_module_init(void) {
	// Variables necesarias para manipular archivos
    struct file *file;
    loff_t pos = 0;
    ssize_t bytes_written;

    // Abrir archivo
    file = filp_open("/tmp/writeKernel_file.txt", O_RDWR | O_CREAT, 0644);
    if (IS_ERR(file)) {
        pr_err("Failed to open file\n");
        return PTR_ERR(file);
    }

    // Armo string
    const char *data = "Hello, Kernel!";
	// Escribo en el archivo con kernel_write()
	// file: puntero archivo
	// data: string
	// strlen(data) = tamaño en bytes
	// pos: offset
    bytes_written = kernel_write(file, data, strlen(data), &pos);
    if (bytes_written < 0) {
        pr_err("Error al escribir archivo\n");
        filp_close(file, NULL);
        return bytes_written;
    }
    pr_info("%zd bytes escritos\n", bytes_written);

    // Cierro archivo
    filp_close(file, NULL);

    pr_info("Escritura de archivos exitosa\n");
    return 0;
}

static void __exit my_module_exit(void) {
    pr_info("Modulo extraido\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IgMehle");
MODULE_DESCRIPTION("Ejemplo de kernel_write()");