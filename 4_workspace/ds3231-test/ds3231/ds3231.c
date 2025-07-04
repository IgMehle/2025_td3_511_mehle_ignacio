#include "ds3231.h"

bool rtc_load(rtc_t data){
    int resp;
    uint8_t bf[8];
    bool status = true;

    bf[0] = 0x00; // RTC WRITE ADDRESS
    bf[1] = 0x7F & BIN2BCD(data.second);
    bf[2] = BIN2BCD(data.minute);
    bf[3] = BIN2BCD(data.hour) & 0x3F;;
    bf[4] = data.weekday;
    bf[5] = BIN2BCD(data.day);
    bf[6] = BIN2BCD(data.month);
    bf[7] = BIN2BCD(data.year);

    resp = i2c_write_blocking(i2c0, DS3231_ADDR, bf, 8, false);
    if(resp != 8) status = false;
    return status;
}

bool rtc_read(rtc_t *data){
    static int resp;
    static bool status;
    static uint8_t read_addr = 0x00;
    static uint8_t x[7];

    status = true;
    resp = i2c_write_blocking(i2c0, DS3231_ADDR, &read_addr, 1, true);
    if(resp == PICO_ERROR_GENERIC) status = false;
    resp = i2c_read_blocking(i2c0, DS3231_ADDR, x, 7, false);
    if(resp == PICO_ERROR_GENERIC) status = false;

    data->second = ((x[0] & 0x70) >> 4)*10 + (x[0] & 0x0F);
    data->minute = (x[1] >> 4)*10 + (x[1] & 0x0F);
    data->hour = ((x[2] & 0x20) >> 4)*10 + (x[2] & 0x0F);
    data->weekday = x[3];
    data->day = (x[4] >> 4)*10 + (x[4] & 0x0F);
    data->month = (x[5] >> 4)*10 + (x[5] & 0x0F);
    data->year = ((x[6] & 0xF0) >> 4)*10 + (x[6] & 0x0F);

    return status;
}