#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
// Headers de librerias de modulos
#include "bh1750.h"
#include "ds3231.h"
#include "at24c32.h"
#include "lcd.h"
// Headers de FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define I2C1_FREQ       100000
#define I2C1_SDA_PIN    18
#define I2C1_SCL_PIN    19
#define LCD_ADDR        0x27

#define PWM_OUT_PIN     22
#define ENC_A_PIN       14
#define ENC_B_PIN       15
#define PULS_PIN        13
#define LED1_PIN        6
#define LED2_PIN        7
#define LED_RUN_PIN     25
#define AL_LO_PIN       20
#define AL_HI_PIN       21
#define BUZZER_PIN      12

#define LED_ON 	        0
#define LED_OFF	        1
#define DEBOUNCE_TIME   20

// Handles de tareas
TaskHandle_t tPWM, tBH1750, tEEPROM, tLCD, tEncoder, tPulsador, tRUN;
// Queues de datos inter tarea
QueueHandle_t qPWM, qLUX, qLCD, qCTRL, qEread, qEwrite;
// Mutex bus I2C1
SemaphoreHandle_t mI2C;
// Semaforos de control
SemaphoreHandle_t sPULS, sA, sB, sEwrite, sEread;

// Item de queue LCD
typedef struct {
    char text[17];
    uint8_t line;
} qLCD_t;

// Item de queue LUX
typedef struct {
    uint16_t lux;
} qLUX_t;

void irq_pulsador(uint gpio, uint32_t events)
{
    static BaseType_t taskWoken = pdTRUE;
    // Deshabilito irq para que no se redispare
    gpio_set_irq_enabled(PULS_PIN, GPIO_IRQ_EDGE_RISE, false);
    xSemaphoreGiveFromISR(sPULS, &taskWoken);
}

void irq_encoderA(uint gpio, uint32_t events)
{
    static BaseType_t taskWoken = pdTRUE;
    // Deshabilito irq para que no se redispare
    gpio_set_irq_enabled(ENC_A_PIN, GPIO_IRQ_EDGE_RISE, false);
    xSemaphoreGiveFromISR(sA, &taskWoken);
}

void irq_encoderB(uint gpio, uint32_t events)
{
    static BaseType_t taskWoken = pdTRUE;
    // Deshabilito irq para que no se redispare
    gpio_set_irq_enabled(ENC_B_PIN, GPIO_IRQ_EDGE_RISE, false);
    xSemaphoreGiveFromISR(sB, &taskWoken);
}

void task_encoder(void *pvParams)
{
    char comando;
    while(1){
        // SEMAFORO PULSADOR
        if(xSemaphoreTake(sPULS, portMAX_DELAY) == pdPASS){
            // tiempo de antirrebote
            sleep_ms(DEBOUNCE_TIME);
            // Verifico pulsador
            if(gpio_get(PULS_PIN)){
                // Envio comando a la cola
                comando = 'P';
                xQueueOverwrite(qCTRL, &comando);
                gpio_set_irq_enabled(PULS_PIN, GPIO_IRQ_EDGE_RISE, true);
            }
        }
        // SEMAFORO ENCODER A
        
        // SEMAFORO ENCODER B
        
        // VUELVO A HABILITAR IRQS
        irq_set_enabled(GPIO_IRQ_EDGE_RISE, true);
    }
}

void task_PWM(void *pvParams)
{
    while(1){

    }
}

void task_BH1750(void *pvParams)
{
    uint16_t lux;
    while(1){
        // Si puedo tomar el bus
        if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
            // Leo luxometro
            lux = bh1750_read();
            xSemaphoreGive(mI2C);
            // Paso a la cola
            xQueueOverwrite(qLUX, &lux);
        }
        // Corre cada 150ms
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void task_EEPROM(void *pvParams)
{
    rtc_t time;
    uint8_t bf[7];
    uint16_t address = 0x0010;
    while(1){
        // Intento leer de la queue de escritura
        if(xQueueReceive(qEwrite, &time, portMAX_DELAY) == pdPASS){
            // Desempaqueto
            bf[0] = time.second;
            bf[1] = time.minute;
            bf[2] = time.hour;
            bf[3] = time.weekday;
            bf[4] = time.day;
            bf[5] = time.month;
            bf[6] = time.year;
            // Intento tomar el bus
            if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                // Escribo hora en la eeprom
                eeprom_write(bf, address, 9);
                xSemaphoreGive(mI2C);
            }
        }
        // Intento tomar el semaforo de lectura
        if(xSemaphoreTake(sEread, portMAX_DELAY) == pdPASS){
            // Intento tomar el bus
            if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                // Leo la eeprom
                eeprom_read(bf, address, 7);
                xSemaphoreGive(mI2C);
            }
            // Empaqueto
            time.second = bf[0];
            time.minute = bf[1];
            time.hour = bf[2];
            time.weekday = bf[3];
            time.day = bf[4];
            time.month = bf[5];
            time.year = bf[6];
            // Mando a la cola de lectura
            xQueueSend(qEread, &time, portMAX_DELAY);
        }
    }
}

