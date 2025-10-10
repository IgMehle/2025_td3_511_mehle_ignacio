#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
//#include "gpio_driver.h"

// Etiqueta para el autor del modulo
#define AUTHOR "IgMehle"

// Puntero para primer hilo
static struct task_struct *thread1;
// Puntero para segundo hilo
static struct task_struct *thread2;


/// @brief Hilo 1 periodico en kernel
/// @param params Puntero a parametros
/// @return 0 si OK
static int thread1_f(void *params) {
    // Corre mientras no haya otros procesos que lo detengan
    while(!kthread_should_stop()) {
        // Mensaje para el Kernel
        printk(KERN_INFO "%s: Hola desde el kernel!\n", AUTHOR);
        // Demora de un segundo
        msleep(1000);
    }
    return 0;
}

/// @brief Hilo 2 periodico en kernel
/// @param params Puntero a parametros
/// @return 0 si OK
static int thread2_f(void *params) {
    // Corre mientras no haya otros procesos que lo detengan
    while(!kthread_should_stop()) {
		// Demora de medio segundo
		msleep(500);
        // Mensaje para el Kernel
        printk(KERN_INFO "%s: Chau desde el kernel!\n", AUTHOR);
        // Demora de medio segundo
		msleep(500);
    }
    return 0;
}


/// @brief Callback que inicializa el modulo en el kernel
/// @return 0 si OK
static int __init kernel_module_init(void) {
	// Mensaje para el kernel
	printk(KERN_INFO "%s: Insertando el modulo de kernel\n", AUTHOR);
    // Intento crear y correr el hilo 1
	thread1 = kthread_run(
        thread1_f,  // Callback
        NULL,       // Sin datos
        "thread1"   // Nombre del hilo
    );
    // Verifico si hubo error al crearlo
    if (IS_ERR(thread1)) {
        printk(KERN_ERR "%s: Error al crear thread 1\n", AUTHOR);
        return -1;
    }
    // Intento crear y correr el hilo 2
    thread2 = kthread_run(
        thread2_f,  // Callback
        NULL,       // Sin datos
        "thread2"   // Nombre del hilo
    );
    // Verifico si hubo error al crearlo
    if (IS_ERR(thread2)) {
        printk(KERN_ERR "%s: Error al crear thread 2\n", AUTHOR);
        // Elimino el hilo anterior
        kthread_stop(thread1);
        return -1;
    }
	return 0;
}

/**
 * @brief Se llama cuando el modulo se quita del kernel
 */
static void __exit kernel_module_exit(void) {
	// Mensaje para el Kernel
	pr_info("%s: Removiendo el modulo de kernel\n", AUTHOR);
    // Si se habia podido crear el hilo
	if (thread1) {
        // Detengo el hilo
        kthread_stop(thread1);
    }
    // Si se habia podido crear el hilo
    if (thread2) {
        // Detengo el hilo
        kthread_stop(thread2);
    }
}

// Registro la funcion de inicializacion y salida
module_init(kernel_module_init);
module_exit(kernel_module_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("UTN FRA Tecnicas Digitales III - TP5: GPOS");
