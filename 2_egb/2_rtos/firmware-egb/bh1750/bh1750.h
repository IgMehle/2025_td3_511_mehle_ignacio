#include "hardware/i2c.h"

#define BH1750_ADDR     0x23
#define CONT_HIRES      0x10
#define CONT_LORES      0x13
#define ONESHOT_HIRES   0x20
#define ONESHOT_LORES   0x23

/// @brief Inicializa el luxometro BH1750
/// @param mode Modo de resolucion
/// @return 1 si inicializa OK, 0 si hubo algun error
uint8_t bh1750_init(uint8_t mode);

/// @brief Devuelve el valor de luz leido
/// @param  
/// @return Valor de lux 0-65535
uint16_t bh1750_read(void);