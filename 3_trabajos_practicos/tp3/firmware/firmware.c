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

}

// TAREA CONTADOR
static void task_Counter(void *pvParams)
{
    while(1){
        if(gpio_get(COUNTER_PIN)){
            xSemaphoreGive(sCount);
            while(gpio_get(COUNTER_PIN));
        }
    }
}

// TAREA PRINT
static void task_Print(void *pvParams)
{
    // Contador de pulsos
    uint16_t freq = 0;
    // Parametros de vTaskDelayUntil()
    TickType_t last_wake_tick = xTaskGetTickCount();
    const TickType_t freq_1seg = pdMS_TO_TICKS(1000);

    while(1){
        // Cuento la cantidad de veces que puedo tomar el semaforo
        // while(xSemaphoreTake(sCount, 0) == pdPASS){
        //     // Incremento el contador
        //     freq++;
        // }
        freq = uxSemaphoreGetCount(sCount);
        xQueueReset(sCount);
        // Imprimo
        printf("Frecuencia: %d\n", freq);
        freq = 0;
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
    //gpio_set_irq_enabled_with_callback(COUNTER_PIN, GPIO_IRQ_EDGE_RISE, true, &counter_irq);

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

    // Creo semaforo counting
    sCount = xSemaphoreCreateCounting(10000, 0);

    // Creo tareas
    xTaskCreate(task_Counter, "Counter", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    // Print tiene que tener mayor prioridad para que deje de contar cuando estoy imprimiendo
    xTaskCreate(task_Print, "Print", 2*configMINIMAL_STACK_SIZE, NULL, 2, NULL);

    // START SCHEDULER
    vTaskStartScheduler();
    while (true);
}
