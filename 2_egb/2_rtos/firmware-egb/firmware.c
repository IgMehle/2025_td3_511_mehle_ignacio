#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
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

#define DUMP_SIZE       20

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

// COMANDOS + ARGUMENTOS
// set lux -v INT -h INT -l INT -a INT
// get lux
// get log
// set eclear
// set rtc day/month/year weekday hour:min:seg
#define UART_ID uart0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define UART_BAUDRATE 115200
#define UART_BUFFER_SIZE 128

// Queues de manejo de uart
QueueHandle_t q_uart_rx;
QueueHandle_t q_uart_tx;

// Handles de tareas
TaskHandle_t th_Control, th_Lux, th_RTC, th_LCD, th_PWM, th_Config;
// Queues de datos inter tarea
QueueHandle_t q_settings, q_irq, q_ctrl, q_pwm, q_lux, q_lcd, q_rtc;
// Queues de manejo de eeprom
QueueHandle_t q_ecmd;
// Queues de datos de eeprom
QueueHandle_t q_ewrite, q_ewritecalib, q_eread, q_ereadcalib, q_dump;
// Mutex bus I2C1
SemaphoreHandle_t m_i2c;
// Semaforos de control
SemaphoreHandle_t s_lux, s_rtc, s_refresh, s_calib, s_eeprom_full, s_eeprom_empty;

// ITEM DE QUEUE LCD
typedef struct lcd {
    char text[17];
    uint8_t line;
    uint8_t clear;
} q_lcd_t;

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
    float kp_r;
    float ti_r;
    float td_r;
    float alpha_r;
    float kp_l;
    float ti_l;
    float alpha_l;
    uint16_t lux_max;
    uint16_t lux_min;
} calibration_t;

// Estructura para dump request
typedef struct dumpreq {
    QueueHandle_t responseQueue;
    // uint16_t start_address;
    uint16_t length;
} eepromDumpRequest_t;

// typedef struct lut {
//     float duty;
//     uint8_t lux;
// } lut_t;

//volatile uint8_t toggle = 0;
//volatile uint8_t toggle2 = 0;
//lut_t lut[101];

void clear_settings(void);
void log_settings(void);

uint8_t uart_set_lux(const char *args)
{
    settings_t settings = {0};
    rtc_t time = {0};
    eeprom_cmd_t ecmd;
    uint8_t ack = 0;
    char buffer[UART_BUFFER_SIZE];

    // Suspendo tareas
    vTaskSuspend(th_Control);
    vTaskSuspend(th_Lux);

    // Copio args a buffer
    strncpy(buffer, args, UART_BUFFER_SIZE);
    buffer[UART_BUFFER_SIZE - 1] = '\0';
    // Tokenizer
    /* 
    * char *strtok(char *string1, const char *string2);
    *
    * La función strtok() lee string1 como una serie de cero o más señales, 
    * y string2 como el conjunto de caracteres que sirven como delimitadores de las señales en string1.
    * En la primera llamada a la función strtok() para una string1 determinada
    * la función strtok() busca la primera señal en string1, omitiendo los delimitadores iniciales.
    * Se devuelve un puntero a la primera señal.
    * En llamadas posteriores con la misma serie de señal, 
    * la función strtok() devuelve un puntero a la siguiente señal de la serie.
    * Se devuelve un puntero NULL cuando no hay más señales.
    * Todas las señales tienen un final nulo.
    */
    char *token = strtok(buffer, " ");
    // Ignoramos los primeros tokens ("set", "lux")
    /* Cuando se llama a la función strtok() con un argumento NULL string1 
    * la siguiente señal se lee de una copia almacenada del último parámetro string1 no nulo.
    * Cada delimitador se sustituye por un carácter nulo. 
    * El conjunto de delimitadores puede variar de una llamada a otra, 
    * por lo que string2 puede tomar cualquier valor. 
    * Tenga en cuenta que el valor inicial de string1 no se conserva 
    * después de la llamada a la función strtok()
    */
    while (token && strcmp(token, "set") == 0)
        token = strtok(NULL, " ");
    if (token && strcmp(token, "lux") == 0)
        token = strtok(NULL, " ");
    
    // Parseo los pares flag + valor
    while (token != NULL)
    {
        if (strcmp(token, "-v") == 0)
        {
            token = strtok(NULL, " ");
            if (token) settings.setpoint = (uint16_t) atoi(token);
        }
        else if (strcmp(token, "-h") == 0)
        {
            token = strtok(NULL, " ");
            if (token) settings.user_max = (uint16_t) atoi(token);
        }
        else if (strcmp(token, "-l") == 0)
        {
            token = strtok(NULL, " ");
            if (token) settings.user_min = (uint16_t) atoi(token);
        }
        else if (strcmp(token, "-a") == 0)
        {
            token = strtok(NULL, " ");
            if (token) settings.curva = (uint8_t) atoi(token);
        }

        token = strtok(NULL, " ");
    }

    // Leo la hora en el rtc
    xSemaphoreGive(s_rtc);
    taskYIELD();
    xQueueReceive(q_rtc, &time, portMAX_DELAY);
    settings.time = time;

    // Actualizo queue settings
    //xQueueOverwrite(q_settings, &settings);

    // settings -> EEPROM
    xQueueSend(q_ewrite, &settings, portMAX_DELAY);
    ecmd = ECTRL_WRITE_SETTINGS;
    xQueueSend(q_ecmd, &ecmd, portMAX_DELAY);
    taskYIELD();

    // DELAY 1SEG
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Doy la orden a task_Control para que refresque los datos
    xSemaphoreGive(s_refresh);
    // Saco de la suspension a task_Control Y task del luxometro
    vTaskResume(th_Control);
    vTaskResume(th_Lux);

    // FOR DEBUG
    // Confirmo que recibi los datos del nuevo setting
    printf("[OK] Lux = %5d - Max = %5d - Min: %5d - Curva: %1d\n",
         settings.setpoint, settings.user_max, settings.user_min, settings.curva);

    // Mando ACK de recepcion de datos
    return ack;
}

