#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>

static int __init my_module_init(void) {
	// Variables necesarias para manipular archivos
    struct file *file;
    char buffer[128];
    loff_t pos = 0;
    ssize_t bytes_read;

    // Abrir archivo
    file = filp_open("/tmp/writeKernel_file.txt", O_RDONLY, 0);
    if (IS_ERR(file)) {
        pr_err("Error al abrir archivo\n");
        return PTR_ERR(file);
    }

	// Leer del archivo
    bytes_read = kernel_read(file, buffer, sizeof(buffer), &pos);
    if (bytes_read < 0) {
        pr_err("Error al leer archivo\n");
        filp_close(file, NULL);
        return bytes_read;
    }

    // Terminar el buffer con caracter nulo
    if (bytes_read >= sizeof(buffer)) {
        bytes_read = sizeof(buffer) - 1;
    }
    buffer[bytes_read] = '\0';

    pr_info("%zd bytes leidos: %s\n", bytes_read, buffer);

    // Cerrar archivo
    filp_close(file, NULL);

    return 0;
}

static void __exit my_module_exit(void) {
    pr_info("Modulo extraido\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IgMehle");
MODULE_DESCRIPTION("Ejemplo de kernel_read()");