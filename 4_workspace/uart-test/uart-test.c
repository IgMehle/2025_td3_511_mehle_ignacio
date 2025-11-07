#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/irq.h"
// Headers de FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define LED_RUN_PIN     25

#define UART_ID uart0
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define UART_BAUDRATE 115200
#define UART_BUFFER_SIZE 128

// COMANDOS + ARGUMENTOS
// set lux -v INT -h INT -l INT -a INT
// get lux
// get log
// set eclear
// set rtc day/month/year weekday hour:min:seg

// Queues de manejo de uart
QueueHandle_t q_uart_rx = NULL;
QueueHandle_t q_uart_tx = NULL;

//----- RTC_T for RTC -----//
typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} rtc_t;

// ESTRUCTURA BASE DE SETTINGS
typedef struct settings {
    uint16_t index; // 2B
    uint16_t setpoint; // 2B
    uint16_t user_max; // 2B
    uint16_t user_min; // 2B
    uint8_t curva; // 1B
    rtc_t time; // 7B
} settings_t;

/* void uart_tx_send(const char *msg) {
    //if (q_uart_tx == NULL) return;

    char buffer[UART_BUFFER_SIZE];
    strncpy(buffer, msg, sizeof(buffer));
    buffer[UART_BUFFER_SIZE - 1] = '\0';

    xQueueSend(q_uart_tx, buffer, 0);
} */

uint8_t uart_set_lux(const char *args)
{
    settings_t settings = {0};
    char buffer[UART_BUFFER_SIZE];

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
    // FOR DEBUG
    // Confirmo que recibi los datos del nuevo setting
    printf("[OK] Lux = %5d - Max = %5d - Min: %5d - Curva: %1d\n",
         settings.setpoint, settings.user_max, settings.user_min, settings.curva);

    // Mando ACK de recepcion de datos
    return 0;
}

uint8_t uart_eclear(void)
{
    // clear_settings();
    // FOR DEBUG
    printf("[OK] Comando de borrado recibido\n");
    // Mando ACK de recepcion de datos
    return 0;
}

uint8_t uart_set_rtc(const char *args)
{
    rtc_t time = {0};
    // uint8_t d0, d1, m0, m1, y0, y1, wd, hh0, hh1, mm0, mm1, ss0, ss1;
    uint8_t day, month, year, weekday, hour, minute, second;
    //uint8_t d, m, y, wd, hh, mm, ss;
    int parsed;
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
            &day, &month, &year, &weekday, &hour, &minute, &second);
    
    if(parsed == 7){
        time.day = day;
        time.month = month;
        time.year = year;
        time.weekday = weekday;
        time.hour = hour;
        time.min = minute;
        time.sec = second;
        // FOR TESTING
        printf("[OK] RTC_T recibido: %2d/%2d/%2d %1d %2d:%2d:%2d\n",
        time.day, time.month, time.year, time.weekday,
        time.hour, time.min, time.sec);
    }

    // FOR TESTING
    else printf("[RTC] Error de formato de fecha y hora\n");
    // Mando ACK de recepcion de datos
    return 0;
}

void uart_get_lux(void)
{
    char msg[UART_BUFFER_SIZE];
    static uint16_t lux_actual = 1111;
    static uint16_t max_actual = 2222;
    static uint16_t min_actual = 999;
    // Armo string (demo)
    snprintf(msg, sizeof(msg),
        "[NOW] Lux = %5d - AL max = %5d - AL min = %5d - Curva = 0\n", 
        lux_actual, max_actual, min_actual);
    // Encolo el mensaje a UART_TX
    xQueueSend(q_uart_tx, msg, 0);
    // FOR DEBUG
    printf("[OK] Comando de lectura de lux recibido\n");
}

void uart_get_log(void)
{
    char msg[UART_BUFFER_SIZE];

    // FOR DEBUG
    printf("[OK] Comando de dump log recibido\n");
    // log_settings();

    // Armo string (DEMO)
    // "[OK] Lux = 1111 - Max = 2222 - Min = 999 - Curva: RAPIDA\n"
    for(uint8_t i = 0; i < 5; i++){
        snprintf(msg, sizeof(msg), "[%03d] Lux = %5d - Max = %5d - Min = %5d - Curva: RAPIDA\n",
            i, i+1000, i+1200, i+800);
        // Encolo strings a UART_TX
        xQueueSend(q_uart_tx, msg, 0);
        // Delay para refresh de buffers en recepcion
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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
        snprintf(msg, sizeof(msg), "[ACK] Comando set %s OK\n", opc);
        // Encolo string a UART_TX
        xQueueSend(q_uart_tx, msg, 0);
    }
    else {
        snprintf(msg, sizeof(msg), "[NACK] Comando set %s incorrecto\n", opc);
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

void task_LedRun(void *pvParams)
{
    for(;;){
        gpio_put(LED_RUN_PIN, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_put(LED_RUN_PIN, false);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main()
{
    stdio_init_all();

    // Init LED RUN
    gpio_init(LED_RUN_PIN);
    gpio_set_dir(LED_RUN_PIN, true);
    gpio_put(LED_RUN_PIN, true);

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

    // Creo tareas
    xTaskCreate(task_LedRun, "RUN", 128, NULL, 1, NULL);
    xTaskCreate(task_UART_RX, "UART-RX", 512, NULL, 2, NULL);
    xTaskCreate(task_UART_TX, "UART-TX", 512, NULL, 1, NULL);

    // START SCHEDULER
    vTaskStartScheduler();
    while (true);
}