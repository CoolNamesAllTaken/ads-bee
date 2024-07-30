cmake_minimum_required(VERSION 3.13)

include(SetUpConfigurations.cmake)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)

if (NOT CMAKE_BUILD_TYPE STREQUAL "Test")
    ## Building for target.
    # Pull in SDK (must be before project)
    include(pico_sdk_import.cmake)
    if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
        message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
    endif()

    project(ads_bee C CXX ASM)
    # Flag indicating that build is for the embedded target. Used to toggle target_source calls.
    set(COMPILED_FOR_TARGET 1)

    # Differentiate common code between Pico and ESP32. For code highlighting, add this to the "defines" property of
    # "configurations" in c_cpp_properties.json.
    add_compile_definitions(ON_PICO)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        # set(PICO_DEOPTIMIZED_DEBUG 1)
        add_compile_definitions(HARDWARE_UNIT_TESTS)
    endif()

    # Initialize the SDK
    pico_sdk_init()
    add_executable(${PROJECT_NAME} "")

    add_subdirectory(../common adsbee_common)
    add_subdirectory(src)

    target_include_directories(${PROJECT_NAME} PRIVATE 
        ../common
        src
        /ads_bee/modules/cppAT/src
    )

    target_sources(${PROJECT_NAME} PRIVATE
        /ads_bee/modules/cppAT/src/cpp_at.cc
        # /ads_bee/modules/cppAT/src/cpp_at.hh
    )

    # Create C header file with the name <pio program>.pio.h
    pico_generate_pio_header(${PROJECT_NAME}  
    ${CMAKE_CURRENT_LIST_DIR}/pio/blink.pio 
    )
    pico_generate_pio_header(${PROJECT_NAME}  
    ${CMAKE_CURRENT_LIST_DIR}/pio/capture.pio       
    )

    # Firmware: Pull in Pico library
    target_link_libraries(${PROJECT_NAME} 
        pico_stdlib
        pico_float # for math functions
        hardware_pio
        hardware_pwm
        hardware_adc
        hardware_i2c
        hardware_spi
        hardware_exception
    )

    # Firmware: Create map/bin/hex file etc
    pico_add_extra_outputs(${PROJECT_NAME})

    # Set up debug USB
    pico_enable_stdio_usb(${PROJECT_NAME} 1) # use USB for standard printing
    pico_enable_stdio_uart(${PROJECT_NAME} 0) # disable STDIO UART
    
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

else()
    ## Building for unit tests on host.
    project(ads_bee_test C CXX ASM)

    set(CMAKE_BUILD_TYPE "Debug") # Now that we've identified we're running tests, revert to an actual build config.

    add_compile_options(
        -Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
    )

    add_executable(${PROJECT_NAME} "")

    add_subdirectory(../common adsbee_common)
    add_subdirectory(src)
    add_subdirectory(host_test)

    target_include_directories(${PROJECT_NAME} PRIVATE
        ../common
        /ads_bee/modules/googletest
        /ads_bee/modules/cppAT/src
        src
        host_test
    )

    target_sources(${PROJECT_NAME} PRIVATE
        /ads_bee/modules/cppAT/src/cpp_at.cc
        /ads_bee/modules/cppAT/test/test_cpp_at.cc
    )

    # Test: Pull in google test library
    add_library(libgtest SHARED IMPORTED)
    set_target_properties(libgtest PROPERTIES IMPORTED_LOCATION /ads_bee/modules/googletest/build/lib/libgtest.so)
    target_link_libraries(${PROJECT_NAME} PRIVATE libgtest)
    

endif()

# Set parameters for serial flasher.
# NOTE: These are mirrored in the top level `pico/.vscode/c_cpp_properties.json` for intellisense.
add_compile_definitions(
    MD5_ENABLED 
    SERIAL_FLASHER_INTERFACE_UART 
    SERIAL_FLASHER_DEBUG_TRACE=0 
    SERIAL_FLASHER_WRITE_BLOCK_RETRIES=3
)

# Embed binaries into the app.
# In ESP-IDF this can also be done using EMBED_FILES option of idf_component_register.
# Here an external tool is used to make file embedding similar with other ports.
include(${CMAKE_CURRENT_LIST_DIR}/src/esp32_flasher/bin2array.cmake)
create_resources(${CMAKE_CURRENT_LIST_DIR}/src/esp32_flasher/bin ${CMAKE_BINARY_DIR}/binaries.c)
set_property(SOURCE ${CMAKE_BINARY_DIR}/binaries.c PROPERTY GENERATED 1)
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_BINARY_DIR}/binaries.c)