uint8_t uart_eclear(void)
{
    uint8_t ack = 0;
    // FOR DEBUG
    printf("[OK] Comando de borrado recibido\n");
    clear_settings();
    // Mando ACK de recepcion de datos
    return ack;
}

uint8_t uart_set_rtc(const char *args)
{
    rtc_t time = {0};
    // uint8_t d0, d1, m0, m1, y0, y1, wd, hh0, hh1, mm0, mm1, ss0, ss1;
    // uint8_t day, month, year, weekday, hour, minute, second;
    uint8_t d, m, y, wd, hh, mm, ss;
    int parsed;
    uint8_t ack = 0;
    /* STRING ESPERADA
    * "set rtc day/month/year weekday hour:min:seg"
    * 1) separar set y rtc (hardcodeado con puntero)
    * 2) tomar el string de fecha y hora
    * 3) parsear con sscanf()
    */
    char buffer[24];
    // Puntero, salto "set rtc "
    const char *ptr = args + 8;
    // salta posibles espacios
    while (*ptr == ' ') ptr++;
    // copia hasta fin de cadena (se podria hacer con strncpy)
    for (uint8_t i = 0; i < 23 && *ptr; i++, ptr++) buffer[i] = *ptr;
    buffer[23] = '\0';  // asegurar terminación

    /*
    * La función sscanf() lee datos del almacenamiento intermedio
    * en las ubicaciones que proporciona la lista-argumentos. Cada
    * argumento debe ser un puntero a una variable con un tipo que
    * corresponda a un especificador de tipo en la serie-formato.
    */
    // Deserializo con sscanf()

    // DE A 1 DIGITO
    /* parsed = sscanf(buffer,
        "%1hhu%1hhu/"   // day
        "%1hhu%1hhu/"   // month
        "%1hhu%1hhu "   // year
        "%1hhu "        // weekday
        "%1hhu%1hhu:"   // hour
        "%1hhu%1hhu:"   // min
        "%1hhu%1hhu",   // sec
        &d0, &d1, &m0, &m1, &y0, &y1, 
        &wd, &hh0, &hh1, &mm0, &mm1, &ss0, &ss1);

    if(parsed == 13){
        time.day = d0 + 10*d1;
        time.month = m0 + 10*m1;
        time.year = y0 + 10*y1;
        time.weekday = wd;
        time.hour = hh0 + 10*hh1;
        time.min = mm0 + 10*mm1;
        time.sec = ss0 * 10*ss1;
        // FOR TESTING
        printf("[OK] RTC_T recibido: %2d/%2d/%2d %1d %2d:%2d:%2d\n",
        time.day, time.month, time.year, time.weekday,
        time.hour, time.min, time.sec);
    } */
    // DE A 2 DIGITOS
    parsed = sscanf(buffer, "%2hhu/%2hhu/%2hhu %1hhu %2hhu:%2hhu:%2hhu",
                        &d, &m, &y, &wd, &hh, &mm, &ss);
    if(parsed == 7){
        time.day = d;
        time.month = m;
        time.year = y;
        time.weekday = wd;
        time.hour = hh;
        time.min = mm;
        time.sec = ss;

        // ESCRIBO EN EL RTC EN BAREMETAL
        // ------------------------------
        taskENTER_CRITICAL();
        rtc_load(time);
        taskEXIT_CRITICAL();
        // ------------------------------

        // FOR DEBUG
        printf("[OK] RTC_T recibido: %02d/%02d/%02d %1d %02d:%02d:%02d\n",
        time.day, time.month, time.year, time.weekday,
        time.hour, time.min, time.sec);
    }

    // FOR DEBUG
    else {
        printf("[RTC] Error de formato de fecha y hora\n");
        ack = 1;
    } 
    // Mando ACK de recepcion de datos
    return ack;
}

void uart_get_lux(void)
{
    char msg[UART_BUFFER_SIZE];
    static settings_t actual;
    uint16_t lux_actual;
    // FOR DEBUG
    printf("[OK] Comando de lectura de lux recibido\n");
    /* LEER LUX
    * Hay lectura del luxometro ?
    * Como la cola es recibida tambien dentro de task_Control()
    * Vamos a trabajar con Overwrite y Peek para que ambas no se pisen
    */
    if(xQueuePeek(q_lux, &lux_actual, 0) == pdPASS){
        if(xQueuePeek(q_settings, &actual, 0) == pdFALSE){
            // Si no puedo leer valores validos de settings mando 0
            actual.user_max = 0;
            actual.user_min = 0;
            actual.curva = 0;
        }
        snprintf(msg, UART_BUFFER_SIZE-1, 
            "[NOW] LUX %d MAX %d MIN %d CURVA %d\n",
            lux_actual, actual.user_max, actual.user_min, actual.curva);
        // Encolo el mensaje a UART_TX
        xQueueSend(q_uart_tx, msg, 0);
    }
    else printf("[LUX] Error de lectura de lux\n");
}

