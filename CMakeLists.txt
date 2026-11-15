cmake_minimum_required(VERSION 3.13)

include(cmake/pico_sdk_import.cmake)

# Waveshare RP2350-USB-CM is an RP2350A board. Pins for the PIO-USB port and the
# WS2812 status LED are configured in the firmware, so the generic pico2 board
# definition is sufficient.
if (NOT PICO_BOARD)
    set(PICO_BOARD pico2 CACHE STRING "Board type")
endif()

# Location of the Pico-PIO-USB library (host stack for the RP2350 PIO USB port).
if (NOT DEFINED PICO_PIO_USB_PATH)
    set(PICO_PIO_USB_PATH "${CMAKE_CURRENT_LIST_DIR}/../Pico-PIO-USB")
endif()

project(pico_rcm_injector C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Pico-PIO-USB: defines the `pico_pio_usb` target and generates usb_tx/usb_rx
# PIO headers. Must be added after pico_sdk_init().
add_subdirectory(${PICO_PIO_USB_PATH} pico_pio_usb)

if (NOT PICO_TINYUSB_PATH)
    set(PICO_TINYUSB_PATH ${PICO_SDK_PATH}/lib/tinyusb)
endif()

add_executable(pico_rcm_injector
    src/main.c
    src/rcm_injector.c
    src/status_led.c
    # USB device side: MSC drag-and-drop payload drive on the native USB port.
    src/usb_descriptors.c
    src/msc_disk.c
    src/payload_store.c
    # TinyUSB's PIO-USB host controller driver shim (kept in the SDK tree).
    ${PICO_TINYUSB_PATH}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
)

# Generate the WS2812 PIO header for the status LED.
pico_generate_pio_header(pico_rcm_injector ${CMAKE_CURRENT_LIST_DIR}/src/ws2812.pio)

target_include_directories(pico_rcm_injector PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Tell the Pico-PIO-USB sources (compiled into this target) to integrate with
# the TinyUSB host stack rather than run standalone.
target_compile_definitions(pico_rcm_injector PRIVATE
    PIO_USB_USE_TINYUSB
)

target_link_libraries(pico_rcm_injector
    pico_stdlib
    pico_multicore
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_flash
    pico_flash
    tinyusb_device
    tinyusb_host
    tinyusb_board
    pico_pio_usb
)

# The RP2350 native USB peripheral is the flashing receptacle; the target is on
# the PIO-USB port. UART carries the logs.
pico_enable_stdio_usb(pico_rcm_injector 0)
pico_enable_stdio_uart(pico_rcm_injector 1)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(TARGET pico_rcm_injector POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:pico_rcm_injector> ${CMAKE_CURRENT_BINARY_DIR}/pico_rcm_injector.bin
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/bin2uf2.py
            --family rp2350-arm-s
            ${CMAKE_CURRENT_BINARY_DIR}/pico_rcm_injector.bin
            ${CMAKE_CURRENT_BINARY_DIR}/pico_rcm_injector.uf2
    BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/pico_rcm_injector.bin
            ${CMAKE_CURRENT_BINARY_DIR}/pico_rcm_injector.uf2
    COMMENT "Generating pico_rcm_injector.bin and pico_rcm_injector.uf2"
    VERBATIM
)
