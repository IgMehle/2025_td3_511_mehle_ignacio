#include "hardware/i2c.h"

#define AT24C32_ADDR 0x50
#define EEPROM_PAGE_SIZE 0x20

uint8_t eeprom_wait_ready(void);

/// @brief Escribe bytes en la eeprom
/// @param data Puntero al array de bytes a escribir en la eeprom
/// @param address Direccion interna de memoria a escribir
/// @param bytes Cantidad de bytes a escribir
/// @return 1 si se escriben los bytes OK, 0 si hay algun error
uint8_t eeprom_write(uint8_t *data, uint16_t address, uint8_t bytes);

/// @brief Lee bytes de la eeprom
/// @param data Puntero al array de bytes donde escribo los bytes leidos
/// @param address Direccion interna de memoria a leer
/// @param bytes Cantidad de bytes a leer
/// @return 1 si se leen los bytes OK, 0 si hay algun error
uint8_t eeprom_read(uint8_t *data, uint16_t address, uint8_t bytes);