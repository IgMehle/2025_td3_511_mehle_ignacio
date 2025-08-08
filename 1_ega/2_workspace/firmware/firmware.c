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
#define DEBOUNCE_TIME   20
#define I2C1_FREQ       100000
#define LCD_ADDR        0x27
#define MENU_SIZE       5
#define EEPROM_QUEUE_SIZE   1
#define EEPROM_DATA_BASE    0x0010
#define EEPROM_DATA_SIZE    0x0010
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
TaskHandle_t tPWM, tBH1750, tEEPROM, tLCD, tEncoder, tPulsador, tRUN, tSetup;
// Queues de datos inter tarea
QueueHandle_t  qCTRL, qPWM, qLUX, qLCD, qEread, qEwrite;
// Queue Set de task_EEPROM
QueueSetHandle_t qsetEEPROM;
// Queues de manejo de eeprom
QueueHandle_t qEreadReq, qEcalibReq, qEdumpReq, qEclearReq;
// Queues de datos de eeprom
QueueHandle_t qEwrite, qEwritecalib, qEread, qEreadcalib, qElog;
// Mutex bus I2C1
SemaphoreHandle_t mI2C;
// Semaforos de control
SemaphoreHandle_t sPULS, sA, sB, sRefresh, sCalib;

// ITEM DE QUEUE LCD
typedef struct {
    char text[17];
    uint8_t line;
    uint8_t clear;
} qLCD_t;

#pragma pack(push, 1)
// ESTRUCTURA BASE DE SETTINGS (16B)
typedef struct {
    uint16_t index; // 2B
    uint16_t lux; // 2B
    uint16_t user_max; // 2B
    uint16_t user_min; // 2B
    char curva; // 1B
    rtc_t time; // 7B
} settings_t;

#pragma pack(pop)
// BUFFER DE SETTINGS
typedef union {
    settings_t settings;
    uint8_t packet[16];
} settings_buffer_t;

// ESTRUCTURA DE MENU
typedef struct {
    const char* texto;
    int valor;
    int min;
    int max;
} menu_t;

// ESTRUCTURA DE PARAMETROS CALIBRACION
typedef struct {
    uint8_t params;
} calibration_t;

// Estructura para read request
typedef struct {
    QueueHandle_t read_q;
} eepromReadRequest_t;

// Estructura para calibration request
typedef struct {
    QueueHandle_t calib_q;
} eepromCalibRequest_t;

// Estructura para dump request
typedef struct {
    QueueHandle_t responseQueue;
    uint16_t start_address;
    uint16_t length;
} eepromDumpRequest_t;

// Estructura para clear request
typedef struct {
    uint16_t start_address;
    uint16_t length;
    QueueHandle_t doneSignal; // puede ser NULL
} eepromClearRequest_t;

// // ----- STRINGS PARA IMPRIMIR EN LCD ----- //
// char text1[] = {"NIVEL LUX:"};
// char text2[] = {"ALARMA MAX:"};
// char text3[] = {"ALARMA MIN:"};
// char text4[] = {"AJUSTE:"};
// char text5[] = {"GRADUAL"};
// char text6[] = {"DIRECTO"};
// char text7[] = {"CALIBRACION?"};
// char text10[] = {"GUARDANDO..."};
// char text11[] = {"LUX: "};

void settings2bytes(settings_t *settings, uint8_t *bytes)
{
    bytes[0] = (uint8_t)(((settings->index)>>8) & 0x00FF);
    bytes[1] = (uint8_t)(settings->index & 0x00FF);
    bytes[2] = (uint8_t)(((settings->lux)>>8) & 0x00FF);
    bytes[3] = (uint8_t)(settings->lux & 0x00FF);
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
    settings->index = ((bytes[0]<<8) & 0xFF00) + bytes[1];
    settings->lux = ((bytes[2]<<8) & 0xFF00) + bytes[3];
    settings->user_max = ((bytes[4]<<8) & 0xFF00) + bytes[5];
    settings->user_min = ((bytes[6]<<8) & 0xFF00) + bytes[7];
    settings->curva = bytes[8];
    settings->time.sec = bytes[9];
    settings->time.min = bytes[10];
    settings->time.hour = bytes[11];
    settings->time.weekday = bytes[12];
    settings->time.day = bytes[13];
    settings->time.month = bytes[14];
    settings->time.year = bytes[15];
}

