#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
// HEADERS PERIFERICOS
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

// HEADERS MODULOS
#include "bmp280.h"
#include "lcd.h"
// HEADERS FREERTOS
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

//----- PINOUT ----- //
#define BUTTON_PIN  17
#define LED_PWM_PIN 16
#define I2C_SDA_PIN 18
#define I2C_SCL_PIN 19
#define LED_RUN     25

// Parametros de compensacion del BMP280
struct bmp280_calib_param params_bmp;

// Struct de la cola del LCD
typedef struct {
    float temperatura;
    int32_t presion;
} q_lcd_t;

// Handle mutex I2C
SemaphoreHandle_t sm_i2c;
// Handle queue LCD
QueueHandle_t q_lcd;
// Handle semaforo IRQ pulsador
SemaphoreHandle_t s_puls;
// Handle queue cambio de pantalla
QueueHandle_t q_screen;

//----- IRQ PULSADOR -----//
void puls_irq(uint gpio, uint32_t event_mask)
{
    static BaseType_t wake_hp_task = pdTRUE;
    static uint8_t lcd_screen = 0;

    if(lcd_screen == 0) lcd_screen = 1;
    else lcd_screen = 0;

    //irq_clear(GPIO_IRQ_EDGE_FALL);
    // Envio indicacion de que pantalla tengo que mostrar
    xQueueSendFromISR(q_screen, &lcd_screen, &wake_hp_task);
}

uint16_t led_pwm(float temperatura, float setpoint){
    static int fade;
    float error;
    // Calculo el error absoluto
    error = temperatura - setpoint;
    // Tomo valor absoluto
    if(error < 0.0) error = -error;
    // Calculo error relativo * 2,55
    fade = (int)(255.0 * error / setpoint);
    // Limito fade a 255
    if(fade > 255) fade = 255;
    // Devuelvo valor real de PWM
    // El PWM esta configurado en 16 bits
    // Se calcula el PWM de manera cuadratica
    // Para obtener una variacion de brillo mas lineal
    // Fuente: github.com/raspberrypi/pico-examples/pwm/led_fade
    return (uint16_t)(fade*fade);
}

static void task_BMP280(void *pvParams)
{
    q_lcd_t bf;
    int32_t raw_temp, raw_press;
    int brillo;

    while(1){
        // Intento tomar el Mutex para usar el bus I2C
        if(xSemaphoreTake(sm_i2c, portMAX_DELAY) == pdPASS){
            // Leo valores en crudo de temperatura y presion
            bmp280_read_raw(&raw_temp, &raw_press);
            //printf("Leo BMP280\n");
            // Devuelvo el Mutex
            xSemaphoreGive(sm_i2c);
            // Convierto valores y guardo en el buffer
            bf.temperatura = bmp280_convert_temp(raw_temp, &params_bmp);
            bf.presion = bmp280_convert_pressure(raw_press, raw_temp, &params_bmp);
            // Envio datos a la cola del lcd
            xQueueSend(q_lcd, &bf, portMAX_DELAY);
        };
        // Levanto datos cada 200ms
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void task_LCD(void *pvParams)
{
    q_lcd_t bf;
    char line0[16], line1[16];
    uint16_t brillo = 0;
    float setpoint = 25.0;

    while(1){
        if(xQueueReceive(q_lcd, &bf, portMAX_DELAY) == pdPASS){
            // Formateo los datos para mostrar en LCD
            sprintf(line0, "Temp: %.1f °C", bf.temperatura);
            sprintf(line1, "Presion: %d hPa", bf.presion);
            // Intento tomar el mutex del bus I2C
            if(xSemaphoreTake(sm_i2c, portMAX_DELAY) == pdPASS){
                // Imprimo ambas lineas del LCD
                //lcd_set_cursor(0, 0);
                //lcd_string(line0);
                //lcd_set_cursor(1, 0);
                //lcd_string(line1);
                printf("%s %s",line0, line1);
                // Devuelvo el mutex
                xSemaphoreGive(sm_i2c);
            }
            // Actualizo PWM led de error
            brillo = led_pwm(bf.temperatura, setpoint);
            pwm_set_gpio_level(LED_PWM_PIN, brillo);
        }
    }
}

static void task_Run(void *pvParams)
{
    while(1){
        gpio_put(LED_RUN, true);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_put(LED_RUN, false);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main()
{
    stdio_init_all();

    // Init led de run
    gpio_init(LED_RUN);
    gpio_set_dir(LED_RUN, true);

    //----- config GPIO -----//
    // INIT GPIO INPUT PIN
    gpio_init(BUTTON_PIN);
    // GPIO como entrada
    gpio_set_dir(BUTTON_PIN, false);
    // Set pulldown
    gpio_set_pulls(BUTTON_PIN, true, false);
    // Habilito la IRQ del pin
    //gpio_set_irq_enabled_with_callback(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &puls_irq);
    
    //----- config PWM -----//
    // Asigno salida PWM al LED
    gpio_set_function(LED_PWM_PIN, GPIO_FUNC_PWM);
    // Guardar que slice de PWM le corresponde al pin
    uint slice_num = pwm_gpio_to_slice_num(LED_PWM_PIN);
    // Get some sensible defaults for the slice configuration. By default, the
    // counter is allowed to wrap over its maximum range (0 to 2**16-1)
    pwm_config config = pwm_get_default_config();
    // Set divider, reduces counter clock to sysclock/this value
    pwm_config_set_clkdiv(&config, 4.f);
    // Load the configuration into our PWM slice, and set it running.
    pwm_init(slice_num, &config, true);


    //----- config I2C -----//
    // I2C1 a 100khz
    i2c_init(i2c1, 100000);
    // I2C1_SDA en GPIO18
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    // I2C1_SCL en GPIO19
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    // Pongo pullups a 3V3
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // ----- config BMP280 -----//
    // Inicializa el BMP280 usando el I2C1
    bmp280_init(i2c1);
    // Obtiene parámetros de compensación
    struct bmp280_calib_param params_bmp;
    bmp280_get_calib_params(&params_bmp);

    // Inicializo LCD
    lcd_init(i2c1, 0x27);
    lcd_clear();

    // Creo mutex para el bus I2C y lo libero
    sm_i2c = xSemaphoreCreateMutex();
    xSemaphoreGive(sm_i2c);
    // Creo queue para el LCD
    q_lcd = xQueueCreate(1, sizeof(q_lcd_t));
    // Creo semaforo binario para el pulsador
    s_puls = xSemaphoreCreateBinary();
    // Creo cola para cambio de pantalla con el pulsador
    q_screen = xQueueCreate(1, sizeof(uint8_t));

    // Creo tarea BMP280
    xTaskCreate(task_BMP280, "BMP280", 2*configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    // Creo tarea LCD
    xTaskCreate(task_LCD, "LCD", 2*configMINIMAL_STACK_SIZE, NULL, 2, NULL);
    // Creo tarea RUN
    xTaskCreate(task_Run, "RUN", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // START SCHEDULER
    vTaskStartScheduler();
    while(true);
}
