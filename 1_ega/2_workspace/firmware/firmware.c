#include <stdio.h>
#include <string.h>
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

// PINOUT
#define I2C1_SDA_PIN    18
#define I2C1_SCL_PIN    19
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
// CONSTANTES
#define LUX_TIME        25

#define DEBOUNCE_TIME   20
#define I2C1_FREQ       100000
#define LCD_ADDR        0x27
#define MENU_SIZE       7
#define EEPROM_QUEUE_SIZE   1
#define EEPROM_DATA_BASE    0x20
#define EEPROM_DATA_SIZE    0x10

#define DUMP_SIZE       15

#define PULS_RISE       1
#define ENC_A_RISE      2
#define ENC_B_RISE      3
#define ENC_A_FALL      4
#define ENC_B_FALL      5
// MACROS SALIDAS DIGITALES
#define LED1_ON         gpio_put(LED1_PIN, false)
#define LED1_OFF        gpio_put(LED1_PIN, true)
#define LED2_ON         gpio_put(LED2_PIN, false)
#define LED2_OFF        gpio_put(LED2_PIN, true)
#define ALARMA_LO_ON    gpio_put(AL_LO_PIN, false)
#define ALARMA_LO_OFF   gpio_put(AL_LO_PIN, true)
#define ALARMA_HI_ON    gpio_put(AL_HI_PIN, false)
#define ALARMA_HI_OFF   gpio_put(AL_HI_PIN, true)

// Handles de tareas
TaskHandle_t tControl, tLuxo, tPWM, tConfig;
// Queues de datos inter tarea
QueueHandle_t qIRQ, qCTRL, qPWM, qLUX, qLCD, qRTC;
// Queues de manejo de eeprom
QueueHandle_t qEcontrol;
// Queues de datos de eeprom
QueueHandle_t qEwrite, qEwritecalib, qEread, qEreadcalib, qEdump;
// Mutex bus I2C1
SemaphoreHandle_t mI2C;
// Semaforos de control
SemaphoreHandle_t sRTC, sRefresh, sCalib, sEfull, sEempty;
// Semaforos counting
SemaphoreHandle_t sLux_show;

// ITEM DE QUEUE LCD
typedef struct lcd {
    char text[17];
    uint8_t line;
    uint8_t clear;
} qLCD_t;

// ESTRUCTURA BASE DE SETTINGS
typedef struct settings {
    uint16_t index; // 2B
    uint16_t setpoint; // 2B
    uint16_t user_max; // 2B
    uint16_t user_min; // 2B
    uint8_t curva; // 1B
    rtc_t time; // 7B
} settings_t;

// // BUFFER DE SETTINGS
// typedef union {
//     settings_t settings;
//     uint8_t packet[16];
// } settings_buffer_t;

// ESTRUCTURA DE MENU
typedef struct menu {
    const char* texto;
    uint16_t valor;
    uint16_t min;
    uint16_t max;
} menu_t;

typedef enum ecmd {
    ECTRL_WRITE_SETTINGS,
    ECTRL_READ_SETTINGS,
    ECTRL_WRITE_CALIB,
    ECTRL_READ_CALIB,
    ECTRL_DUMP,
    ECTRL_ERASE
} eeprom_cmd_t;

// ESTRUCTURA DE PARAMETROS CALIBRACION
typedef struct calib {
    float kp;
    float ti;
    float td;
    //float alpha_r;
    //float alpha_l;
    uint16_t lux_max;
    uint16_t lux_min;
} calibration_t;

// Estructura para dump request
typedef struct dumpreq {
    QueueHandle_t responseQueue;
    // uint16_t start_address;
    uint16_t length;
} eepromDumpRequest_t;

typedef struct lut {
    float duty;
    uint8_t lux;
} lut_t;

volatile uint8_t toggle = 0;
volatile uint8_t toggle2 = 0;
lut_t lut[101];

void settings2bytes(settings_t *settings, uint8_t *bytes)
{
    bytes[0] = (uint8_t)(((settings->index)>>8) & 0x00FF);
    bytes[1] = (uint8_t)(settings->index & 0x00FF);

    bytes[2] = (uint8_t)(((settings->setpoint)>>8) & 0x00FF);
    bytes[3] = (uint8_t)(settings->setpoint & 0x00FF);
    
    bytes[4] = (uint8_t)(((settings->user_max)>>8) & 0x00FF);
    bytes[5] = (uint8_t)(settings->user_max & 0x00FF);
    
    bytes[6] = (uint8_t)(((settings->user_min)>>8) & 0x00FF);
    bytes[7] = (uint8_t)(settings->user_min & 0x00FF);
    
    bytes[8] = settings->curva;
    
    bytes[9] = settings->time.sec;
    bytes[10] = settings->time.min;
    bytes[11] = settings->time.hour;
    bytes[12] = settings->time.weekday;
    bytes[13] = settings->time.day;
    bytes[14] = settings->time.month;
    bytes[15] = settings->time.year;
}

void bytes2settings(settings_t *settings, uint8_t *bytes)
{
    settings->index = ((bytes[0]<<8) & 0xFF00) | bytes[1];
    settings->setpoint = ((bytes[2]<<8) & 0xFF00) | bytes[3];
    settings->user_max = ((bytes[4]<<8) & 0xFF00) | bytes[5];
    settings->user_min = ((bytes[6]<<8) & 0xFF00) | bytes[7];
    
    settings->curva = bytes[8];
    settings->time.sec = bytes[9];
    settings->time.min = bytes[10];
    settings->time.hour = bytes[11];
    settings->time.weekday = bytes[12];
    settings->time.day = bytes[13];
    settings->time.month = bytes[14];
    settings->time.year = bytes[15];
}

