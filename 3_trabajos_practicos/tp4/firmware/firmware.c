#include <stdio.h>
#include "pico/stdlib.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

#include "helper.h"   //libreria para PWM
#include "lcd.h"      //libreria de LCD
#include "bmp280.h"   //libreria del sensor de presion y temperatura BMP280

#define I2C_PORT i2c0
#define I2C_SDA  8          //PIN 11-GPIO8
#define I2C_SCL  9          //PIN 12-GPIO9
#define ADDR_LCD     0x27    // Direccion de 7 bits del adaptador del LCD
#define SETPOINT 19       //Se agrega SETPOINT

#define PIN_BOTON 15        //PIN 20 COMO ENTRADA
#define PIN_LED 16         //PIN 20 COMO SALIDA PWM

/*Handlers de las tareas*/ //Se agrega para V2 
TaskHandle_t handle_Task_BMP280 = NULL;
TaskHandle_t handle_Task_LCD = NULL;
TaskHandle_t handle_Task_LED = NULL;

SemaphoreHandle_t Mutex_I2C;        //Handler del semaforo mutex para el I2C
QueueHandle_t Queue_data;           // Handler de la cola
QueueHandle_t Queue_page;           //Se agrega para V2

//Estructura para manejo de los datos del sensor
typedef struct {
    float temp;
    float presion;
} datasensor_t;

datasensor_t datos_bmp;     //Estructura de datos para la calibracion del sensor y calculos

//variable auxiliar //Se agrega para V2
bool change_page = false;

/*interrupcion que se ejecuta al presionar el boton, por flanco descendente, modificando un dato de cola
 que cambiara la pagina a mostrar en el lcd, */
