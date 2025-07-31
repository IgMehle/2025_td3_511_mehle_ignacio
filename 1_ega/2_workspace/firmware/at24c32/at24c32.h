#include "hardware/i2c.h"

#define AT24C32_ADDR 0x50

/*! \brief Escribe bytes en la eeprom
 *  \ingroup at24c32
 *  
 * \param data Puntero al array de bytes a escribir en la eeprom
 * \param address Direccion de 16bit de la memoria interna donde vamos a escribir
 * \param bytes Cantidad de bytes a escribir
 * 
 * \return TRUE si se escriben los bytes OK, FALSE si hay algun error
 */
uint8_t eeprom_write(uint8_t *data, uint16_t address, uint8_t bytes);

/// @brief Lee bytes de la eeprom
/// @param data Puntero al array de bytes donde escribo los bytes leidos
/// @param address Direccion de 16bit de la memoria interna donde vamos a leer
/// @param bytes Cantidad de bytes a leer
/// @return TRUE si se leen los bytes OK, FALSE si hay algun error
uint8_t eeprom_read(uint8_t *data, uint16_t address, uint8_t bytes);