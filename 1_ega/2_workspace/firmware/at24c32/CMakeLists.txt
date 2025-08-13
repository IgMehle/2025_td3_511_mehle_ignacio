### AT24C32 8X32K EEPROM LIBRARY - PICO 2 ###

# Creo biblioteca estatica at24c32
add_library(at24c32 STATIC at24c32.c)

# Linkeo dependencias
target_link_libraries(at24c32 PUBLIC hardware_i2c)

# Incluyo headers
target_include_directories(at24c32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})