void uart_get_log(void)
{
    // FOR DEBUG
    printf("[OK] Comando de dump log recibido\n");
    log_settings();

    /* char msg[UART_BUFFER_SIZE];
    // Armo string (DEMO)
    // "[OK] Lux = 1111 - Max = 2222 - Min = 999 - Curva: RAPIDA\n"
    for(uint8_t i = 0; i < 5; i++){
        snprintf(msg, UART_BUFFER_SIZE-1, "[%03d] Lux = %5d - Max = %5d - Min = %5d - Curva: RAPIDA\n",
            i, i+1000, i+1200, i+800);
        // Encolo strings a UART_TX
        xQueueSend(q_uart_tx, msg, 0);
        // Delay para refresh de buffers en recepcion
        vTaskDelay(pdMS_TO_TICKS(100));
    } */
}

void uart_cmd_set(const char *args)
{
    uint8_t ack = 1; // clear si se ejecuta bien el comando
    char msg[UART_BUFFER_SIZE];
    printf("[UART] uart_set() recibido: %s\n", args);
    /*--- OPCIONES ---
    // set lux args
    // set eclear
    // set rtc args
    -----------------*/
    // Buffer de opciones
    char opc[8] = {0};
    // Puntero, salto "set "
    const char *ptr = args + 4;
    // salta espacios
    while (*ptr == ' ') ptr++;
    // copia hasta espacio o fin de cadena
    for (uint8_t i = 0; i < 7 && *ptr && *ptr != ' '; i++, ptr++) opc[i] = *ptr;
    opc[7] = '\0';  // asegurar terminación

    // Desglose
    if(strncmp(opc, "lux", 3) == 0){
        // Opcion new setting
        ack = uart_set_lux(args);
    }
    else if(strncmp(opc, "rtc", 3) == 0){
        // Opcion set rtc
        ack = uart_set_rtc(args);
    }
    else if(strncmp(opc, "eclear", 6) == 0){
        // Opcion clear eeprom
        ack = uart_eclear();
    }
    else printf("[UART] Opcion desconocida: %s\n", opc);
    // ENVIO ACK SI COMANDO OK
    if(ack == 0){
        snprintf(msg, UART_BUFFER_SIZE-1, "[ACK] Comando set %s OK\n", opc);
        // Encolo string a UART_TX
        xQueueSend(q_uart_tx, msg, 0);
    }
    else {
        snprintf(msg, UART_BUFFER_SIZE-1, "[NACK] Comando set %s incorrecto\n", opc);
        // Encolo string a UART_TX
        xQueueSend(q_uart_tx, msg, 0);
    }
}

void uart_cmd_get(const char *args) 
{
    printf("[UART] uart_get() recibido: %s\n", args);
    /*--- OPCIONES ---
    // get lux
    // get log
    -----------------*/
    // Buffer de opciones
    char opc[6] = {0};
    // Puntero, salto "get "
    const char *ptr = args + 4;
    // salta espacios
    while (*ptr == ' ') ptr++;
    // copia hasta espacio o fin de cadena
    for (uint8_t i = 0; i < 5 && *ptr && *ptr != ' '; i++, ptr++) opc[i] = *ptr;
    opc[5] = '\0';  // asegurar terminación

    // Desglose
    if(strncmp(opc, "lux", 3) == 0){
        // Opcion leer valor actual de lux
        uart_get_lux();
    }
    else if(strncmp(opc, "log", 3) == 0){
        // Opcion traer log de settings
        uart_get_log();
    }
    else printf("[UART] Opcion desconocida: %s\n", opc);
}

/* ISR de recepción UART */
void ISR_uart_rx() {
    static char uart_rx_buffer[UART_BUFFER_SIZE];
    static uint16_t uart_rx_index = 0;

    while (uart_is_readable(UART_ID)) {
        char c = uart_getc(UART_ID);
        // if (c == '\r') continue;
        if (c == '\r' || c == '\n') {
            if (uart_rx_index > 0) {
                uart_rx_buffer[uart_rx_index] = '\0';
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                xQueueSendFromISR(q_uart_rx, uart_rx_buffer, &xHigherPriorityTaskWoken);
                uart_rx_index = 0;
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        } else if (uart_rx_index < UART_BUFFER_SIZE - 1) {
            uart_rx_buffer[uart_rx_index++] = c;
        }
    }
}

/* Tarea RX de la UART */
void task_UART_RX(void *pvParams) {
    // BUFFER
    char rx_buffer[UART_BUFFER_SIZE];

    for (;;) {
        if (xQueueReceive(q_uart_rx, rx_buffer, portMAX_DELAY) == pdTRUE) {
            // COMANDO SET (ESCRIBIR DATOS)
            if (strncmp(rx_buffer, "set", 3) == 0)
                uart_cmd_set(rx_buffer);
            // COMANDO GET (TRAER DATOS)
            else if (strncmp(rx_buffer, "get", 3) == 0)
                uart_cmd_get(rx_buffer);
            else printf("[UART] Comando desconocido: %s\n", rx_buffer);
        }
    }
}

/* Tarea TX de la UART */
void task_UART_TX(void *pvParams) {
    // BUFFER
    char tx_buffer[UART_BUFFER_SIZE];

    for (;;) {
        // Espera un mensaje en la cola para enviar
        if (xQueueReceive(q_uart_tx, tx_buffer, portMAX_DELAY) == pdTRUE) {
            // Aseguro terminacion de linea
            tx_buffer[UART_BUFFER_SIZE - 1] = '\0';
            // Mando string a la uart
            uart_puts(UART_ID, tx_buffer);
        }
    }
}

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
        xQueueSendFromISR(q_irq, &cmd, &taskWoken);
        portYIELD_FROM_ISR(taskWoken);
    }
}

