# Librería FatFs (fuente local)
add_library(fatfs STATIC
    fatfs/source/ff.c
    fatfs/source/ffsystem.c
    fatfs/source/ffunicode.c
    fatfs/source/diskio.c
)

target_include_directories(fatfs PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/fatfs/source
)

# Creo libreria estatica "sd_card" con los source
add_library(sd_card STATIC sd_card.c)

# Linkeo dependencias de la bibliotecas
target_link_libraries(sd_card PUBLIC
    pico_stdlib
    hardware_spi
    fatfs
)

# Incluir las cabeceras de la biblioteca
target_include_directories(sd_card PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)