#include "bh1750.h"

uint8_t bh1750_init(void){
    uint8_t bf[2];
    uint8_t status = 1;
    int resp;
    // Escribo comando START = 0x01
    bf[0] = 0x01;
    resp = i2c_write_blocking(i2c1, BH1750_ADDR, bf, 1, false);
    if(resp != 1) status = 0;
    // Escribo comando HRES-MODE = 0x10
    bf[0] = 0x10;
    resp = i2c_write_blocking(i2c1, BH1750_ADDR, bf, 1, false);
    if(resp != 1) status = 0;
    return status;
}

uint16_t bh1750_read(void){
    static uint8_t bf[2];
    static uint32_t r;
    static float lux;
    static uint16_t res;
    // Vacio buffer de recepcion
    bf[0] = 0;
    bf[1] = 0;
    // Leo valor
    i2c_read_blocking(i2c1, BH1750_ADDR, bf, 2, false);
    // Acumulo
    r = 256*bf[0] + bf[1];
    // Paso a float y divido por factor
    lux = (float)(r);
    lux /= 1.2;
    // Cast a entero
    res = (uint16_t)(lux);
    return res;
}