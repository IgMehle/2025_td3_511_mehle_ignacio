### LIBRERIA PARA EL ENCODER ###

# Creo biblioteca estatica encoder
add_library(encoder STATIC encoder.c)

# Linkeo dependencias
target_link_libraries(encoder PUBLIC)

# Incluyo headers
target_include_directories(encoder PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})