void irq_encoder(uint gpio, uint32_t events)
{
    static BaseType_t taskWoken = pdTRUE;
    switch(gpio){
        case PULS_PIN:
            xSemaphoreGiveFromISR(sPULS, &taskWoken);
            break;
        case ENC_A_PIN:
            xSemaphoreGiveFromISR(sA, &taskWoken);
            break;
        case ENC_B_PIN:
            xSemaphoreGiveFromISR(sB, &taskWoken);
            break;
        default:
            break;
    }
}

void task_Setup(void *pvParams)
{

}

void task_Pulsador(void *pvParams)
{
    char comando = 'P';
    while(1){
        // SEMAFORO PULSADOR
        if(xSemaphoreTake(sPULS, portMAX_DELAY) == pdPASS){
            xQueueOverwrite(qCTRL, &comando);
        }
    }
}

void task_EncoderA(void *pvParams)
{
    char comando = 'A';
    while(1){
        // SEMAFORO ENCODER A
        if(xSemaphoreTake(sA, portMAX_DELAY) == pdPASS){
            xQueueOverwrite(qCTRL, &comando);
        }
    }
}

void task_EncoderB(void *pvParams)
{
    char comando = 'H';
    while(1){
        // SEMAFORO ENCODER B
        if(xSemaphoreTake(sB, portMAX_DELAY) == pdPASS){
            xQueueOverwrite(qCTRL, &comando);
        }
    }
}

void task_PWM(void *pvParams)
{
    //Cargo el slice fisico del pin
    uint slice_num = pwm_gpio_to_slice_num(PWM_OUT_PIN);
    //Frec de clock de la pico 2
    float clock_freq = 125000000.f;              
    float freq = 1000.0;
    float divider = clock_freq / (freq * 8192);
    float duty = 0.0;

    while(1){
        if (xQueueReceive(qPWM, &duty, portMAX_DELAY) == pdPASS){
            //Pongo el pin como pwm
            gpio_set_function(PWM_OUT_PIN, GPIO_FUNC_PWM);
            //Calculo con precision de 16 bits de la frec que se quiere--65536        
            //divider = clock_freq / (freq * 8192);    
            pwm_set_clkdiv(slice_num, divider);
            //16 bit de resolucion para el conteo maximo del ciclo de PWM--65535
            pwm_set_wrap(slice_num, 8191);                 
            //Calcula el duty cycle--65536
            pwm_set_gpio_level(PWM_OUT_PIN, duty * 8192);
            // Habilitab el PWM en el pin            
            pwm_set_enabled(slice_num, true);                
        }
    }
}

