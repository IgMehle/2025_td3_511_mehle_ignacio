#include "at24c32.h"

uint8_t eeprom_write(uint8_t *data, uint16_t address, uint8_t bytes)
{
    uint8_t len = bytes + 2;
    // Frame = Address + Data
    uint8_t frame[len];
    uint8_t offset;
    static uint8_t stat;
    static int resp;

    
    stat = 0;
    // Calculo address
    offset = address % EEPROM_PAGE_SIZE;
    //address = page*EEPROM_PAGE_SIZE + offset;
    // Chequeo que no me desbordo del limite de pagina
    if ((bytes + offset) <= 32){
        // Desempaqueto la direccion de memoria a la que escribir
        frame[0] = (uint8_t)((address >> 8) & 0x00FF);
        frame[1] = (uint8_t)(address & 0x00FF);
        // Cargo los datos en la trama
        for(uint8_t i = 0; i<bytes; i++) frame[i+2] = data[i];
        // frame[2] = data[0];
        // frame[3] = data[1];
        // frame[4] = data[2];
        // frame[5] = data[3];
        // Envio bytes a escribir
        resp = i2c_write_blocking(i2c0, AT24C32_ADDR, frame, len, false);
        // Tiempo de escritura en eeprom PROBAR
        sleep_ms(10);
        if (resp != len) stat = 1;
    }
    else stat = 1;
    return stat;
}

uint8_t eeprom_read(uint8_t *data, uint16_t address, uint8_t bytes)
{
    static uint8_t addr[2];
    static uint8_t stat;
    static int resp;
    stat = 0;

    // Desempaqueto la direccion de memoria a la que escribir
    addr[0] = (uint8_t)(address >> 8);
    addr[0] = (uint8_t)(address & 0x00FF);
    // Envio direccion de memoria de la eeprom
    resp = i2c_write_blocking(i2c0, AT24C32_ADDR, addr, 2, true);
    if (resp != 2) stat = 1;
    // Leo bytes de la eeprom
    resp = i2c_read_blocking(i2c0, AT24C32_ADDR, data, bytes, false);
    if (resp != bytes) stat = 1;
    return stat;
}