void irq_gpio(uint gpio, uint32_t events)
{
    static BaseType_t taskWoken = pdFALSE;
    uint8_t cmd;
    // Detectar flanco ascendente
    if (events & GPIO_IRQ_EDGE_RISE) {
        switch (gpio) {
            case PULS_PIN: cmd = PULS_RISE; break;
            case ENC_A_PIN: cmd = ENC_A_RISE; break;
            case ENC_B_PIN: cmd = ENC_B_RISE; break;
        }
    }

    // Detectar flanco descendente
    if (events & GPIO_IRQ_EDGE_FALL) {
        switch (gpio) {
            case ENC_A_PIN: cmd = ENC_A_FALL; break;
            case ENC_B_PIN: cmd = ENC_B_FALL; break;
        }
    }

    if (cmd) {
        xQueueSendFromISR(qIRQ, &cmd, &taskWoken);
        portYIELD_FROM_ISR(taskWoken);
    }
}

void task_Encoder(void *pvParams)
{
    uint8_t irq;
    char comando;
    while(1){
        if(xQueueReceive(qIRQ, &irq, portMAX_DELAY) == pdPASS){
            switch (irq)
            {
            case PULS_RISE:
                comando = 'P';
                break;
            case ENC_A_RISE:
                //vTaskDelay(DEBOUNCE_TIME);
                if(gpio_get(ENC_B_PIN)) comando = 'H';
                else comando = 'A';
                break;
            case ENC_A_FALL:
                //vTaskDelay(DEBOUNCE_TIME);
                if(gpio_get(ENC_B_PIN)) comando = 'A';
                else comando = 'H';
                break;
            case ENC_B_RISE:
                //vTaskDelay(DEBOUNCE_TIME);
                if(gpio_get(ENC_A_PIN)) comando = 'A';
                else comando = 'H';
                break;
            case ENC_B_FALL:
                //vTaskDelay(DEBOUNCE_TIME);
                if(gpio_get(ENC_A_PIN)) comando = 'H';
                else comando = 'A';
                break;
            default:
                comando = 0;
                break;
            }
            xQueueSend(qCTRL, &comando, portMAX_DELAY);
        }
    }
}

void task_RTC(void *pvParams)
{
    rtc_t time;
    while(1){
        // Si llega la orden para leer hora
        if(xSemaphoreTake(sRTC, portMAX_DELAY) == pdPASS){
            // Leo la hora en el rtc
            if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                rtc_read(&time);
                xSemaphoreGive(mI2C);
                xQueueSend(qRTC, &time, portMAX_DELAY);
            }
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
                // Si clear esta en 1, limpio la pantalla
                if(bf.clear){
                    lcd_clear();
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                // Escribo linea
                lcd_set_cursor(bf.line, 0);
                lcd_string(bf.text);
                xSemaphoreGive(mI2C);
            }
            // Delay de procesamiento
            vTaskDelay(pdMS_TO_TICKS(10));
        }        
    }
}

void task_EEPROM(void *pvParams)
{
    eeprom_cmd_t comando;
    settings_t settings;
    uint8_t bf[16];
    calibration_t calib;
    //uint8_t calib[16];
    eepromDumpRequest_t dump_q;
    //uint8_t bf_calib[16];
    uint16_t address = 0x0000;
    uint16_t index = 0;
    // DUMP Y CLEAR
    uint16_t ptr;
    uint16_t end;

    // LEVANTO EL NUMERO DE REGISTROS ESCRITOS EN EEPROM
    taskENTER_CRITICAL();
    for(uint8_t i=0; i<255; i++){
        address = i*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
        eeprom_read(bf, address, 16);
        bytes2settings(&settings, bf);
        if (settings.index == 0xFFFF)
        {
            if(i>0) index = (uint16_t)(i-1);
            else index = 0;
            break;
        }
    }
    taskEXIT_CRITICAL();

    while(1){
        if(xQueueReceive(qEcontrol, &comando, portMAX_DELAY) == pdPASS){
            switch (comando)
            {
            case ECTRL_WRITE_SETTINGS:
                // Intento leer de la queue de escritura
                if(xQueueReceive(qEwrite, &settings, portMAX_DELAY) == pdPASS){
                    // Incremento index para agregar un nuevo registro
                    index++;
                    printf("\nRegistro N: %d\n", index);
                    // Calculo address
                    address = index*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                    // Desempaqueto
                    settings2bytes(&settings, bf);
                    // Intento tomar el bus
                    if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                        // Escribo settings en la eeprom
                        eeprom_write(bf, address, EEPROM_DATA_SIZE);
                        // delay de escritura de eeprom
                        vTaskDelay(pdMS_TO_TICKS(5));
                        //eeprom_write(settings, address, EEPROM_DATA_SIZE);
                        xSemaphoreGive(mI2C);
                    }
                }   
                break;

            case ECTRL_READ_SETTINGS:
                // Calculo address
                address = index*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                // Intento tomar el bus
                if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                    // Leo el ultimo registro de la eeprom
                    eeprom_read(bf, address, 16);
                    // eeprom_read(settings, address, 16);
                    xSemaphoreGive(mI2C);
                }
                // Empaqueto
                bytes2settings(&settings, bf);
                // Mando a la cola de lectura
                xQueueSend(qEread, &settings, portMAX_DELAY);
                break;

            case ECTRL_WRITE_CALIB:
                // Intento leer de la queue de escritura de calibracion
                if(xQueueReceive(qEwritecalib, &calib, 0) == pdPASS){
                    // Intento tomar el bus
                    if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                        // Escribo parametros de calibracion en la eeprom
                        //eeprom_write(calib, 0x0000, 16);
                        // delay de escritura de eeprom
                        vTaskDelay(pdMS_TO_TICKS(5));
                        xSemaphoreGive(mI2C);
                    }
                }  
                break;

            case ECTRL_READ_CALIB:
                // Intento tomar el bus
                if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                    // Leo parametros de calibracion de la eeprom
                    //eeprom_read(calib, 0x0000, 16);
                    xSemaphoreGive(mI2C);
                }
                // Mando a la cola de lectura de parametros del pid
                xQueueSend(qEreadcalib, &calib, portMAX_DELAY);
                break;

            case ECTRL_DUMP:
                ////////////////////////////////////////////////////////
                eeprom_read(bf, 0x0000, 16);
                memcpy(&calib, bf, sizeof(calibration_t));
                printf("\r\nKP= %f - KI= %f - KD= %f - LUXMAX= %d - luxmin= %d",
                    calib.kp, calib.ti, calib.td, calib.lux_max, calib.lux_min);
                fflush(stdout);
                // Paso el numero de items en memoria
                //dump_q.length = index;
                dump_q.length = DUMP_SIZE;
                // creo cola de volcado
                //dump_q.responseQueue = xQueueCreate(index, 16U);
                dump_q.responseQueue = xQueueCreate(DUMP_SIZE, 16U);
                // apunto al inicio de los settings
                ptr = EEPROM_DATA_BASE;
                // apunto al final de los settings
                //end = (index) * EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                end = DUMP_SIZE * EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                while (ptr < end) {
                    // leo datos y encolo
                    eeprom_read(bf, ptr, 16);
                    bytes2settings(&settings, bf);
                    if(xQueueSend(dump_q.responseQueue, &settings, portMAX_DELAY)==pdPASS){
                        ptr += EEPROM_DATA_SIZE;
                    }
                }
                xQueueSend(qEdump, &dump_q, portMAX_DELAY);
                ////////////////////////////////////////////////////////
                break;
            case ECTRL_ERASE:
                //////////////////////////////////////////////////////////////////////
                // EEPROM default erased value
                memset(bf, 0xFF, sizeof(bf));
                // apunto al inicio de los settings menos el primero
                ptr = EEPROM_DATA_BASE + EEPROM_DATA_SIZE;
                // apunto al final de los settings
                //end = index * EEPROM_DATA_SIZE + ptr;
                end = DUMP_SIZE * EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                while (ptr < end) {
                    eeprom_write(bf, ptr, 16);
                    // delay de escritura de eeprom
                    vTaskDelay(pdMS_TO_TICKS(10));
                    ptr += EEPROM_DATA_SIZE;
                }
                index = 0;
                xSemaphoreGive(sEempty);
                ///////////////////////////////////////////////////////////////////////
                break;
            default:
                break;
            }
        }
    }
}