void task_BH1750(void *pvParams)
{
    uint16_t lux = 0;
    while(1){
        // Si puedo tomar el bus
        // if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
        //     // Leo luxometro
        //     lux = bh1750_read();
        //     xSemaphoreGive(mI2C);
        //     // Paso a la cola
        //     xQueueOverwrite(qLUX, &lux);
        // }
        //////////////////////////////////////
        // Funcion de prueba
        lux = lux + 100;
        if(lux > 1000) lux = 0;
        xQueueOverwrite(qLUX, &lux);
        //////////////////////////////////////
        // Corre cada 150ms
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void task_EEPROM(void *pvParams)
{
    // request de datos
    QueueHandle_t read_q;
    QueueHandle_t calib_q;
    eepromDumpRequest_t dump_q;
    eepromClearRequest_t clear_q;
    //read_req.read_q = qEread;
    //calib_req.calib_q = qEreadcalib,
    //dump_req.log_q = qElog;
    //clear_req.clear_q = qEclear;
    // selector de queue
    QueueSetMemberHandle_t active_queue;

    settings_t settings;
    calibration_t calib;
    uint8_t bf[16], bf_calib[16];
    uint16_t address = 0x0000;
    uint16_t index = 0;

    uint8_t buffer[64];

    while(1){
        active_queue = xQueueSelectFromSet(qsetEEPROM, portMAX_DELAY);

        // ESCRITURA SETTINGS
        if (active_queue == qEwrite)
        {
            // Intento leer de la queue de escritura
            if(xQueueReceive(qEwrite, &settings, 0) == pdPASS){
                // Calculo address
                address = index*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                // Desempaqueto
                settings2bytes(&settings, bf);
                // Intento tomar el bus
                if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                    // Escribo settings en la eeprom
                    eeprom_write(bf, address, EEPROM_DATA_SIZE);
                    xSemaphoreGive(mI2C);
                }
            }   
        }
        // LECTURA SETTINGS
        else if (active_queue == qEreadReq){
            if(xQueueReceive(qEreadReq, &read_q, 0) == pdPASS){
                // Calculo address
                address = index*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                // Intento tomar el bus
                if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                    // Leo el ultimo registro de la eeprom
                    eeprom_read(bf, address, 16);
                    xSemaphoreGive(mI2C);
                }
                // Empaqueto
                bytes2settings(&settings, bf);
                // Mando a la cola de lectura
                xQueueSend(qEread, &settings, portMAX_DELAY);
                //xQueueSend(read_q, &bf, portMAX_DELAY);
            }
        }
        // ESCRITURA CALIBRACION
        if (active_queue == qEwritecalib)
        {
            // Intento leer de la queue de escritura de calibracion
            if(xQueueReceive(qEwritecalib, &bf_calib, 0) == pdPASS){
                // Intento tomar el bus
                if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                    // Escribo parametros de calibracion en la eeprom
                    eeprom_write(bf_calib, 0x0000, 16);
                    xSemaphoreGive(mI2C);
                }
            }   
        }
        // LECTURA CALIBRACION
        else if (active_queue == qEreadReq){
            if(xQueueReceive(qEcalibReq, &calib_q, 0) == pdPASS) {
                // Intento tomar el bus
                if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                    // Leo parametros de calibracion de la eeprom
                    eeprom_read(bf_calib, 0x0000, 16);
                    xSemaphoreGive(mI2C);
                }
                // Mando a la cola de lectura de parametros del pid
                xQueueSend(qEreadcalib, &bf_calib, portMAX_DELAY);
                //xQueueSend(calib_q, &bf_calib, portMAX_DELAY);
            }
        }
        // LOGGER DE DATOS
        else if (active_queue == qEdumpReq){
            if(xQueueReceive(qEdumpReq, &dump_q, 0) == pdPASS){
                ////////////////////////////////////////////////////////
                uint16_t offset = 0;
                while (offset < dump_q.length) {
                    uint16_t chunk = (dump_q.length - offset > sizeof(buffer)) ? sizeof(buffer) : dump_q.length - offset;
                    eeprom_read(buffer, dump_q.start_address + offset, chunk);
                    xQueueSend(dump_q.responseQueue, buffer, portMAX_DELAY);
                    offset += chunk;
                }
                ////////////////////////////////////////////////////////
            }
        }
        // BORRADO DE DATOS
        else if (active_queue == qEclearReq){
            if(xQueueReceive(qEclearReq, &clear_q, 0) == pdPASS){
                //////////////////////////////////////////////////////////////////////
                memset(buffer, 0xFF, sizeof(buffer)); // EEPROM default erased value
                uint16_t offset = 0;
                while (offset < clear_q.length) {
                    uint16_t chunk = (clear_q.length - offset > sizeof(buffer)) ? sizeof(buffer) : clear_q.length - offset;
                    eeprom_write(buffer, clear_q.start_address + offset, chunk);
                    offset += chunk;
                }
                if (clear_q.doneSignal) {
                    xQueueSend(clear_q.doneSignal, NULL, 0);
                }
                ///////////////////////////////////////////////////////////////////////
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
                if(bf.clear){
                    lcd_clear();
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                lcd_set_cursor(bf.line, 0);
                lcd_string(bf.text);
                xSemaphoreGive(mI2C);
            }
            // Delay de procesamiento
            vTaskDelay(pdMS_TO_TICKS(10));
        }        
    }
}

