# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(uart-test C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(uart-test uart-test.c )

pico_set_program_name(uart-test "uart-test")
pico_set_program_version(uart-test "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(uart-test 0)
pico_enable_stdio_usb(uart-test 1)

# Configuración adicional de FreeRTOS de la aplicación (si el archivo existe)
set(FREERTOS_USER_CONFIG "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "User FreeRTOSConfig path")
# Habilito uso de memoria dinámica
set(FREERTOS_SUPPORT_DYNAMIC_ALLOCATION "ON" CACHE PATH "FreeRTOS dynamic memory implementation support")

# Add external FreeRTOS library
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../freertos ${CMAKE_BINARY_DIR}/freertos)

# Add the standard library to the build
target_link_libraries(uart-test
        pico_stdlib
        hardware_irq
        freertos)

# Add the standard include files to the build
target_include_directories(uart-test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(uart-test)