void task_LCD(void *pvParams)
{
    qLCD_t bf;
    while(1){
        // Si hay datos en la cola, intento tomar el bus
        if(xQueueReceive(qLCD, &bf, portMAX_DELAY) == pdPASS){
            // Si puedo tomar el bus
            if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                lcd_set_cursor(bf.line, 0);
                lcd_string(bf.text);
                xSemaphoreGive(mI2C);
            }
        }        
    }
}

void task_pulsador(void *pvParams)
{
    BaseType_t taken;
    rtc_t time = {0};

    while(1){
        taken = xSemaphoreTake(sPULS, portMAX_DELAY);
        if(taken == pdPASS){
            // delay de antirrebote
			vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_TIME));
            // si el pin sigue en 0
            if(!gpio_get(PULS_PIN)){
                // Prendo leds de debug y buzzer
                gpio_put(LED1_PIN, LED_ON);
                gpio_put(LED2_PIN, LED_ON);
                gpio_put(BUZZER_PIN, true);
                
                // Leo RTC
                // Intento tomar el bus
                if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                    vTaskSuspendAll();
                    rtc_read(&time);
                    xTaskResumeAll();
                    xSemaphoreGive(mI2C);
                }
                // Envio a la cola
                xQueueSend(qEwrite, &time, portMAX_DELAY);

                // DELAY 50MS
                vTaskDelay(pdMS_TO_TICKS(50));
                // Apago leds
                gpio_put(BUZZER_PIN, false);
                gpio_put(LED1_PIN, LED_OFF);
                gpio_put(LED2_PIN, LED_OFF);
            }
        }
    }   
}

void task_TestHardware(void *pvParams)
{
    uint16_t lux;
    float delta;
    qLCD_t lcd;
    rtc_t time;
    uint8_t bf[7];

    while(1){
        // Hay lectura del luxometro ?
        if(xQueueReceive(qLUX, &lux, portMAX_DELAY) == pdPASS){
            /////////////////
            // CONTROL PWM //
            /////////////////
            xQueueSend(qPWM, &delta, portMAX_DELAY);
            // Armo el texto para el LCD
            sprintf(lcd.text, "LUX: %d", lux);
            lcd.line = 0;
            // Envio a la cola del lcd
            xQueueSend(qLCD, &lcd, portMAX_DELAY);
            // Fuerzo cambio de contexto
            taskYIELD();
            // Leo ultima hora en eeprom
            xSemaphoreGive(sEread);
            taskYIELD();
            // leo queue de eeprom
            xQueueReceive(qEread, &time, portMAX_DELAY);
            // Desempaqueto
            bf[0] = time.second;
            bf[1] = time.minute;
            bf[2] = time.hour;
            bf[3] = time.weekday;
            bf[4] = time.day;
            bf[5] = time.month;
            bf[6] = time.year;
            // muestro la ultima hora
            sprintf(lcd.text, "%02d/%02d %02d:%02d:%02d", time.day, time.month, 
                                time.hour, time.minute, time.second);
            lcd.line = 1;
            // Escribo hora en el LCD
            xQueueSend(qLCD, &lcd, portMAX_DELAY);
        }
    }
}

