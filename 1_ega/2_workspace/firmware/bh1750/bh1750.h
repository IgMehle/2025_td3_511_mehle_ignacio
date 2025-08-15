#include "hardware/i2c.h"

#define BH1750_ADDR     0x23
#define BH1750_HIRES    0x10
#define BH1750_LORES    0x13

uint8_t bh1750_init(uint8_t mode);
uint16_t bh1750_read(void);