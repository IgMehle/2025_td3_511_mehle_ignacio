#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/i2c_slave.h"
// Headers de FreeRTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#define LED_RUN_PIN     25

#define UART_ID    uart0
#define UART_TXPIN 0
#define UART_RXPIN 1

#define I2C0_SDA_PIN    4
#define I2C0_SCL_PIN    5
#define I2C0_ADDR       0x20
#define I2C0_BAUD       100000

#define REMOTE_BUFFER_SIZE  128

// ==== BUFFERS ====
#define BUF_SIZE   128

static char   rx_line[BUF_SIZE];
static size_t rx_idx = 0;

static char   reply_buffer[BUF_SIZE];
static size_t reply_len = 0;
static size_t reply_idx = 0;

// Queues de manejo de i2c slave
QueueHandle_t q_remote_rx = NULL;
QueueHandle_t q_remote_tx = NULL;

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

uint8_t remote_set_lux(const char *args)
{
    settings_t settings = {0};
    char buffer[REMOTE_BUFFER_SIZE];

    // Copio args a buffer
    strncpy(buffer, args, REMOTE_BUFFER_SIZE);
    buffer[REMOTE_BUFFER_SIZE - 1] = '\0';
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

uint8_t remote_eclear(void)
{
    // clear_settings();
    // FOR DEBUG
    printf("[OK] Comando de borrado recibido\n");
    // Mando ACK de recepcion de datos
    return 0;
}

uint8_t remote_set_rtc(const char *args)
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
    for (uint8_t i = 0; i < 23 && *ptr; i++, ptr++) {
        buffer[i] = *ptr;
    }
    // Asegurar terminacion
    buffer[23] = '\0';

    /*
    * La función sscanf() lee datos del almacenamiento intermedio
    * en las ubicaciones que proporciona la lista-argumentos. Cada
    * argumento debe ser un puntero a una variable con un tipo que
    * corresponda a un especificador de tipo en la serie-formato.
    */
    // Deserializo con sscanf()
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

void remote_get_lux(void)
{
    char msg[REMOTE_BUFFER_SIZE];
    static uint16_t lux_actual = 1111;
    static uint16_t max_actual = 2222;
    static uint16_t min_actual = 999;
    // Armo string (demo)
    snprintf(msg, sizeof(msg),
        "[NOW] Lux = %5d - AL max = %5d - AL min = %5d - Curva = 0\n", 
        lux_actual, max_actual, min_actual);
    // Encolo el mensaje a REMOTE_TX
    xQueueSend(q_remote_tx, msg, 0);
    // FOR DEBUG
    printf("[OK] Comando de lectura de lux recibido\n");
}

void remote_get_log(void)
{
    char msg[REMOTE_BUFFER_SIZE];

    // FOR DEBUG
    printf("[OK] Comando de dump log recibido\n");
    // log_settings();

    // Armo string (DEMO)
    // "[OK] Lux = 1111 - Max = 2222 - Min = 999 - Curva: RAPIDA\n"
    for(uint8_t i = 0; i < 5; i++){
        snprintf(msg, sizeof(msg), "[%03d] Lux = %5d - Max = %5d - Min = %5d - Curva: RAPIDA\n",
            i, i+1000, i+1200, i+800);
        // Encolo strings a REMOTE_TX
        xQueueSend(q_remote_tx, msg, 0);
        // Delay para refresh de buffers en recepcion
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void remote_cmd_set(const char *args)
{
    uint8_t ack = 1; // clear si se ejecuta bien el comando
    char msg[REMOTE_BUFFER_SIZE];
    printf("[I2C] remote_set() recibido: %s\n", args);
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
        ack = remote_set_lux(args);
    }
    else if(strncmp(opc, "rtc", 3) == 0){
        // Opcion set rtc
        ack = remote_set_rtc(args);
    }
    else if(strncmp(opc, "eclear", 6) == 0){
        // Opcion clear eeprom
        ack = remote_eclear();
    }
    else printf("[I2C] Opcion desconocida: %s\n", opc);
    // ENVIO ACK SI COMANDO OK
    if(ack == 0){
        snprintf(msg, sizeof(msg), "[ACK] Comando set %s OK\n", opc);
        // Encolo string a REMOTE_TX
        xQueueSend(q_remote_tx, msg, 0);
    }
    else {
        snprintf(msg, sizeof(msg), "[NACK] Comando set %s incorrecto\n", opc);
        // Encolo string a REMOTE_TX
        xQueueSend(q_remote_tx, msg, 0);
    }
}

void remote_cmd_get(const char *args) 
{
    printf("[I2C] remote_get() recibido: %s\n", args);
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
        remote_get_lux();
    }
    else if(strncmp(opc, "log", 3) == 0){
        // Opcion traer log de settings
        remote_get_log();
    }
    else printf("[I2C] Opcion desconocida: %s\n", opc);
}

// =====================================================
//              ISR I2C - SLAVE
// =====================================================
static void ISR_i2c_slave(i2c_inst_t *i2c, i2c_slave_event_t event)
{
    static bool led_state = false;

    switch (event) {
    case I2C_SLAVE_RECEIVE:
        // El master nos está escribiendo datos
        while (i2c_get_read_available(i2c)) {
            uint8_t c = i2c_read_byte_raw(i2c);

            if (c == '\n' || c == '\r') {
                if (rx_idx > 0) {
                    rx_line[rx_idx] = 0;

                    // Debug por UART
                    printf("[MST] %s\n", rx_line);

                    // Mando linea a la cola
                    BaseType_t xHPW = pdFALSE;
                    xQueueSendFromISR(q_remote_rx, rx_line, &xHPW);
                    rx_idx = 0;
                    portYIELD_FROM_ISR(xHPW);
                }
            } else if (rx_idx < BUF_SIZE - 1) {
                rx_line[rx_idx++] = (char)c;
            }
        }
        break;

    case I2C_SLAVE_REQUEST:
        // El master está leyendo de nosotros
        while (i2c_get_write_available(i2c)) {
            uint8_t c = 0;
            // Copio caracteres en reply_buffer
            if (reply_idx < reply_len) {
                c = (uint8_t) reply_buffer[reply_idx++];
            } else {
                // Si no hay nada más que mandar, mandamos 0
                c = 0;
            }
            // Escribo caracter a enviar
            i2c_write_byte_raw(i2c, c);
        }
        break;

    case I2C_SLAVE_FINISH:
        // Se terminó la transacción (STOP).
        // Reseteo indices
        rx_idx      = 0;
        reply_idx   = 0;
        break;
    }
    // Toggle LED para ver actividad I2C
    led_state = !led_state;
    gpio_put(PICO_DEFAULT_LED_PIN, led_state);
}

// =====================================================
//       TAREA I2C_RX (procesa comandos igual que UART)
// =====================================================
void task_REMOTE_RX(void *pvParams)
{
    char buffer[REMOTE_BUFFER_SIZE];

    for (;;) {
        if (xQueueReceive(q_remote_rx, buffer, portMAX_DELAY) == pdTRUE) {
            // Separo comandos set y get
            if (strncmp(buffer, "set", 3) == 0)
                remote_cmd_set(buffer);

            else if (strncmp(buffer, "get", 3) == 0)
                remote_cmd_get(buffer);

            else
                printf("[RX] Comando desconocido: %s\n", buffer);
        }
    }
}

// =====================================================
//      TAREA TX: prepara reply_buffer
//      (la ISR lo entrega cuando la Pi hace READ)
// =====================================================
void task_REMOTE_TX(void *pvParams)
{
    char msg[REMOTE_BUFFER_SIZE];

    for (;;) {
        if (xQueueReceive(q_remote_tx, msg, portMAX_DELAY) == pdTRUE) {
            // Copio msg → reply_buffer
            taskENTER_CRITICAL();
            strncpy(reply_buffer, msg, sizeof(reply_buffer));
            reply_buffer[REMOTE_BUFFER_SIZE - 1] = 0;
            reply_len = strlen(reply_buffer);
            taskEXIT_CRITICAL();
        }
    }
}

// =====================================================
//                      LED RUN
// =====================================================
void task_LedRun(void *pvParams)
{
    bool led_state = true;
    for(;;){
        gpio_put(LED_RUN_PIN, led_state ^= true);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// =====================================================
//                        MAIN
// =====================================================
int main()
{
    // NO usar stdio_init_all(): rompe el I2C0 slave por USB-CDC
    stdio_uart_init_full(UART_ID, 115200, UART_TXPIN, UART_RXPIN);
    sleep_ms(500);
    printf("Pico2 I2C0 SLAVE echo + UART0 debug\n");

    // Init LED RUN
    gpio_init(LED_RUN_PIN);
    gpio_set_dir(LED_RUN_PIN, true);
    gpio_put(LED_RUN_PIN, true);
    
    // creo queues de comunicacion remota (i2c slave)
    q_remote_rx = xQueueCreate(5, REMOTE_BUFFER_SIZE);
    q_remote_tx = xQueueCreate(10, REMOTE_BUFFER_SIZE);

    // ---- Init I2C0 en modo master (velocidad) ----
    // pico_i2c_slave se encarga después de pasarlo a slave
    // i2c_init(i2c0, I2C0_BAUD);

    gpio_set_function(I2C0_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C0_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C0_SDA_PIN);
    gpio_pull_up(I2C0_SCL_PIN);

    printf("SDA FUNC=%d SCL FUNC=%d\n",
           gpio_get_function(I2C0_SDA_PIN),
           gpio_get_function(I2C0_SCL_PIN));

    // ---- Pasar I2C0 a modo SLAVE usando la librería ----
    i2c_slave_init(i2c0, I2C0_ADDR, ISR_i2c_slave);

    printf("I2C0 SLAVE listo en addr 0x%02X\n", I2C0_ADDR);
    printf("Esperando comandos desde master...\n");

    // Creo tareas
    xTaskCreate(task_LedRun, "RUN", 128, NULL, 1, NULL);
    xTaskCreate(task_REMOTE_RX, "I2C-RX", 512, NULL, 2, NULL);
    xTaskCreate(task_REMOTE_TX, "I2C-TX", 512, NULL, 1, NULL);
    
    // START SCHEDULER
    vTaskStartScheduler();
    while (1);
}