void task_LedRun(void *pvParams)
{
    while(1){
        gpio_put(LED_RUN_PIN, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_put(LED_RUN_PIN, false);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main()
{
    stdio_init_all();

    // INIT GPIO INPUT PINS
    gpio_init(ENC_A_PIN);
    gpio_init(ENC_B_PIN);
    gpio_init(PULS_PIN);
    // GPIO como entradas
    gpio_set_dir(ENC_A_PIN, false);
    gpio_set_dir(ENC_B_PIN, false);
    gpio_set_dir(PULS_PIN, false);
    // Habilito las IRQ
    gpio_set_irq_enabled_with_callback(ENC_A_PIN, GPIO_IRQ_EDGE_RISE, true, &irq_encoderA);
    gpio_set_irq_enabled_with_callback(ENC_B_PIN, GPIO_IRQ_EDGE_RISE, true, &irq_encoderB);
    gpio_set_irq_enabled_with_callback(PULS_PIN, GPIO_IRQ_EDGE_RISE, true, &irq_pulsador);

    // I2C INIT
    i2c_init(i2c1, I2C1_FREQ);
    gpio_set_function(I2C1_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C1_SCL_PIN, GPIO_FUNC_I2C);
    // Pongo pullups a 3V3
    gpio_pull_up(I2C1_SDA_PIN);
    gpio_pull_up(I2C1_SCL_PIN);

    // Inicializo LCD
    lcd_init(i2c1, LCD_ADDR);
    lcd_clear();

    // Inicializo BH1750
    bh1750_init();

    // Inicializo DS3231
    rtc_t init;
    init.second = 30;
    init.minute = 10;
    init.hour = 20;
    init.weekday = 3;
    init.day = 2;
    init.month = 7;
    init.year = 25;
    rtc_load(init);

    // // Escribo EEPROM
    // uint8_t data[8];
    // data[0] = 'h';
    // data[1] = 'o';
    // data[2] = 'l';
    // data[3] = 'a';
    // data[4] = 'P';
    // data[5] = 'I';
    // data[6] = 'C';
    // data[7] = 'O';
    // uint16_t eeprom_address = 0x0010;
    // eeprom_write(data, eeprom_address, 8);

    // Init LED RUN
    gpio_init(LED_RUN_PIN);
    gpio_set_dir(LED_RUN_PIN, true);
    gpio_put(LED_RUN_PIN, LED_OFF);

    // Init LED 1
    gpio_init(LED1_PIN);
    gpio_set_dir(LED1_PIN, true);
    gpio_put(LED1_PIN, LED_OFF);

    // Init LED 2
    gpio_init(LED2_PIN);
    gpio_set_dir(LED2_PIN, true);
    gpio_put(LED2_PIN, LED_OFF);

    // Init ALARMA LO
    gpio_init(AL_LO_PIN);
    gpio_set_dir(AL_LO_PIN, true);
    gpio_put(AL_LO_PIN, LED_OFF);

    // Init ALARMA HI
    gpio_init(AL_HI_PIN);
    gpio_set_dir(AL_HI_PIN, true);
    gpio_put(AL_HI_PIN, LED_OFF);

    // Init BUZZER
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, true);
    gpio_put(BUZZER_PIN, LED_OFF);

    // Creo semaforos binarios
    vSemaphoreCreateBinary(sPULS);
    vSemaphoreCreateBinary(sA);
    vSemaphoreCreateBinary(sB);
    vSemaphoreCreateBinary(sEwrite);
    vSemaphoreCreateBinary(sEread);
    
    // Creo mutex
    mI2C = xSemaphoreCreateMutex();

    // Creo queues
    qLCD = xQueueCreate(1, sizeof(qLCD_t));
    qLUX = xQueueCreate(1, sizeof(qLUX_t));
    qPWM = xQueueCreate(1, sizeof(float));
    qEread = xQueueCreate(1, sizeof(rtc_t));
    qEwrite = xQueueCreate(1, sizeof(rtc_t));

    // Creo tareas
    xTaskCreate(task_TestHardware, "TEST", 2*configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_BH1750, "BH1750", configMINIMAL_STACK_SIZE, NULL, 2, &tBH1750);
    xTaskCreate(task_PWM, "PWM", configMINIMAL_STACK_SIZE, NULL, 2, &tPWM);
    xTaskCreate(task_LCD, "LCD", configMINIMAL_STACK_SIZE, NULL, 3, &tLCD);
    xTaskCreate(task_EEPROM, "EEPROM", configMINIMAL_STACK_SIZE, NULL, 3, &tEEPROM);
    xTaskCreate(task_encoder, "Encoder", configMINIMAL_STACK_SIZE, NULL, 5, &tEncoder);
    xTaskCreate(task_pulsador, "Pulsador", configMINIMAL_STACK_SIZE, NULL, 4, &tPulsador);
    xTaskCreate(task_LedRun, "LEDRUN", configMINIMAL_STACK_SIZE, NULL, 4, &tRUN);

    // Enciendo el scheduler
    vTaskStartScheduler();
    while (true);
}