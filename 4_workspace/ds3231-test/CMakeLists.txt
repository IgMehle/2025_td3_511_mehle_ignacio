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
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(ds3231-test C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(ds3231-test ds3231-test.c )

pico_set_program_name(ds3231-test "ds3231-test")
pico_set_program_version(ds3231-test "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(ds3231-test 0)
pico_enable_stdio_usb(ds3231-test 1)

# Agrego subdirectorio de librerias
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/ds3231 ${CMAKE_BINARY_DIR}/ds3231)

# Add the standard library to the build
target_link_libraries(ds3231-test
        pico_stdlib
        # Agrego librerias
        hardware_i2c
        ds3231)

# Add the standard include files to the build
target_include_directories(ds3231-test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(ds3231-test)