void task_Setear(void *params)
{
    // respuesta de lectura de queue
    BaseType_t rx;
    // request de lectura de datos
    QueueHandle_t read_req = qEread;
    // struct de hora
    rtc_t time = {0};
    // lineas del lcd
    qLCD_t lcd;
    // settings
    settings_t settings;
    uint16_t num_log = 0;
    // valores del menu
    menu_t menu[MENU_SIZE] = {
        {"NIVEL LUX:", 10, 0, 100},
        {"ALARMA MAX:", 99, 0, 100},
        {"ALARMA MIN:", 1, 0, 100},
        {"CURVA:", 0, 0, 1},
        {"CALIBRACION?", 0, 0, 1}
    };
    // indice del menu
    uint8_t indice = 0;
    // comando del encoder
    char opc;
    // mostrar nuevo item
    uint8_t show_menu;

    while(1){
        // Si toco el pulsador entro en el menu de configuracion
        rx = xQueueReceive(qCTRL, &opc, portMAX_DELAY);
        if(rx == pdPASS && opc == 'P'){
            // ENTRO EN CONFIGURACION

            // Levanto la config actual de la eeprom
            xQueueSend(qEreadReq, &read_req, portMAX_DELAY);
            // Cambio de contexto para leer
            taskYIELD();
             // settings <- EEPROM
            xQueueReceive(qEread, &settings, portMAX_DELAY);
            // Cargo settings actuales al menu
            menu[0].valor = settings.lux;
            menu[1].valor = settings.user_max;
            menu[2].valor = settings.user_min;
            menu[3].valor = settings.curva;
            menu[4].valor = 0; // No hago calibracion salvo que especifique
            num_log = settings.index;
        
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
                    xQueueSend(qLCD, &lcd, portMAX_DELAY);
                    taskYIELD();
                    // Muestro valor actualizado en LCD
                    sprintf(lcd.text, "%4d", menu[indice].valor);
                    lcd.line = 1;
                    lcd.clear = 0;
                    xQueueSend(qLCD, &lcd, portMAX_DELAY);
                    taskYIELD();
                    // limpio variable
                    show_menu = 0;
                }
                //printf("Indice: %d, Opc: %c, Show: %d/n", indice, opc, show_menu);
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
                        sprintf(lcd.text, "%4d", menu[indice].valor);
                        lcd.line = 1;
                        lcd.clear = 0;
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
                        sprintf(lcd.text, "%4d", menu[indice].valor);
                        lcd.line = 1;
                        lcd.clear = 0;
                        xQueueSend(qLCD, &lcd, portMAX_DELAY);
                        taskYIELD();
                    }
                    opc = 0;
                    break;
                case 'P':
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
            xSemaphoreGive(sCalib);
            taskYIELD();
            

            // Leo la hora en el rtc
            if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                rtc_read(&time);
                xSemaphoreGive(mI2C);
                settings.time = time;
            }
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

            // DELAY 1SEG
            vTaskDelay(pdMS_TO_TICKS(1000));
            // cargo menu actualizado a settings
            settings.lux = menu[0].valor;
            settings.user_max = menu[1].valor;
            settings.user_min = menu[2].valor;
            settings.user_max = menu[3].valor;
            settings.curva = menu[4].valor;
            // settings -> EEPROM
            xQueueSend(qEwrite, &settings, portMAX_DELAY);
            taskYIELD();
            // Doy la orden a task_Control parta que refresque los datos
            xSemaphoreGive(sRefresh);
        }
    }   
}

void task_Calibracion(void *pvParams)
{
    // respuesta de lectura de queue
    BaseType_t rx;
    // request de lectura de datos
    QueueHandle_t calib_req = qEreadcalib;
    while(1){
        if(xSemaphoreTake(sCalib, portMAX_DELAY) == pdPASS){
            // qEreadcalib <- EEPROM
            /////////////////
            // CALIBRACION //
            /////////////////
            // qEwritecalib -> EEPROM
            LED2_ON;
            vTaskDelay(pdMS_TO_TICKS(1000));
            LED2_OFF;
        }
    }
}