void set_pwm(float duty, float alpha)
{
    //Cargo el slice fisico del pin
    uint slice_num = pwm_gpio_to_slice_num(PWM_OUT_PIN);
    //Frec de clock de la pico 2
    const float clock_freq = 125000000.f;              
    const float freq = 1000.0;
    const float divider = clock_freq / (freq * 8192);
    static float duty_f0 = 0.0;
    static float duty_f = 0.0;

    //Pongo el pin como pwm
    gpio_set_function(PWM_OUT_PIN, GPIO_FUNC_PWM);
    //Calculo con precision de 16 bits de la frec que se quiere--65536        
    //divider = clock_freq / (freq * 8192);    
    pwm_set_clkdiv(slice_num, divider);
    //16 bit de resolucion para el conteo maximo del ciclo de PWM--65535
    pwm_set_wrap(slice_num, 8191);                 
    //Calcula el duty cycle--65536

    // Filtro EMA - RESPUESTA DE LA PLANTA
    duty_f = alpha*duty + (1 - alpha)*duty_f0;
    duty_f0 = duty_f;
    
    // CLAMP
    if(duty_f > 0.9) duty_f = 0.9;
    if(duty_f < 0.1) duty_f = 0.1;

    pwm_set_gpio_level(PWM_OUT_PIN, duty_f * 8192);
    // Habilita el PWM en el pin            
    pwm_set_enabled(slice_num, true);
}

void setup_pwm(float duty)
{
    //Cargo el slice fisico del pin
    uint slice_num = pwm_gpio_to_slice_num(PWM_OUT_PIN);
    //Frec de clock de la pico 2
    const float clock_freq = 125000000.f;              
    const float freq = 1000.0;
    const float divider = clock_freq / (freq * 8192);
    //static float duty = 0.0;
    static float duty0 = 0.0;
    static float duty_f = 0.0;

    //Pongo el pin como pwm
    gpio_set_function(PWM_OUT_PIN, GPIO_FUNC_PWM);
    //Calculo con precision de 16 bits de la frec que se quiere--65536        
    //divider = clock_freq / (freq * 8192);    
    pwm_set_clkdiv(slice_num, divider);
    //16 bit de resolucion para el conteo maximo del ciclo de PWM--65535
    pwm_set_wrap(slice_num, 8191);                 
    //Calcula el duty cycle--65536

    pwm_set_gpio_level(PWM_OUT_PIN, duty * 8192);
    // Habilita el PWM en el pin            
    pwm_set_enabled(slice_num, true);
}