void gpio_callback(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    bool page;
    if (gpio == PIN_BOTON && (events & GPIO_IRQ_EDGE_FALL)) {
        xQueuePeekFromISR(Queue_page, &page);
        page = !page;
        change_page = true;
        sleep_ms(200);
        xQueueOverwriteFromISR(Queue_page, &page, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

//TAREA LECTURA SENSOR TEMPERATURA Y PRESION
void task_BMP280(void *params)
{
    // Obtiene los parámetros de calibración
    struct bmp280_calib_param struct_calib_params;
    xSemaphoreTake(Mutex_I2C ,portMAX_DELAY);         //Toma el semaforo para que ningun otra tarea pueda utilizar el bus I2C
    bmp280_get_calib_params(&struct_calib_params);    //obtiene los parámetros de calibracion del sensor
    xSemaphoreGive(Mutex_I2C);                        //Devuelve el semaforo I2C

    while (1){
        int32_t raw_temp, raw_presion;
        xSemaphoreTake(Mutex_I2C ,portMAX_DELAY);     //vuelve a tomar el semaforo ahora para obtener los datos RAW del sensor
        bmp280_read_raw(&raw_temp, &raw_presion);     //Obtiene ambos valores sin compensar
        xSemaphoreGive(Mutex_I2C);                    //Devuelve semaforo

        datos_bmp.temp =    bmp280_convert_temp(raw_temp, &struct_calib_params);                           // Convierte temperatura
        datos_bmp.presion = bmp280_convert_pressure(raw_presion, raw_temp, &struct_calib_params) / 100.00; // Convierte presion
        
        printf("Temperatura: %.2f °C \nPresion: %.2f hPa\n", datos_bmp.temp, datos_bmp.presion);  //Imprime datos por consola serial para debuggear

        xQueueOverwrite(Queue_data, &datos_bmp);    //Sobreescribe datos del sensor en cola para escribir en el LCD.

        vTaskDelay(pdMS_TO_TICKS(500));         //Delay de 100ms hasta proxima lectura
    }
}

//RECIBO DE DATOS DE LA COLA E IMPRESIÓN EN EL LCD
void task_LCD(void *params)
{
    datasensor_t datos_LCD;
    bool page;  //Se agrega
    // Variable para imprimir el mensaje
    char str[16];
    float error_prueba;

    while (1){
        xQueuePeek(Queue_data, &datos_LCD, portMAX_DELAY);         //Copia Queue_data en datos_LCD
        xSemaphoreTake(Mutex_I2C ,portMAX_DELAY);                  //Toma el semaforo mutex para poder imprimir en LCD y no ser interrumpido por lectura de datos del sensor
        xQueuePeek(Queue_page, &page, portMAX_DELAY);              //hago peek de numero de pagina a imprimir

         if(change_page){   //verifico si es necesario un borrado de pantalla
            lcd_clear(); 
            change_page = false;
        }
        if(!page){
            lcd_set_cursor(0, 0);
            sprintf(str, "Temp: %.2f C", datos_LCD.temp);
            lcd_string(str);                                            
            lcd_set_cursor(1, 0);
            sprintf(str,"Pres: %.1f hPa", datos_LCD.presion);
            lcd_string(str);
            printf("ESTOY ACA 1\n");                      //Mensaje de DEBUG.
        }
        else{                                           //imprimo datos de TEMP, SETPOINT Y ERROR  en la segunda pagina del DISPLAY LCD
            lcd_set_cursor(0, 0);                                      
            sprintf(str, "Temp: %.2f C", datos_LCD.temp);
            lcd_string(str);
            lcd_set_cursor(1, 0);
            sprintf(str, "SetPoint: %.2f C", SETPOINT);
            lcd_string(str);
            // Muevo el cursor al comienzo de la segunda fila
            lcd_set_cursor(2, 0);
            // Imprimo el mensaje
            error_prueba = datos_LCD.temp-SETPOINT; if (error_prueba < 0 ) error_prueba = - error_prueba;  //obtengo valor absoluto del error
            sprintf(str,"Error: %.2f C", error_prueba);
            lcd_string(str);
            printf("ESTOY ACA 0\n");                      //Mensaje de DEBUG.
        } 
        
        xSemaphoreGive(Mutex_I2C);                 //Devuelve semaforo mutex I2C para libre utilizacion del sensor, si fuese necesario
        vTaskDelay(pdMS_TO_TICKS(500));             //demora de 100ms hasta proxima secuencia de impresion
    }

}

//tarea de encendido de LED mediante el uso de PWM
void task_LED(void *params){
    datasensor_t datos_LCD;
    float error_prueba;
    uint16_t pwm_duty;
    
    while(1){    
        xQueuePeek(Queue_data, &datos_LCD, portMAX_DELAY);         //hago peek de los datos de sensor (solo utilizo la TEMP)
        error_prueba = datos_LCD.temp-SETPOINT;                     //calculo ERROR
        if (error_prueba < 0 ) error_prueba = - error_prueba;       //obtengo valor absoluto del ERROR
        pwm_duty = (uint16_t)(error_prueba * 100.0 / 5.0);          //hago escalado de ERROR -> PWM ------ (0 - 5 °C) -> (0 - 100) 
        if(pwm_duty > 100) pwm_duty = 100;                          //limito maximo dutycicle a 100
        if(pwm_duty < 0 ) pwm_duty = 0;                             //limito minimo dutycicle a 0 
        pwm_set_gpio_level(PIN_LED, pwm_duty);                      //establezco dutycicle del PWM
        vTaskDelay(pdMS_TO_TICKS(100));                             //demora de 100ms hasta proxima actualizacion del LED
    }
}


int main(){

    stdio_init_all();

     // Generador de PWM para encender el LED
    pwm_user_init(PIN_LED, 10000);

    //creacion del semaforo mutex
    Mutex_I2C = xSemaphoreCreateMutex();

     //Creacion de la cola que enviara datos de Task_BMP280 a Task_LCD
    Queue_data = xQueueCreate(1, sizeof(datasensor_t));

    /*Se agrega*/
    bool page_number = 0;
    Queue_page = xQueueCreate(1, sizeof(bool));
    xQueueOverwrite(Queue_page, &page_number);

    i2c_init(I2C_PORT, 400*1000);                   // Inicializo el I2C con un clock de 400 KHz
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);      // Habilito la funcion de I2C en los GPIOs
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);                          // Habilito pull-ups
    gpio_pull_up(I2C_SCL);
    
    lcd_init(I2C_PORT, ADDR_LCD);                   // Inicializo LCD
    lcd_clear();  

    bmp280_init(i2c0);                              // Inicializa el BMP280 usando el I2C0

    gpio_init(PIN_BOTON);
    gpio_set_dir(PIN_BOTON, false);
    //gpio_pull_up(PIN_BOTON);

    xTaskCreate(task_BMP280, "Task_BMP280", 4*configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_LCD, "Task_LCD", 4*configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_LCD, "Task_LCD", 4*configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    //Habilito las interrupcion del pulsador po flanco ascendente
    gpio_set_irq_enabled_with_callback(PIN_BOTON, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    // Arranca el scheduler
    vTaskStartScheduler();

    while (true);
}
