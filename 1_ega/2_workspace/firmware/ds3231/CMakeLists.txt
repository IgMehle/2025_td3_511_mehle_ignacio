### DS3231 3V3 RTC LIBRARY - PICO 2 ###

# Creo biblioteca estatica ds3231
add_library(ds3231 STATIC ds3231.c)

# Linkeo dependencias
target_link_libraries(ds3231 PUBLIC hardware_i2c)

# Incluyo headers
target_include_directories(ds3231 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})