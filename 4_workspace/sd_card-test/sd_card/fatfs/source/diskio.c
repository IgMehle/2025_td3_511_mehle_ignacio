#include "diskio.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <string.h>

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define SECTOR_SIZE 512

static bool sd_spi_initialized = false;

// 🔧 Función auxiliar: transferir un solo byte por SPI
static uint8_t spi_xfer(uint8_t data) {
    uint8_t rx;
    spi_write_read_blocking(SPI_PORT, &data, &rx, 1);
    return rx;
}

// 🔧 Función auxiliar: selecciona SD
static inline void sd_select() {
    gpio_put(PIN_CS, 0);
}

// 🔧 Función auxiliar: deselecciona SD
static inline void sd_deselect() {
    gpio_put(PIN_CS, 1);
}

// 🧪 Inicialización básica SPI
static int sd_spi_init() {
    if (sd_spi_initialized) return 0;

    spi_init(SPI_PORT, 400 * 1000); // 400 kHz al inicio
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);

    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    sd_deselect();

    // Clocks en alto (idle), envía 80 ciclos sin seleccionar para "despertar" SD
    for (int i = 0; i < 10; i++) spi_xfer(0xFF);

    sd_spi_initialized = true;
    return 0;
}

// 📦 Implementación mínima requerida por FatFs
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;
    return (sd_spi_init() == 0) ? 0 : STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv) {
    return (pdrv == 0) ? 0 : STA_NOINIT;
}

// 🔴 Estas funciones son **placeholders** por ahora
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;

    // ⚠️ Aún no implementado: deberías aquí llamar a tu rutina real de lectura SD
    memset(buff, 0xFF, SECTOR_SIZE * count);  // dummy data
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;

    // ⚠️ Aún no implementado: deberías aquí llamar a tu rutina real de escritura SD
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) return RES_PARERR;

    switch (cmd) {
        case CTRL_SYNC:  return RES_OK;
        case GET_SECTOR_COUNT: *((DWORD *)buff) = 32768; return RES_OK; // 16MB
        case GET_SECTOR_SIZE:  *((WORD  *)buff) = SECTOR_SIZE; return RES_OK;
        case GET_BLOCK_SIZE:   *((DWORD *)buff) = 1; return RES_OK;
        default: return RES_PARERR;
    }
}
