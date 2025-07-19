### BH1750FVI LIBRARY - PICO 2 ###

# Creo biblioteca estatica bh1750
add_library(bh1750 STATIC bh1750.c)

# Linkeo dependencias
target_link_libraries(bh1750 PUBLIC hardware_i2c)

# Incluyo headers
target_include_directories(bh1750 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})