void autotune(void)
{
    qLCD_t lcd;
    // respuesta de lectura de queue
    static BaseType_t rx;
    // comandos de eeprom
    static eeprom_cmd_t ecmd;
    // comando del encoder
    char opc;
    // logica de la funcion
    char salir;
    char ajuste_Tu;
    salir = 0;
    ajuste_Tu = 1;
    // contador
    uint16_t i = 0;
    // Constantes de filtro de planta
    static float alpha_r = 0.4;
    static float alpha_l = 0.1;
    // Temporizadores
    TickType_t last_tick;
    TickType_t ticks;
    //////////////////////////////////
    ///// RUTINA DE AUTOTUNE PID /////
    //////////////////////////////////
    // Medicion
    static uint16_t lux = 0;
    // Variables del algoritmo
    float ku = 0.0;
    float tu = 1.0;
    float tu_old = 1.0;
    uint16_t a = 0;
    float d = 0.3;
    // Variables PID
    float kp = 0.0005;
    float ti = 5.0;
    float td = 0.0;
    // Variables de control
    float duty;
    float duty_f0;
    float duty_f;
    duty_f0 = 0.0;
    duty_f = 0.0;

    // Contador de semiperiodo
    uint32_t n;

    // ALGORITMO
    while(true){
        // Reinicio valores de salida y lectura pico
        a = 0;
        duty = 0.5;
        duty_f = 0.5;
        duty_f0 = 0.5;
        setup_pwm(1-duty);
        vTaskDelay(pdMS_TO_TICKS(1000));
        duty = 0.9;
        // Muestro valor de TU en LCD
        sprintf(lcd.text, "Tu =");
        lcd.line = 0;
        lcd.clear = 1;
        ///// -> LCD
        xQueueSend(qLCD, &lcd, portMAX_DELAY);
        taskYIELD();
        sprintf(lcd.text, "%3.1f", tu);
        lcd.line = 1;
        lcd.clear = 0;
        ///// -> LCD
        xQueueSend(qLCD, &lcd, portMAX_DELAY);
        taskYIELD();

        // ENTRO EN SECCION BAREMETAL
        // taskENTER_CRITICAL();
        vTaskResume(tLuxo);
        /////////////////////////////
        duty = 0.9;
        duty_f = alpha_r*duty + (1 - alpha_r)*duty_f0;
        duty_f0 = duty_f;
        setup_pwm(1-duty_f);
        // Cantidad de milisegundos de cada Tu/2
        n = (uint32_t)(500*tu);

        // Hago 3 periodos de oscilacion
        for(uint8_t j = 0; j<6; j++){
            printf("\nDuty = %.1f\n", duty);

            // Reinicio contador de semiperiodo
            last_tick = xTaskGetTickCount();
            ticks = 0;

            while(ticks < n){
                // Leo luxometro
                rx = xQueueReceive(qLUX, &lux, 0);
                if(rx == pdPASS) {
                    // Si es pico, actualizo a
                    if(lux > a) a = lux;
                    printf("Lux = %5d \t", lux);
                    vTaskDelay(pdMS_TO_TICKS(1));
                    // Saco la cuadrada + filtro de planta
                    // set_pwm(1-duty, alpha_r);
                    duty_f = alpha_r*duty + (1 - alpha_r)*duty_f0;
                    duty_f0 = duty_f;
                    setup_pwm(1-duty_f);
                }
                ticks = xTaskGetTickCount() - last_tick;
            }
            //printf("\nLux_pico = %5d", a);
            // Invierto si llegue al limite del semi periodo
            if(j%2) duty = 0.9;
            else duty = 0.1;
        }
        // SALGO DE SECCION BAREMETAL
        // taskEXIT_CRITICAL();
        vTaskSuspend(tLuxo);
        /////////////////////////////
        // Calculo constantes
        ku = (4*d)/(3.14*a);
        kp = 0.45*ku;
        ti = 0.83*tu;
        /////
        printf("\nTu = %.2f\ta = %5d\tKu =  %.5f\tKp = %.6f\t Ti = %.3f",
            tu, a, ku, kp, ti);
        // CAMBIO TU PARA NUEVO AJUSTE
        // SI TU = TU_OLD SALGO DEL AUTOTUNE
        tu_old = tu;
        ajuste_Tu = 1;
        while(ajuste_Tu){
            // Recibo comando del encoder
            xQueueReceive(qCTRL, &opc, portMAX_DELAY);
            switch(opc){
                case 'H':
                    tu += 0.05;
                    // Muestro valor de TU en LCD
                    sprintf(lcd.text, "%3.2f", tu);
                    lcd.line = 1;
                    lcd.clear = 0;
                    ///// -> LCD
                    xQueueSend(qLCD, &lcd, portMAX_DELAY);
                    taskYIELD();
                    break;
                case 'A':
                    if(tu > 0.05) tu -= 0.05;
                    // Muestro valor de TU en LCD
                    sprintf(lcd.text, "%3.2f", tu);
                    lcd.line = 1;
                    lcd.clear = 0;
                    ///// -> LCD
                    xQueueSend(qLCD, &lcd, portMAX_DELAY);
                    taskYIELD();
                    break;
                case 'P':
                    if(tu == tu_old) salir = 1;
                    ajuste_Tu = 0;
                    break;
                default:
                    break;
            }
        }
        if(salir) break;
    }
    //////////////////////////////////  
}

