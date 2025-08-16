#include "at24c32.h"

uint8_t eeprom_wait_ready() {
    uint8_t dummy = 0;
    for (int i = 0; i < 100; i++) {   // hasta ~10 ms
        int resp = i2c_write_blocking(i2c1, AT24C32_ADDR, &dummy, 0, false);
        if (resp >= 0) {
            return 1; // ACK recibido → ya está lista
        }
        sleep_us(100); // esperar 0.1 ms antes de reintentar
    }
    return 0; // timeout → error
}

uint8_t eeprom_write(uint8_t *data, uint16_t address, uint8_t bytes)
{
    uint8_t len = bytes + 2;
    // Frame = Address + Data
    uint8_t frame[len];
    uint8_t offset;
    static uint8_t status;
    static int resp;
    status = 1;
    
    // Chequeo que no voy a querer escribir mas que 32 bytes (eeprom page)
    offset = address % EEPROM_PAGE_SIZE;
    // if ((bytes+offset) <= 32){
    if ((bytes + offset) <= 32){
        // Desempaqueto la direccion de memoria a la que escribir
        frame[0] = (uint8_t)((address >> 8) & 0x00FF);
        frame[1] = (uint8_t)(address & 0x00FF);
        // Cargo los datos en la trama
        for(uint8_t i = 0; i<bytes; i++) frame[i+2] = data[i];
        // Envio bytes a escribir
        resp = i2c_write_blocking(i2c1, AT24C32_ADDR, frame, len, false);
        if (resp != len) status = 0;
        // Tiempo de escritura en eeprom PROBAR
        // sleep_ms(10);
    }
    else status = 0;
    return status;
}

uint8_t eeprom_read(uint8_t *data, uint16_t address, uint8_t bytes)
{
    static uint8_t addr[2];
    static uint8_t status;
    static int resp;
    status = 1;

    // Desempaqueto la direccion de memoria a la que escribir
    addr[0] = (uint8_t)(address >> 8);
    addr[1] = (uint8_t)(address & 0x00FF);
    // Envio direccion de memoria de la eeprom
    resp = i2c_write_blocking(i2c1, AT24C32_ADDR, addr, 2, true);
    if (resp != 2) status = 0;
    // Leo bytes de la eeprom
    resp = i2c_read_blocking(i2c1, AT24C32_ADDR, data, bytes, false);
    if (resp != bytes) status = 0;
    return status;
}