void task_Control(void *pvParams)
{
    // request de lectura de datos
    QueueHandle_t read_req = qEread;
    QueueHandle_t calib_req = qEreadcalib;
    // Settings
    settings_t settings;
    calibration_t calib;
    uint16_t lux;
    float duty;
    qLCD_t lcd;
    rtc_t time;
    uint8_t bf[7];

    while(1){
        // Actualizar seteos y calibracion ? (NO BLOQUEANTE)
        if(xSemaphoreTake(sRefresh, 0)){
            // Levanto valores de la eeprom
            xQueueSend(qEreadReq, &read_req, portMAX_DELAY);
            taskYIELD();
            xQueueReceive(qEread, &settings, portMAX_DELAY);
            xQueueSend(qEcalibReq, &calib_req, portMAX_DELAY);
            taskYIELD();
            xQueueReceive(qEreadcalib, &calib, portMAX_DELAY);
        }
        // Hay lectura del luxometro ?
        if(xQueueReceive(qLUX, &lux, portMAX_DELAY) == pdPASS){
            /////////////////
            // CONTROL PWM //
            /////////////////
            // Funcion de test de pwm
            duty = lux / 1000.0;
            //////////////////////////////////////////
            xQueueSend(qPWM, &duty, portMAX_DELAY);

            // Armo el texto para el LCD
            sprintf(lcd.text, "LUX: %d", lux);
            lcd.line = 0;
            lcd.clear = 1;
            // Envio a la cola del lcd
            xQueueSend(qLCD, &lcd, portMAX_DELAY);
            // Fuerzo cambio de contexto
            taskYIELD();

            // Leo la hora en el rtc
            if(xSemaphoreTake(mI2C, portMAX_DELAY) == pdPASS){
                rtc_read(&time);
                xSemaphoreGive(mI2C);
            }
            // muestro la ultima hora
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
    }
}

void task_Consola(void *params) {
    char cmd[32];
    int addr, len;

    for (;;) {
        printf("\nComando (dump / clear / exit): ");
        fflush(stdout);

        // Leer comando por consola
        if (scanf("%31s", cmd) == 1) {
            
            if (strcmp(cmd, "dump") == 0) {
                printf("Direccion inicial: ");
                scanf("%d", &addr);
                printf("Longitud: ");
                scanf("%d", &len);

                // Crear cola de respuesta para recibir bloques
                QueueHandle_t qResp = xQueueCreate(16, sizeof(uint8_t[64]));

                eepromDumpRequest_t req = {
                    .start_address = addr,
                    .length = len,
                    .responseQueue = qResp
                };

                xQueueSend(qEdumpReq, &req, portMAX_DELAY);

                // Recibir datos y mostrarlos
                int bloques = (len + 63) / 64;
                for (int i = 0; i < bloques; i++) {
                    uint8_t buffer[64];
                    xQueueReceive(qResp, buffer, portMAX_DELAY);
                    for (int j = 0; j < 64 && (i*64+j) < len; j++) {
                        printf("%02X ", buffer[j]);
                    }
                    printf("\n");
                }

                vQueueDelete(qResp);
            }

            else if (strcmp(cmd, "clear") == 0) {
                printf("Direccion inicial: ");
                scanf("%d", &addr);
                printf("Longitud: ");
                scanf("%d", &len);

                QueueHandle_t qDone = xQueueCreate(1, sizeof(uint8_t));

                eepromClearRequest_t clr = {
                    .start_address = addr,
                    .length = len,
                    .doneSignal = qDone
                };

                xQueueSend(qEclearReq, &clr, portMAX_DELAY);

                // Esperar confirmación
                uint8_t dummy;
                xQueueReceive(qDone, &dummy, portMAX_DELAY);

                printf("Borrado completado.\n");
                vQueueDelete(qDone);
            }

            else if (strcmp(cmd, "exit") == 0) {
                printf("Saliendo...\n");
                vTaskDelete(NULL);
            }

            else {
                printf("Comando no reconocido.\n");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
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
    gpio_set_irq_enabled_with_callback(PULS_PIN, GPIO_IRQ_EDGE_RISE, true, &irq_encoder);
    gpio_set_irq_enabled(ENC_B_PIN, GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(ENC_A_PIN, GPIO_IRQ_EDGE_RISE, true);

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
    //bh1750_init();

    // Inicializo DS3231
    rtc_t init;
    init.sec = 00;
    init.min = 10;
    init.hour = 21;
    init.weekday = 4;
    init.day = 7;
    init.month = 8;
    init.year = 25;
    rtc_load(init);

    // Escribo EEPROM
    uint8_t data[16];
    settings_t settings;
    settings.index = 1;
    settings.lux = 10;
    settings.user_max = 99;
    settings.user_min = 1;
    settings.curva = 0;
    settings.time = init;
    settings2bytes(&settings, data);
    uint16_t eeprom_address = 0x0010;
    eeprom_write(data, eeprom_address, 16);

    // Init LED RUN
    gpio_init(LED_RUN_PIN);
    gpio_set_dir(LED_RUN_PIN, true);
    gpio_put(LED_RUN_PIN, false);

    // Init LED 1
    gpio_init(LED1_PIN);
    gpio_set_dir(LED1_PIN, true);
    gpio_put(LED1_PIN, false);

    // Init LED 2
    gpio_init(LED2_PIN);
    gpio_set_dir(LED2_PIN, true);
    gpio_put(LED2_PIN, false);

    // Init ALARMA LO
    gpio_init(AL_LO_PIN);
    gpio_set_dir(AL_LO_PIN, true);
    gpio_put(AL_LO_PIN, false);

    // Init ALARMA HI
    gpio_init(AL_HI_PIN);
    gpio_set_dir(AL_HI_PIN, true);
    gpio_put(AL_HI_PIN, false);

    // Init BUZZER
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, true);
    gpio_put(BUZZER_PIN, false);

    // Creo semaforos binarios y los libero
    vSemaphoreCreateBinary(sPULS);
    xSemaphoreGive(sPULS);
    vSemaphoreCreateBinary(sA);
    xSemaphoreGive(sA);
    vSemaphoreCreateBinary(sB);
    xSemaphoreGive(sB);
    vSemaphoreCreateBinary(sRefresh);
    xSemaphoreGive(sRefresh);
    vSemaphoreCreateBinary(sCalib);
    xSemaphoreGive(sCalib);
    
    // Creo mutex y lo libero
    mI2C = xSemaphoreCreateMutex();
    xSemaphoreGive(mI2C);

    // Creo queues de programa
    qCTRL = xQueueCreate(5, sizeof(char));
    qLCD = xQueueCreate(1, sizeof(qLCD_t));
    qLUX = xQueueCreate(1, sizeof(uint16_t));
    qPWM = xQueueCreate(1, sizeof(float));
    // Creo queues "semaforo" de eeprom
    qEreadReq = xQueueCreate(1, sizeof(QueueHandle_t));
    qEcalibReq = xQueueCreate(1, sizeof(QueueHandle_t));
    qEdumpReq = xQueueCreate(1, sizeof(eepromDumpRequest_t));
    qEclearReq = xQueueCreate(1, sizeof(eepromClearRequest_t));
    // Creo queues de datos de eeprom
    qEwrite = xQueueCreate(1, sizeof(settings_t));
    qEwritecalib = xQueueCreate(1, sizeof(calibration_t));
    qEread = xQueueCreate(1, sizeof(settings_t));
    qEreadcalib = xQueueCreate(1, sizeof(calibration_t));
    //qElog = xQueueCreate(1, sizeof(char)); 

    // Creo queue set de eeprom
    qsetEEPROM = xQueueCreateSet(EEPROM_QUEUE_SIZE*6);
    // Agrego las colas al set
    xQueueAddToSet(qEreadReq, qsetEEPROM);
    xQueueAddToSet(qEcalibReq, qsetEEPROM);
    xQueueAddToSet(qEwrite, qsetEEPROM);
    xQueueAddToSet(qEwritecalib, qsetEEPROM);
    xQueueAddToSet(qEdumpReq, qsetEEPROM);
    xQueueAddToSet(qEclearReq, qsetEEPROM);

    // Creo tareas
    xTaskCreate(task_Control, "Control", 2*configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(task_BH1750, "BH1750", configMINIMAL_STACK_SIZE, NULL, 2, &tBH1750);
    xTaskCreate(task_PWM, "PWM", configMINIMAL_STACK_SIZE, NULL, 2, &tPWM);
    xTaskCreate(task_Setear, "Setear", 512, NULL, 2, &tPulsador);
    xTaskCreate(task_LedRun, "Run", 128, NULL, 2, &tRUN);
    xTaskCreate(task_Calibracion, "Calibracion", 256, NULL, 3, NULL);
    // xTaskCreate(task_Consola, "Consola", 1024, NULL, 3, NULL);
    xTaskCreate(task_LCD, "LCD", 128, NULL, 4, &tLCD);
    xTaskCreate(task_EEPROM, "EEPROM", 1024, NULL, 4, &tEEPROM);
    xTaskCreate(task_Pulsador, "Pulsador", 128, NULL, 5, NULL);
    xTaskCreate(task_EncoderA, "EncoderA", 128, NULL, 5, NULL);
    xTaskCreate(task_EncoderB, "EncoderB", 128, NULL, 5, NULL);
    //xTaskCreate(task_Setup, "SETUP", 1024, NULL, 6, NULL);

    // Enciendo el scheduler
    vTaskStartScheduler();
    while (true);
}