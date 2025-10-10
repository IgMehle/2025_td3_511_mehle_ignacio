#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
// LIBRERIAS
#include "helper.h"
#include "lcd.h"
// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define PWM_PIN 2
#define COUNTER_PIN 3
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5

#define PWM_FREQ 1000

// Handle de semaforo counting
SemaphoreHandle_t sCount;

// IRQ del GPIO
void counter_irq(){
    static BaseType_t wake_higher_task = pdFALSE;
    xSemaphoreGiveFromISR(sCount, &wake_higher_task);
    portYIELD_FROM_ISR(wake_higher_task);
}

// TAREA CONTADOR
// static void task_Counter(void *pvParams)
// {
//     while(1){
//         if(gpio_get(COUNTER_PIN)){
//             xSemaphoreGive(sCount);
//             while(gpio_get(COUNTER_PIN));
//         }
//     }
// }

// TAREA PRINT
static void task_Print(void *pvParams)
{
    // Contador de pulsos
    uint16_t freq = 0;
    // Parametros de vTaskDelayUntil()
    TickType_t last_wake_tick = xTaskGetTickCount();
    const TickType_t freq_1seg = pdMS_TO_TICKS(1000);
    // String auxiliar para imprimir datos
    char txt_frecuencia[17];

    while(1){
        // Cuento la cantidad de veces que puedo tomar el semaforo
        freq = uxSemaphoreGetCount(sCount);
        // Formateo el valor dentro de una cadena auxiliar
        sprintf(txt_frecuencia, "FREQ: %4d", freq);
        // Envio string al display
        lcd_set_cursor(0, 0);
        lcd_string(txt_frecuencia);
        // Reseteo contador y semaforo
        // QueueReset() Tiene que estar justo antes del delayUntil()
        // para que cuente interrupciones con mayor exactitud
        freq = 0;
        xQueueReset(sCount);
        // Me bloqueo hasta que pase 1 segundo exacto
        vTaskDelayUntil(&last_wake_tick, freq_1seg);
    }
}

int main()
{
    stdio_init_all();

    // INIT GPIO INPUT PIN
    gpio_init(COUNTER_PIN);
    // GPIO como entrada
    gpio_set_dir(COUNTER_PIN, false);
    // Set pulldown
    gpio_set_pulls(COUNTER_PIN, false, true);
    // Habilito la IRQ del pin
    gpio_set_irq_enabled_with_callback(COUNTER_PIN, GPIO_IRQ_EDGE_RISE, true, &counter_irq);

    // PWM GEN (gracias Fabri!!)
    pwm_user_init(PWM_PIN, PWM_FREQ);

    // I2C INIT
    // I2C0 (DEFAULT) a 100khz
    i2c_init(i2c0, 100000);
    // I2C0_SDA en GPIO4
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    // I2C0_SCL en GPIO5
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    // Pongo pullups a 3V3
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // Inicializo LCD
    lcd_init(i2c0, 0x27);
    lcd_clear();

    // Creo semaforo counting
    sCount = xSemaphoreCreateCounting(10000, 0);

    // Creo tareas
    //xTaskCreate(task_Counter, "Counter", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    // Print tiene que tener mayor prioridad para que deje de contar cuando estoy imprimiendo
    xTaskCreate(task_Print, "Print", 2*configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    // START SCHEDULER
    vTaskStartScheduler();
    while (true);
}