void task_Encoder(void *pvParams)
{
    uint8_t irq;
    char comando;
    while(1){
        if(xQueueReceive(q_irq, &irq, portMAX_DELAY) == pdPASS){
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
            xQueueSend(q_ctrl, &comando, portMAX_DELAY);
        }
    }
}

void task_RTC(void *pvParams)
{
    rtc_t time;
    while(1){
        // Si llega la orden para leer hora
        if(xSemaphoreTake(s_rtc, portMAX_DELAY) == pdPASS){
            // Leo la hora en el rtc
            if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                rtc_read(&time);
                xSemaphoreGive(m_i2c);
                xQueueSend(q_rtc, &time, portMAX_DELAY);
            }
        }
    }
}

void task_LCD(void *pvParams)
{
    q_lcd_t bf;
    while(1){
        // Si hay datos en la cola, intento tomar el bus
        if(xQueueReceive(q_lcd, &bf, portMAX_DELAY) == pdPASS){
            // Si puedo tomar el bus
            if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                // Si clear esta en 1, limpio la pantalla
                if(bf.clear){
                    lcd_clear();
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                // Escribo linea
                lcd_set_cursor(bf.line, 0);
                lcd_string(bf.text);
                xSemaphoreGive(m_i2c);
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
    // eepromDumpRequest_t dump_q;
    //uint8_t bf_calib[16];
    uint16_t address = 0x0000;
    // Contador de registros
    uint16_t index = 0;
    // Puntero al registro en eeprom
    uint16_t ptr = EEPROM_DATA_BASE;
    // Address del primer lugar vacio
    uint16_t top = EEPROM_DATA_BASE;
    // Address del final de los registros
    uint16_t end = 250*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
    char force_config = 0;
    // LOG UART
    char msg[UART_BUFFER_SIZE];

    // LEVANTO EL NUMERO DE REGISTROS ESCRITOS EN EEPROM
    for(uint8_t i=0; i<250; i++){
        address = i*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
        eeprom_read(bf, address, 16);
        bytes2settings(&settings, bf);
        if ((settings.index == 0xFFFF) || (settings.index == 0x0000)){
            // Index es la cantidad de registros que avance
            // Antes de encontrar el vacio
            // i = contador, arranca de 0
            if(i>0){
                index = (uint16_t)(i);
                // Leo setting anterior y guardo en queue
                address = (index-1)*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                eeprom_read(bf, address, 16);
                bytes2settings(&settings, bf);
                settings.index = index;
                xQueueOverwrite(q_settings, &settings);
            } 
            else {
                // Memoria de settings nueva o vacia
                // Fuerzo entrada a Config enviando un caracter 'P' a q_ctrl
                index = 0;
                force_config = 'P';
                xSemaphoreGive(s_eeprom_empty);
                xQueueOverwrite(q_ctrl, &force_config);
            }
            break;
        }
    }
    // Con los settings leidos puedo iniciar tareas
    // Si no tengo settings, no puedo iniciar Control
    if(force_config == 0) vTaskResume(th_Control);
    vTaskResume(th_LCD);
    vTaskResume(th_RTC);
    vTaskResume(th_Lux);

    while(1){
        if(xQueueReceive(q_ecmd, &comando, portMAX_DELAY) == pdPASS){
            switch (comando)
            {
            case ECTRL_WRITE_SETTINGS:
                // Intento leer de la queue de escritura
                if(xQueueReceive(q_ewrite, &settings, portMAX_DELAY) == pdPASS){
                    // Calculo address DEL NUEVO REGISTRO
                    address = index*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                    // Incremento index y guardo en settings
                    index++;
                    settings.index = index;
                    // Imprimo nuevo setting a guardar
                    printf("\n[%d] (%02d/%02d %02d:%02d:%02d) Lux: %d, Max: %d, Min: %d, Curva: %d",
                            settings.index, settings.time.day, settings.time.month,
                            settings.time.hour, settings.time.min,
                            settings.time.sec, settings.setpoint, settings.user_max,
                            settings.user_min, settings.curva);
                    // Desempaqueto
                    settings2bytes(&settings, bf);
                    // Intento tomar el bus
                    if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                        // Escribo settings en la eeprom
                        eeprom_write(bf, address, EEPROM_DATA_SIZE);
                        // delay de escritura de eeprom
                        vTaskDelay(pdMS_TO_TICKS(5));
                        xSemaphoreGive(m_i2c);
                    }
                    // Guardo nuevo setting en queue
                    xQueueOverwrite(q_settings, &settings);
                }   
                break;

            case ECTRL_READ_SETTINGS:
                // Calculo address del ultimo registro
                address = (index-1)*EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                printf("\nLeyendo registro N.%d", index-1);
                // Intento tomar el bus
                if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                    // Leo el ultimo registro de la eeprom
                    eeprom_read(bf, address, 16);
                    xSemaphoreGive(m_i2c);
                }
                // Empaqueto
                bytes2settings(&settings, bf);
                // Mando a la cola de lectura
                xQueueSend(q_eread, &settings, portMAX_DELAY);
                break;

            case ECTRL_WRITE_CALIB:
                // Intento leer de la queue de escritura de calibracion
                if(xQueueReceive(q_ewritecalib, &calib, 0) == pdPASS){
                    // Intento tomar el bus
                    if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                        // Escribo parametros de calibracion en la eeprom
                        //eeprom_write(calib, 0x0000, 132);
                        // delay de escritura de eeprom
                        vTaskDelay(pdMS_TO_TICKS(5));
                        xSemaphoreGive(m_i2c);
                    }
                }  
                break;

            case ECTRL_READ_CALIB:
                // Intento tomar el bus
                if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                    // Leo parametros de calibracion de la eeprom
                    //eeprom_read(calib, 0x0000, 32);
                    xSemaphoreGive(m_i2c);
                }
                // Mando a la cola de lectura de parametros del pid
                xQueueSend(q_ereadcalib, &calib, portMAX_DELAY);
                break;

            case ECTRL_DUMP:
                ////////////////////////////////////////////////////////
                // IMPRESION DE PARAMETROS DE CALIBRACION PARA DEBUG
                // eeprom_read(bf, 0x0000, 16);
                // memcpy(&calib, bf, sizeof(calibration_t));
                // printf("\r\nKP= %f - KI= %f - KD= %f - LUXMAX= %d - luxmin= %d",
                //     calib.kp, calib.ti, calib.td, calib.lux_max, calib.lux_min);
                // fflush(stdout);
                // Paso el numero de items en memoria
                //dump_q.length = index;
                //dump_q.length = DUMP_SIZE;
                // creo cola de volcado
                //dump_q.responseQueue = xQueueCreate(index, 16U);
                //dump_q.responseQueue = xQueueCreate(DUMP_SIZE, 16U);
                // apunto al inicio de los settings de usuario
                ptr = EEPROM_DATA_BASE;
                //ptr = EEPROM_DATA_BASE + EEPROM_DATA_SIZE;
                // apunto al final de los settings
                top = (index) * EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                // top = DUMP_SIZE * EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                printf("\n");
                while (ptr < top) {
                    // Intento tomar el bus
                    if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                        // leo datos y encolo
                        eeprom_read(bf, ptr, 16);
                        xSemaphoreGive(m_i2c);
                    }
                    bytes2settings(&settings, bf);
                    // Formateo
                    snprintf(msg, UART_BUFFER_SIZE-1, "[%3d] (%02d/%02d %02d:%02d:%02d) Lux = %5d - Max = %5d - Min = %d - Curva = %1d\n",
                        settings.index, settings.time.day, settings.time.month,
                        settings.time.hour, settings.time.min,
                        settings.time.sec, settings.setpoint, settings.user_max,
                        settings.user_min, settings.curva);
                    // Encolo strings a UART_TX
                    xQueueSend(q_uart_tx, msg, 0);
                    // IMPRIMO POR CONSOLA (DEBUG)
                    printf("%s", msg);
                    fflush(stdout);
                    // DELAY DE PROCESAMIENTO
                    vTaskDelay(pdMS_TO_TICKS(50));
                    // Incremento puntero a registro
                    ptr += EEPROM_DATA_SIZE;
                }
                ////////////////////////////////////////////////////////
                break;
            case ECTRL_ERASE:
                //////////////////////////////////////////////////////////////////////
                // EEPROM default erased value
                memset(bf, 0xFF, sizeof(bf));
                // apunto al inicio de los settings menos el primero
                ptr = EEPROM_DATA_BASE + EEPROM_DATA_SIZE;
                // apunto al final de los settings
                // top = DUMP_SIZE * EEPROM_DATA_SIZE;
                top = (index) * EEPROM_DATA_SIZE + EEPROM_DATA_BASE;
                while (ptr < top) {
                    // Intento tomar el bus
                    if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                        eeprom_write(bf, ptr, 16);
                        xSemaphoreGive(m_i2c);
                    }
                    // delay de escritura de eeprom
                    vTaskDelay(pdMS_TO_TICKS(10));
                    ptr += EEPROM_DATA_SIZE;
                }
                // Resguardo el ultimo setting
                index = 1;
                // Leo el ultimo setting de la cola
                xQueuePeek(q_settings, &settings, 0);
                // Asigno al registro 1
                settings.index = 1;
                ptr = EEPROM_DATA_BASE;
                // Desempaqueto
                settings2bytes(&settings, bf);
                // Intento tomar el bus
                if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
                    eeprom_write(bf, ptr, 16);
                    xSemaphoreGive(m_i2c);
                }
                // delay de escritura de eeprom
                vTaskDelay(pdMS_TO_TICKS(10));
                // Aviso que termine de borrar
                xSemaphoreGive(s_eeprom_empty);
                ///////////////////////////////////////////////////////////////////////
                break;
            default:
                break;
            }
        }
    }
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
    q_lcd_t lcd;
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
        xQueueSend(q_lcd, &lcd, portMAX_DELAY);
        taskYIELD();
        sprintf(lcd.text, "%3.1f", tu);
        lcd.line = 1;
        lcd.clear = 0;
        ///// -> LCD
        xQueueSend(q_lcd, &lcd, portMAX_DELAY);
        taskYIELD();

        // ENTRO EN SECCION BAREMETAL
        // taskENTER_CRITICAL();
        vTaskResume(th_Lux);
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
                rx = xQueueReceive(q_lux, &lux, 0);
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
        vTaskSuspend(th_Lux);
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
            xQueueReceive(q_ctrl, &opc, portMAX_DELAY);
            switch(opc){
                case 'H':
                    tu += 0.05;
                    // Muestro valor de TU en LCD
                    sprintf(lcd.text, "%3.2f", tu);
                    lcd.line = 1;
                    lcd.clear = 0;
                    ///// -> LCD
                    xQueueSend(q_lcd, &lcd, portMAX_DELAY);
                    taskYIELD();
                    break;
                case 'A':
                    if(tu > 0.05) tu -= 0.05;
                    // Muestro valor de TU en LCD
                    sprintf(lcd.text, "%3.2f", tu);
                    lcd.line = 1;
                    lcd.clear = 0;
                    ///// -> LCD
                    xQueueSend(q_lcd, &lcd, portMAX_DELAY);
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
    ///// DUMP DE SETTINGS /////
    ecmd = ECTRL_DUMP;
    xQueueSend(q_ecmd, &ecmd, portMAX_DELAY);
    taskYIELD();
}

