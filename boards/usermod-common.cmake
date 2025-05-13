if(NOT DEFINED PIMORONI_PICO_PATH)
set(PIMORONI_PICO_PATH ${CMAKE_CURRENT_LIST_DIR}/../pimoroni-pico)
endif()
include(${PIMORONI_PICO_PATH}/pimoroni_pico_import.cmake)

include_directories(${CMAKE_CURRENT_LIST_DIR}/..)
include_directories(${PIMORONI_PICO_PATH}/micropython)

# Allows us to find /modules/c/<module>/micropython
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/..")

list(APPEND CMAKE_MODULE_PATH "${PIMORONI_PICO_PATH}/micropython")
list(APPEND CMAKE_MODULE_PATH "${PIMORONI_PICO_PATH}/micropython/modules")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Essential
include(pimoroni_i2c/micropython)
include(pimoroni_bus/micropython)

# Pico Graphics Essential
include(hershey_fonts/micropython)
include(bitmap_fonts/micropython)
include(picovector/micropython)
include(modules/picographics/micropython)

# Pico Graphics Extra
include(jpegdec/micropython)
include(pngdec/micropython)
include(qrcode/micropython/micropython)

# Sensors & Breakouts
include(micropython-common-breakouts)
include(pcf85063a/micropython)

# Pico Synth
include(modules/picosynth/micropython)

# Utility
include(adcfft/micropython)

include(modules_py/modules_py)

# C++ Magic Memory
include(cppmem/micropython)
target_compile_definitions(usermod INTERFACE
    CPP_FIXED_HEAP_SIZE=256)

# Disable build-busting C++ exceptions
include(micropython-disable-exceptions)