void log_settings(void)
{
    // Comando de eeprom
    eeprom_cmd_t ecmd;
    // Estructuras para manejo de dump
    eepromDumpRequest_t dump;
    settings_t settings;

    ///// DUMP DE SETTINGS POR CONSOLA /////
    ecmd = ECTRL_DUMP;
    xQueueSend(qEcontrol, &ecmd, portMAX_DELAY);
    taskYIELD();
    // Recibir datos y mostrarlos
    xQueueReceive(qEdump, &dump, portMAX_DELAY);
    // for (cantidad de items en dump.lenght)
    for (uint16_t i = 0; i < dump.length; i++){
        // Recibo de cola response con un timeout
        if(xQueueReceive(dump.responseQueue, &settings, pdMS_TO_TICKS(100)) == pdPASS){
            // Mientras pueda recibir de la cola, imprimo el registro
            printf("%d: (%02d/%02d %02d:%02d:%02d) Lux: %d, Max: %d, Min: %d, Curva: %d\n",
                settings.index, settings.time.day, settings.time.month,
                settings.time.hour, settings.time.min,
                settings.time.sec, settings.setpoint, settings.user_max,
                settings.user_min, settings.curva);
            fflush(stdout);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        // Si no hay mas registros, salgo
        else break;
    }
    // Borro cola para limpiar RAM
    vQueueDelete(dump.responseQueue);
}

void clear_settings(void)
{
    // Comando de eeprom
    eeprom_cmd_t ecmd;

    ecmd = ECTRL_ERASE;
    xQueueSend(qEcontrol, &ecmd, portMAX_DELAY);
    taskYIELD();
    // Esperar confirmación
    if(xSemaphoreTake(sEempty, portMAX_DELAY) == pdPASS){
        printf("Borrado completado.\r\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void task_Config(void *params)
{
    // respuesta de lectura de queue
    BaseType_t rx;
    // comandos de eeprom
    eeprom_cmd_t ecmd;
    // Estructura para manejo de dump
    eepromDumpRequest_t dump;
    // struct de hora
    rtc_t time = {0};
    // lineas del lcd
    qLCD_t lcd;
    // settings
    settings_t settings;
    // buffer eeprom
    uint8_t buffer[16];
    // valores del menu
    menu_t menu[MENU_SIZE] = {
        {"NIVEL LUX:", 100, 0, 5000},
        {"ALARMA MAX:", 1000, 0, 5000},
        {"ALARMA MIN:", 10, 0, 5000},
        {"CURVA:", 0, 0, 1},
        {"CALIBRACION?", 0, 0, 1},
        {"IMPRIMIR LOG?", 0, 0, 1},
        {"BORRAR LOG?", 0, 0, 1}
    };
    // indice del menu
    uint8_t indice = 0;
    // comando del encoder
    char opc;
    // mostrar nuevo item
    uint8_t show_menu;

    // Valores de prueba de menu y encoder
    //settings.lux = menu[0].valor;
    //settings.user_max = menu[1].valor;
    //settings.user_min = menu[2].valor;
    //settings.curva = (uint8_t) menu[3].valor;
    // Cargo buffer de prueba
    //settings2bytes(&settings, buffer);
    // Cargo valores iniciales en eeprom (prueba)
    
    uint16_t address = 0;
    //vTaskSuspendAll();
    //eeprom_write(buffer, 0, 16);
    //xTaskResumeAll();

    while(1){
        // Si toco el pulsador entro en el menu de configuracion
        rx = xQueueReceive(qCTRL, &opc, portMAX_DELAY);
        if(rx == pdPASS && opc == 'P'){
            // ENTRO EN CONFIGURACION
            
            // Suspendo tareas
            vTaskSuspend(tControl);
            vTaskSuspend(tLuxo);
            // Levanto la config actual de la eeprom
            ecmd = ECTRL_READ_SETTINGS;
            xQueueSend(qEcontrol, &ecmd, portMAX_DELAY);
            // Cambio de contexto para leer
            taskYIELD();
            // settings <- EEPROM
            //xQueueReceive(qEread, buffer, portMAX_DELAY);
            xQueueReceive(qEread, &settings, portMAX_DELAY);
            ////// PRUEBA DE EEPROM
            //vTaskSuspendAll();
            //eeprom_read(buffer, address, 16);
            //xTaskResumeAll();
            //for(uint8_t i=0; i<16; i++) printf("%x ", buffer[i]);
            //printf("\n");
            // Desempaqueto
            //bytes2settings(&settings, buffer);
            // Imprimo lectura
            printf("\n%d: (%02d/%02d %02d:%02d:%02d) Lux: %d, Max: %d, Min: %d, Curva: %d",
                            settings.index, settings.time.day, settings.time.month,
                            settings.time.hour, settings.time.min,
                            settings.time.sec, settings.setpoint, settings.user_max,
                            settings.user_min, settings.curva);
            // Cargo settings actuales al menu
            menu[0].valor = settings.setpoint;
            menu[1].valor = settings.user_max;
            menu[2].valor = settings.user_min;
            menu[3].valor = (uint16_t) settings.curva;
            menu[4].valor = 0; // No hago calibracion salvo que especifique
            menu[5].valor = 0;
            menu[6].valor = 0;
        
            // limpio variable
            opc = 0;
            // indice del menu = 0
            indice = 0;
            // muestro primer menu
            show_menu = 1;
            
            // Entro en el loop de ejecucion del menu
            while (indice < MENU_SIZE)
            {
                if(show_menu){
                    // Muestro titulo del menu en lcd
                    sprintf(lcd.text, "%s", menu[indice].texto);
                    lcd.line = 0;
                    lcd.clear = 1;
                    ///// -> LCD
                    xQueueSend(qLCD, &lcd, portMAX_DELAY);
                    taskYIELD();
                    // Muestro valor actualizado en LCD
                    lcd.line = 1;
                    lcd.clear = 0;
                    //sprintf(lcd.text, "%6d", menu[indice].valor);
                    if(indice < 3) sprintf(lcd.text, "%6d", menu[indice].valor);
                    else if(indice == 3) sprintf(lcd.text, " LENTA");
                    else sprintf(lcd.text, "    NO");

                    ///// -> LCD
                    xQueueSend(qLCD, &lcd, portMAX_DELAY);
                    taskYIELD();
                    // limpio variable
                    show_menu = 0;
                }
                //printf("Indice: %d, Opc: %c, Show: %d", indice, opc, show_menu);
                // Recepcion no bloqueante (NO HACER PEEK)
                rx = xQueueReceive(qCTRL, &opc, portMAX_DELAY);
                // Si no llega un comando no lo proceso,
                // Continuo con la proxima iteracion
                //if(rx != pdPASS) continue;
                //if(rx == pdPASS) printf("Comando recibido: %c\n", opc);
                // Comando
                switch (opc){
                case 'H':
                    // INCREMENTAR si menor al maximo
                    if(menu[indice].valor < menu[indice].max){
                        menu[indice].valor++;
                        // Muestro valor actualizado en LCD
                        lcd.line = 1;
                        lcd.clear = 0;
                        //sprintf(lcd.text, "%4d", menu[indice].valor);
                        if(indice < 3) sprintf(lcd.text, "%6d", menu[indice].valor);
                        else if(indice == 3) sprintf(lcd.text, " LENTA");
                        else sprintf(lcd.text, "    SI");
                        ///// -> LCD
                        xQueueSend(qLCD, &lcd, portMAX_DELAY);
                        taskYIELD();
                    }
                    opc = 0;
                    break;
                case 'A':
                    // DECREMENTAR si mayor al minimo
                    if(menu[indice].valor > menu[indice].min){
                        menu[indice].valor--;
                        // Muestro valor actualizado en LCD
                        lcd.line = 1;
                        lcd.clear = 0;
                        //sprintf(lcd.text, "%4d", menu[indice].valor);
                        if(indice < 3) sprintf(lcd.text, "%6d", menu[indice].valor);
                        else if(indice == 3) sprintf(lcd.text, "RAPIDA");
                        else sprintf(lcd.text, "    NO");
                        ///// -> LCD
                        xQueueSend(qLCD, &lcd, portMAX_DELAY);
                        taskYIELD();
                    }
                    opc = 0;
                    break;
                case 'P':
                    // FUNCIONES ESPECIALES
                    if(indice == 4 && menu[4].valor){
                        // RUTINA DE CALIBRACION
                        autotune();
                    }
                    else if(indice == 5  && menu[5].valor){
                        // IMPRIMO LOG DE SETTINGS
                        log_settings();
                    }
                    else if(indice == 6  && menu[6].valor){
                        // BORRO SETTINGS
                        clear_settings();
                    }
                    // Salto al proximo item del menu
                    indice++;
                    // Mostrar nueva pagina del menu
                    show_menu = 1;
                    opc = 0;
                    break;
                default:
                    break;
                }
            }
             // Si calibracion == 1
            //xSemaphoreGive(sCalib);
            //taskYIELD();

            // Leo la hora en el rtc
            xSemaphoreGive(sRTC);
            taskYIELD();
            xQueueReceive(qRTC, &time, portMAX_DELAY);
            settings.time = time;

            // GUARDANDO CONFIG ... -> LCD
            sprintf(lcd.text, "GUARDANDO");
            lcd.line = 0;
            lcd.clear = 1;
            xQueueSend(qLCD, &lcd, portMAX_DELAY);
            taskYIELD();
            sprintf(lcd.text, "CONFIGURACION");
            lcd.line = 1;
            lcd.clear = 0;
            xQueueSend(qLCD, &lcd, portMAX_DELAY);
            taskYIELD();
            
            // cargo menu actualizado a settings
            settings.index++;
            settings.setpoint = menu[0].valor;
            settings.user_max = menu[1].valor;
            settings.user_min = menu[2].valor;
            settings.curva = (uint8_t) menu[3].valor;

            //address = num_registro*EEPROM_DATA_SIZE;
            // Empaqueto settings
            //settings2bytes(&settings, buffer);
            // settings -> EEPROM
            //xQueueSend(qEwrite, buffer, portMAX_DELAY);
            xQueueSend(qEwrite, &settings, portMAX_DELAY);
            ecmd = ECTRL_WRITE_SETTINGS;
            xQueueSend(qEcontrol, &ecmd, portMAX_DELAY);
            taskYIELD();
            ///// PRUEBA DE EEPROM
            //vTaskSuspendAll();
            //eeprom_write(buffer, address, 16);
            //xTaskResumeAll();

            // DELAY 1SEG
            vTaskDelay(pdMS_TO_TICKS(1000));
            // Doy la orden a task_Control para que refresque los datos
            xSemaphoreGive(sRefresh);
            // Saco de la suspension a task_Control
            vTaskResume(tControl);
            vTaskResume(tLuxo);
        }
    }   
}

void task_BH1750(void *pvParams)
{
    uint16_t lux = 0;
    TickType_t last_tick = xTaskGetTickCount();

    while(1){
        // Si puedo tomar el bus
        if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
            // Leo luxometro
            lux = bh1750_read();
            // Vuelvo a disparar la lectura
            bh1750_init(ONESHOT_LORES);
            xSemaphoreGive(mI2C);
            // Paso a la cola
            xQueueSend(qLUX, &lux, portMAX_DELAY);
        }
        // Corre cada LUX_TIME
        vTaskDelayUntil(&last_tick, pdMS_TO_TICKS(LUX_TIME));
        //vTaskDelay(pdMS_TO_TICKS(LUX_TIME));
    }
}

void task_Control(void *pvParams)
{
    // comandos de eeprom
    eeprom_cmd_t ecmd;
    // Settings
    settings_t settings;
    calibration_t calib;
    uint16_t lux;
    float lux_acc = 0.0;
    qLCD_t lcd;
    rtc_t time;
    uint8_t prev_sec;
    uint8_t bf[7];
    uint8_t show_lux = 0;
    //////////////////////////
    ///// VARIABLES PID //////
    float ts = (float) LUX_TIME;
    // constantes pid
    float kp = 0.0002;
    // float kp_lento = 0.00005;
    //float ti = 5.0;
    //float td = 0.0;
    float ki = 0.0001;
    //float ki_lento = 0.00001;
    float kd = 0.00001;
    // acciones pid
    float ap = 0.0;
    float ai = 0.0;
    float ad = 0.0;
    // error absoluto y relativo
    float error = 0.0;
    float eR = 0.0;
    // anteriores
    float ai0 = 0.0;
    float error0 = 0.0;
    // filtro de muestra
    float alpha_m = 0.4;
    float lux_f = 0.0;
    float lux_f0 = 0.0;
    // salida controlador
    float alpha_p = 0.7;
    //float alpha_lento = 0.1;
    float duty = 0.5;
    float duty0 = 0.0;
    float duty_f = 0.0;
    ///////////////////////////
    uint16_t setpoint = 1000;
    TickType_t last_tick = xTaskGetTickCount();
    TickType_t tick;
    TickType_t dif_ticks;

    while(1){
        // Actualizar seteos y calibracion ? (NO BLOQUEANTE)
        if(xSemaphoreTake(sRefresh, 0)){
            // Levanto valores de la eeprom
            ecmd = ECTRL_READ_SETTINGS;
            xQueueSend(qEcontrol, &ecmd, portMAX_DELAY);
            taskYIELD();
            xQueueReceive(qEread, &settings, portMAX_DELAY);
            //ecmd = ECTRL_READ_CALIB;
            //xQueueSend(qEcontrol, &ecmd, portMAX_DELAY);
            //taskYIELD();
            //xQueueReceive(qEreadcalib, &calib, portMAX_DELAY);

            // Calculo de constantes integral y derivativa
            if(settings.curva == 0){
                // PLANTA RAPIDA - PID
                alpha_p = 0.7;
                kp = 0.0002;
                ki = 0.0001;
                kd = 0.00001;
            }
            else {
                // PLANTA LENTA - PI
                alpha_p = 0.1;
                kp = 0.00005;
                ki = 0.00001;
                kd = 0.0;
            }
            //ki = kp/ti;
            //kd = kp*td;
        }
        // Hay lectura del luxometro ?
        // No lo hago bloqueante para que pueda imprimir dentro del tiempo de medicion
        if(xQueueReceive(qLUX, &lux, 0) == pdPASS){
            //////////////////////////////////////////
            ///// CONTROL PID ////////////////////////
            // ap = kp*error;
            // ai = (kp*ts/ti)*(error+error0) + ai0;
            // ad = (kp*td/ts)*(error-error0);
            // duty = ap + ai + ad;
            //////////////////////////////////////////

            // Filtro EMA 2 - Suavizado de muestra
            lux_f = alpha_m*lux + (1 - alpha_m)*lux_f0;
            lux_f0 = lux_f;

            // CALCULO DEL ERROR
            error = (float)(settings.setpoint - lux_f);

            // BANDA DE ERROR
            eR = error / settings.setpoint;
            // Leds debug muestran banda de error al 5%
            if(eR > 0.05) LED1_ON;
            else LED1_OFF;
            if(eR < -0.05) LED2_ON;
            else LED2_OFF;

            // ACCIONES PID
            ap = kp*error;
            ai = ki*(error+error0) + ai0;
            ad = kd*(error-error0);

            // ANTIWINDUP AI
            if(ai > 1.0) ai = 1.0;
            if(ai < 0.0) ai = 0.0;

            // valores actuales -> anteriores
            error0 = error;
            ai0 = ai;

            // SALIDA DEL CONTROLADOR PID
            duty = ap + ai + ad;

            // Filtro EMA - suavizado de duty
            // CONTROLA LA RESPUESTA DE LA PLANTA
            duty_f = alpha_p*duty + (1 - alpha_p)*duty0;
            duty0 = duty_f;

            // CLAMP + ALARMAS
            if(duty_f > 0.95){
                // Enciendo alarma de limite de ajuste
                ALARMA_LO_ON;
                // clamp
                duty_f = 0.95;
            }
            else ALARMA_LO_OFF;

            if(duty_f < 0.10){
                // Enciendo alarma de limite de ajuste
                ALARMA_HI_ON;
                // clamp
                duty_f = 0.10;
            }
            else ALARMA_HI_OFF;

            // ACTUALIZO EL DUTY
            // IMPORTANTE: 0 -> MAX / 1 -> MIN
            setup_pwm(1 - duty_f);

            //last_tick = tick;
            //tick = xTaskGetTickCount();
            //dif_ticks = tick - last_tick;
            //printf("\rLUX = %5d\teR = %.2f\tAP = %.3f\tAI = %.3f\tAD = %.3f\tduty = %2.2f\t%4dms", 
            //    lux, 100*eR, ap, ai, ad, duty_f, dif_ticks);
            // printf("\nd_Ticks: %5dms - LUX= %d - duty = %.2f", dif_ticks, lux, duty_f);

            // Acumulo lux para mostrar en lcd
            show_lux++;
            lux_acc += (float) lux; 
        }

        //Mostrar promedio de medicion de lux cada 10 muestras
        if(show_lux > 9){
            lux_acc = lux_acc / show_lux;
            lux = (uint16_t) lux_acc;
            lux_acc = 0.0;
            // Armo el texto para el LCD
            sprintf(lcd.text, "LUX: %5d", lux);
            lcd.line = 0;
            lcd.clear = 0;
            // Envio a la cola del lcd
            xQueueSend(qLCD, &lcd, portMAX_DELAY);
            // Fuerzo cambio de contexto
            taskYIELD();
            // Reseteo contador
            show_lux = 0;
        }

        // Guardo ultima lectura de segundos
        prev_sec = time.sec;
        // Leo la hora en el rtc
        xSemaphoreGive(sRTC);
        taskYIELD();
        xQueueReceive(qRTC, &time, portMAX_DELAY);
        // muestro la ultima hora si time.sec cambio
        if(time.sec != prev_sec){
            sprintf(lcd.text, "%02d/%02d %02d:%02d:%02d", time.day, time.month, 
                            time.hour, time.min, time.sec);
            lcd.line = 1;
            lcd.clear = 0;
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
        // PPS
        if(toggle) toggle = 0;
        else toggle = 1;
    }
}

void task_Setup(void *pvParams)
{
    while(1){
        
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
    gpio_set_irq_enabled_with_callback(PULS_PIN, GPIO_IRQ_EDGE_RISE, true, &irq_gpio);
    gpio_set_irq_enabled(ENC_A_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(ENC_B_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    // I2C INIT
    i2c_init(i2c1, I2C1_FREQ);
    gpio_set_function(I2C1_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C1_SCL_PIN, GPIO_FUNC_I2C);
    // Pongo pullups a 3V3
    //gpio_pull_up(I2C1_SDA_PIN);
    //gpio_pull_up(I2C1_SCL_PIN);

    // Inicializo LCD
    lcd_init(i2c1, LCD_ADDR);
    lcd_clear();

    // Inicializo BH1750
    // bh1750_init(CONT_LORES);

    // float d;
    // // Lleno tabla lut
    // for(uint8_t n=0; n<101; n++){
    //     d = (float) n/100;
    //     setup_pwm(d);
    //     sleep_ms(25);
    //     lut[n].duty = d;
    //     lut[n].lux = bh1750_read();
    // }

    bh1750_init(ONESHOT_LORES);

    // Inicializo DS3231
    rtc_t init;
    init.sec = 00;
    init.min = 10;
    init.hour = 20;
    init.weekday = 4;
    init.day = 11;
    init.month = 8;
    init.year = 25;
    rtc_load(init);

    // Escribo EEPROM
    uint8_t data[16];
    settings_t settings;
    settings.index = 0;
    settings.setpoint = 1000;
    settings.user_max = 1500;
    settings.user_min = 500;
    settings.curva = 0;
    settings.time = init;
    settings2bytes(&settings, data);
    eeprom_write(data, EEPROM_DATA_BASE, 16);
    sleep_ms(10);

    calibration_t calib;
    calib.kp = 0.0005;
    calib.ti = 5.0;
    calib.td = 0.0;
    calib.lux_max = 5000;
    calib.lux_min = 1;
    memcpy(data, &calib, sizeof(calibration_t));
    eeprom_write(data, 0x0000, 16);
    sleep_ms(10);

    // Init LED RUN
    gpio_init(LED_RUN_PIN);
    gpio_set_dir(LED_RUN_PIN, true);
    gpio_put(LED_RUN_PIN, true);

    // Init LED 1
    gpio_init(LED1_PIN);
    gpio_set_dir(LED1_PIN, true);
    gpio_put(LED1_PIN, true);

    // Init LED 2
    gpio_init(LED2_PIN);
    gpio_set_dir(LED2_PIN, true);
    gpio_put(LED2_PIN, true);

    // Init ALARMA LO
    gpio_init(AL_LO_PIN);
    gpio_set_dir(AL_LO_PIN, true);
    gpio_put(AL_LO_PIN, true);

    // Init ALARMA HI
    gpio_init(AL_HI_PIN);
    gpio_set_dir(AL_HI_PIN, true);
    gpio_put(AL_HI_PIN, true);

    // Init BUZZER
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, true);
    gpio_put(BUZZER_PIN, false);

    // Creo semaforos binarios y los libero
    vSemaphoreCreateBinary(sRTC);
    xSemaphoreGive(sRTC);
    vSemaphoreCreateBinary(sRefresh);
    xSemaphoreGive(sRefresh);
    vSemaphoreCreateBinary(sCalib);
    xSemaphoreGive(sCalib);

    vSemaphoreCreateBinary(sEfull);
    vSemaphoreCreateBinary(sEempty);

    // Creo semaforos counting y los libero
    sLux_show = xSemaphoreCreateCounting(10, 0);
    
    // Creo mutex y lo libero
    mI2C = xSemaphoreCreateMutex();
    xSemaphoreGive(mI2C);

    // Creo queues
    qIRQ = xQueueCreate(1, sizeof(uint8_t));
    qCTRL = xQueueCreate(1, sizeof(char));
    qLCD = xQueueCreate(1, sizeof(qLCD_t));
    qRTC = xQueueCreate(1, sizeof(rtc_t));
    qLUX = xQueueCreate(1, sizeof(uint16_t));
    qPWM = xQueueCreate(1, sizeof(float));

    // Creo queue de comando de eeprom
    qEcontrol = xQueueCreate(1, sizeof(uint8_t));
    // Creo queues de datos de eeprom
    qEwrite = xQueueCreate(1, sizeof(settings_t));
    //qEwrite = xQueueCreate(1, 16U);
    //qEwritecalib = xQueueCreate(1, sizeof(calibration_t));
    qEwritecalib = xQueueCreate(1, 16U);
    qEread = xQueueCreate(1, 16U);
    //qEreadcalib = xQueueCreate(1, sizeof(calibration_t));
    qEreadcalib = xQueueCreate(1, 16U);
    qEdump = xQueueCreate(1, sizeof(eepromDumpRequest_t));

    // Creo tareas
    xTaskCreate(task_Control, "Control", 256, NULL, 1, &tControl);
    xTaskCreate(task_Config, "Setear", 1024, NULL, 2, &tConfig);
    xTaskCreate(task_LedRun, "Run", 128, NULL, 3, NULL);
    xTaskCreate(task_BH1750, "BH1750", 128, NULL, 4, &tLuxo);
    xTaskCreate(task_LCD, "LCD", 128, NULL, 4, NULL);
    xTaskCreate(task_RTC, "RTC", 128, NULL, 4, NULL);
    xTaskCreate(task_EEPROM, "EEPROM", 1024, NULL, 4, NULL);
    xTaskCreate(task_Encoder, "Encoder", 128, NULL, 5, NULL);
    //xTaskCreate(task_Setup, "SETUP", 1024, NULL, 6, NULL);

    // Enciendo el scheduler
    vTaskStartScheduler();
    while (true);
}