void clear_settings(void)
{
    // Comando de eeprom
    eeprom_cmd_t ecmd;
    ///// BORRADO DE SETTINGS /////
    ecmd = ECTRL_ERASE;
    xQueueSend(q_ecmd, &ecmd, portMAX_DELAY);
    taskYIELD();
    // Esperar confirmación
    if(xSemaphoreTake(s_eeprom_empty, portMAX_DELAY) == pdPASS){
        printf("\nBorrado completado.\n");
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
    q_lcd_t lcd;
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

    while(1){
        // Si toco el pulsador entro en el menu de configuracion
        rx = xQueueReceive(q_ctrl, &opc, portMAX_DELAY);
        if(rx == pdPASS && opc == 'P'){
            // ENTRO EN CONFIGURACION
            
            // Suspendo tareas
            vTaskSuspend(th_Control);
            vTaskSuspend(th_Lux);
            // Levanto la config actual de la eeprom
            ecmd = ECTRL_READ_SETTINGS;
            xQueueSend(q_ecmd, &ecmd, portMAX_DELAY);
            // Cambio de contexto para leer
            taskYIELD();
            // settings <- EEPROM
            xQueueReceive(q_eread, &settings, portMAX_DELAY);
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
                    xQueueSend(q_lcd, &lcd, portMAX_DELAY);
                    taskYIELD();
                    // Muestro valor actualizado en LCD
                    lcd.line = 1;
                    lcd.clear = 0;
                    //sprintf(lcd.text, "%6d", menu[indice].valor);
                    if(indice < 3) sprintf(lcd.text, "%6d", menu[indice].valor);
                    else if(indice == 3) sprintf(lcd.text, "RAPIDA");
                    else sprintf(lcd.text, "    NO");

                    ///// -> LCD
                    xQueueSend(q_lcd, &lcd, portMAX_DELAY);
                    taskYIELD();
                    // limpio variable
                    show_menu = 0;
                }
                //printf("Indice: %d, Opc: %c, Show: %d", indice, opc, show_menu);
                // Recepcion no bloqueante (NO HACER PEEK)
                rx = xQueueReceive(q_ctrl, &opc, portMAX_DELAY);
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
                        xQueueSend(q_lcd, &lcd, portMAX_DELAY);
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
                        xQueueSend(q_lcd, &lcd, portMAX_DELAY);
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
            //xSemaphoreGive(s_calib);
            //taskYIELD();

            // Leo la hora en el rtc
            xSemaphoreGive(s_rtc);
            taskYIELD();
            xQueueReceive(q_rtc, &time, portMAX_DELAY);
            settings.time = time;

            // GUARDANDO CONFIG ... -> LCD
            sprintf(lcd.text, "GUARDANDO");
            lcd.line = 0;
            lcd.clear = 1;
            xQueueSend(q_lcd, &lcd, portMAX_DELAY);
            taskYIELD();
            sprintf(lcd.text, "CONFIGURACION");
            lcd.line = 1;
            lcd.clear = 0;
            xQueueSend(q_lcd, &lcd, portMAX_DELAY);
            taskYIELD();
            
            // cargo menu actualizado a settings
            // settings.index se actualiza en task_EEPROM() - WRITE_SETTINGS
            settings.setpoint = menu[0].valor;
            settings.user_max = menu[1].valor;
            settings.user_min = menu[2].valor;
            settings.curva = (uint8_t) menu[3].valor;
            
            // Actualizo queue settings
            // xQueueOverwrite(q_settings, &settings);

            // settings -> EEPROM
            xQueueSend(q_ewrite, &settings, portMAX_DELAY);
            ecmd = ECTRL_WRITE_SETTINGS;
            xQueueSend(q_ecmd, &ecmd, portMAX_DELAY);
            taskYIELD();

            // DELAY 1SEG
            vTaskDelay(pdMS_TO_TICKS(1000));
            // Doy la orden a task_Control para que refresque los datos
            xSemaphoreGive(s_refresh);
            // Saco de la suspension a task_Control Y task del luxometro
            vTaskResume(th_Control);
            vTaskResume(th_Lux);
        }
    }   
}

