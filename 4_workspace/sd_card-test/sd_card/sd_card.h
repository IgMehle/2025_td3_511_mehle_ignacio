#ifndef SD_CARD_H
#define SD_CARD_H

#include "ff.h" // FatFs
#include "diskio.h"
#include <stdbool.h>

#include "hardware/spi.h"
#include "pico/stdlib.h"

bool sd_init();
// bool sd_write_file(const char *filename, const char *text);
bool sd_append_line(const char *filename, const char *text);
void sd_unmount();

#endif