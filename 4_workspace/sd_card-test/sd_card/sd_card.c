#include "sd_card.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

static FATFS fs;
static FIL file;

bool sd_init() {
    spi_init(SPI_PORT, 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1); // Deselect SD card

    sleep_ms(100);

    FRESULT res = f_mount(&fs, "", 1);
    return (res == FR_OK);
}

// bool sd_write_file(const char *filename, const char *text) {
//     FRESULT fr = f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
//     if (fr != FR_OK) return false;

//     UINT bw;
//     fr = f_write(&file, text, strlen(text), &bw);
//     f_close(&file);

//     return (fr == FR_OK && bw == strlen(text));
// }

bool sd_append_line(const char *filename, const char *text) {
    FRESULT fr = f_open(&file, filename, FA_OPEN_APPEND | FA_WRITE);
    if (fr != FR_OK) return false;

    UINT bw;
    fr = f_write(&file, text, strlen(text), &bw);
    f_close(&file);

    return (fr == FR_OK && bw == strlen(text));
}

void sd_unmount() {
    f_mount(NULL, "", 1);
}