void task_BH1750(void *pvParams)
{
    uint16_t lux = 0;
    TickType_t last_tick = xTaskGetTickCount();

    while(1){
        // Si puedo tomar el bus
        if(xSemaphoreTake(m_i2c, portMAX_DELAY) == pdPASS){
            // Leo luxometro
            lux = bh1750_read();
            // Vuelvo a disparar la lectura
            bh1750_init(ONESHOT_LORES);
            xSemaphoreGive(m_i2c);
            // Paso a la cola
            xQueueOverwrite(q_lux, &lux);
            // xQueueSend(q_lux, &lux, portMAX_DELAY);
            // Give semaforo de muestra de lux
            xSemaphoreGive(s_lux);
        }
        // Corre cada LUX_TIME
        vTaskDelayUntil(&last_tick, pdMS_TO_TICKS(LUX_TIME));
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
    uint16_t lux_avg;
    float lux_acc = 0.0;
    q_lcd_t lcd;
    rtc_t time;
    uint8_t prev_sec;
    uint8_t bf[7];
    uint8_t show_lux = 0;
    //////////////////////////
    ///// VARIABLES PID //////
    float ts = (float) LUX_TIME;
    // constantes pid
    float kp = 0.0002;
    //float ti = 5.0;
    //float td = 0.0;
    float ki = 0.0001;
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
    float duty = 0.5;
    float duty_f0 = 0.0;
    float duty_f = 0.0;
    ///////////////////////////
    TickType_t last_tick = xTaskGetTickCount();
    TickType_t tick;
    TickType_t dif_ticks;

    while(1){
        // Actualizar seteos y calibracion ? (NO BLOQUEANTE)
        if(xSemaphoreTake(s_refresh, 0) == pdPASS){
            // ACTUALIZO SETTINGS

            // Levanto valores de la eeprom
            ecmd = ECTRL_READ_SETTINGS;
            xQueueSend(q_ecmd, &ecmd, portMAX_DELAY);
            taskYIELD();
            xQueueReceive(q_eread, &settings, portMAX_DELAY);
            // xQueuePeek(q_settings, &settings, portMAX_DELAY);

            // Calculo de constantes integral y derivativa
            // Sin autotune() se adoptan constantes empiricas
            if(settings.curva == 0){
                // PLANTA RAPIDA - PID
                alpha_p = 0.7;
                kp = 0.0002;
                ki = 0.00005;
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
        // No lo hacemos bloqueante para que pueda imprimir dentro del tiempo de medicion

        // if(xQueueReceive(q_lux, &lux, 0) == pdPASS){
        if(xSemaphoreTake(s_lux, 0) == pdPASS){
            xQueuePeek(q_lux, &lux, 0);

            //////////////////////////////////////////
            ///// CONTROL PID ////////////////////////
            // ap = kp*error;
            // ai = (kp/ti)*(error+error0) + ai0;
            // ad = (kp*td)*(error-error0);
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
            //if(eR > 0.05) LED1_ON;
            //else LED1_OFF;
            //if(eR < -0.05) LED2_ON;
            //else LED2_OFF;

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
            duty_f = alpha_p*duty + (1 - alpha_p)*duty_f0;
            duty_f0 = duty_f;

            // CLAMP + LEDS
            if(duty_f > 0.95){
                // Enciendo alarma de limite de ajuste
                LED1_ON;
                // clamp
                duty_f = 0.95;
            }
            else LED1_OFF;

            if(duty_f < 0.10){
                // Enciendo alarma de limite de ajuste
                LED2_ON;
                // clamp
                duty_f = 0.10;
            }
            else LED2_OFF;

            // ACTUALIZO EL DUTY
            // IMPORTANTE: 0 -> MAX / 1 -> MIN
            setup_pwm(1 - duty_f);

            last_tick = tick;
            tick = xTaskGetTickCount();
            dif_ticks = tick - last_tick;
            //printf("\rLUX = %5d\teR = %.2f\tAP = %.3f\tAI = %.3f\tAD = %.3f\tduty = %2.2f\t%4dms", 
            //    lux, 100*eR, ap, ai, ad, duty_f, dif_ticks);
            // printf("\nd_Ticks: %5dms - LUX= %d - duty = %.2f", dif_ticks, lux, duty_f);

            // Acumulo lux para mostrar en lcd
            show_lux++;
            lux_acc += (float) lux; 
        }

        // Mostrar promedio de medicion de lux cada 10 muestras
        // Actualizar alarmas de banda de error de usuario
        if(show_lux > 9){
            lux_acc = lux_acc / show_lux;
            lux_avg = (uint16_t) lux_acc;
            lux_acc = 0.0;
            // Armo el texto para el LCD
            sprintf(lcd.text, "LUX: %5d", lux_avg);
            lcd.line = 0;
            lcd.clear = 0;
            // Envio a la cola del lcd
            xQueueSend(q_lcd, &lcd, portMAX_DELAY);
            // Fuerzo cambio de contexto
            taskYIELD();
            // Reseteo contador
            show_lux = 0;

            // Alarmas de ajuste de usuario
            if(lux_avg > settings.user_max) ALARMA_HI_ON;
            else ALARMA_HI_OFF;
            if(lux_avg < settings.user_min) ALARMA_LO_ON;
            else ALARMA_LO_OFF;
        }

        // Guardo ultima lectura de segundos
        prev_sec = time.sec;
        // Leo la hora en el rtc
        xSemaphoreGive(s_rtc);
        taskYIELD();
        xQueueReceive(q_rtc, &time, portMAX_DELAY);
        // muestro la ultima hora si time.sec cambio
        if(time.sec != prev_sec){
            sprintf(lcd.text, "%02d/%02d %02d:%02d:%02d", time.day, time.month, 
                            time.hour, time.min, time.sec);
            lcd.line = 1;
            lcd.clear = 0;
            // Escribo hora en el LCD
            xQueueSend(q_lcd, &lcd, portMAX_DELAY);
            // Fuerzo cambio de contexto
            // taskYIELD();
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
    bh1750_init(ONESHOT_LORES);

    // Inicializo DS3231
    rtc_t init;
    init.sec = 00;
    init.min = 30;
    init.hour = 20;
    init.weekday = 2;
    init.day = 16;
    init.month = 12;
    init.year = 25;
    rtc_load(init);

    // Escribo EEPROM
    uint8_t data[32];
    settings_t settings;
    settings.index = 1;
    settings.setpoint = 1000;
    settings.user_max = 1200;
    settings.user_min = 800;
    settings.curva = 0;
    settings.time = init;
    settings2bytes(&settings, data);
    eeprom_write(data, EEPROM_DATA_BASE, 16);
    sleep_ms(10);

    calibration_t calib;
    calib.kp_r = 0.0005;
    calib.ti_r = 5.0;
    calib.td_r = 0.02;
    calib.kp_l = 0.00005;
    calib.ti_l = 5.0;
    calib.lux_max = 5000;
    calib.lux_min = 1;
    memcpy(data, &calib, sizeof(calibration_t));
    eeprom_write(data, 0x0000, 32);
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

    // INIT UART0 
    uart_init(UART_ID, UART_BAUDRATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, false);
    // IRQ UART RX
    irq_set_exclusive_handler(UART0_IRQ, ISR_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    // Creo queues de UART
    q_uart_rx = xQueueCreate(4, UART_BUFFER_SIZE);
    q_uart_tx = xQueueCreate(8, UART_BUFFER_SIZE);

    // Creo semaforos binarios y los libero
    vSemaphoreCreateBinary(s_lux);
    xSemaphoreGive(s_lux);
    vSemaphoreCreateBinary(s_rtc);
    xSemaphoreGive(s_rtc);
    vSemaphoreCreateBinary(s_refresh);
    xSemaphoreGive(s_refresh);
    vSemaphoreCreateBinary(s_calib);
    xSemaphoreGive(s_calib);

    vSemaphoreCreateBinary(s_eeprom_full);
    vSemaphoreCreateBinary(s_eeprom_empty);
    
    // Creo mutex y lo libero
    m_i2c = xSemaphoreCreateMutex();
    xSemaphoreGive(m_i2c);

    // Creo queues intertarea
    q_settings = xQueueCreate(1, sizeof(settings_t));
    q_irq = xQueueCreate(1, sizeof(uint8_t));
    q_ctrl = xQueueCreate(1, sizeof(char));
    q_lcd = xQueueCreate(3, sizeof(q_lcd_t));
    q_rtc = xQueueCreate(1, sizeof(rtc_t));
    q_lux = xQueueCreate(1, sizeof(uint16_t));
    q_pwm = xQueueCreate(1, sizeof(float));

    // Creo queue de comando de eeprom
    q_ecmd = xQueueCreate(1, sizeof(uint8_t));
    // Creo queues de datos de eeprom
    q_ewrite = xQueueCreate(1, sizeof(settings_t));
    //q_ewrite = xQueueCreate(1, 16U);
    q_ewritecalib = xQueueCreate(1, sizeof(calibration_t));
    // q_ewritecalib = xQueueCreate(1, 16U);
    q_eread = xQueueCreate(1, sizeof(settings_t));
    q_ereadcalib = xQueueCreate(1, sizeof(calibration_t));
    // q_ereadcalib = xQueueCreate(1, 16U);
    q_dump = xQueueCreate(1, sizeof(eepromDumpRequest_t));

    // Creo tareas EGA
    xTaskCreate(task_Control, "Control", 256, NULL, 1, &th_Control);
    xTaskCreate(task_Config, "Config", 512, NULL, 2, &th_Config);
    xTaskCreate(task_LedRun, "Run", 128, NULL, 3, NULL);
    xTaskCreate(task_BH1750, "BH1750", 128, NULL, 4, &th_Lux);
    xTaskCreate(task_LCD, "LCD", 128, NULL, 4, &th_LCD);
    xTaskCreate(task_RTC, "RTC", 128, NULL, 4, &th_RTC);
    xTaskCreate(task_EEPROM, "EEPROM", 384, NULL, 4, NULL);
    xTaskCreate(task_Encoder, "Encoder", 128, NULL, 5, NULL);
    // Creo tareas EGB
    xTaskCreate(task_UART_RX, "UART-RX", 512, NULL, 2, NULL);
    xTaskCreate(task_UART_TX, "UART-TX", 128, NULL, 1, NULL);

    // Suspendo tareas para poder acceder a la EEPROM primero
    vTaskSuspend(th_Control);
    vTaskSuspend(th_Lux);
    vTaskSuspend(th_RTC);
    vTaskSuspend(th_LCD);
    // Enciendo el scheduler
    vTaskStartScheduler();
    while (true);
}