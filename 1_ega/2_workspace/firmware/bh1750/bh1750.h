#include "hardware/i2c.h"

#define BH1750_ADDR 0x23

uint8_t bh1750_init(void);
uint16_t bh1750_read(void);