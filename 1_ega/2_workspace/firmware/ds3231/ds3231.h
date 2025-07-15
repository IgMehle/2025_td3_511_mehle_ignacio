#include <stdio.h>
#include <stdbool.h>
#include "hardware/i2c.h"

#define DS3231_ADDR 0x68
#define BIN2BCD(x)		((uint8_t) ((((x / 10 ) << 4) & 0xF0) | ((x % 10) & 0x0F)))
#define BCD2BIN(x)		((uint8_t) (((x >> 4) * 10) + (x & 0x0F)))

//----- RTC_T for RTC -----//
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t weekday;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} rtc_t;

bool rtc_load(rtc_t);
bool rtc_read(rtc_t *);