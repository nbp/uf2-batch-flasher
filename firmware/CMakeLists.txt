set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
cmake_minimum_required(VERSION 3.21)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
if (PICO_SDK_VERSION_STRING VERSION_LESS "1.5.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.5.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        -Wconversion   # Warn about most implicit conversion. This is one of the most frequent source of errors
                       # Around promotion of unsigned type while manipulating signed values.
        )

project(uf2-batch-flasher)

if(CMAKE_BUILD_TYPE EQUAL Debug)
  target_compile_definitions(uf2-batch-flasher PRIVATE
    DEBUG=1
    CFG_TUSB_DEBUG=3
  )
endif()

# We rely on the Pico W to provide a Web interface which is capable of storing
# the image to be flashed and streaming it for each board to be flashed intead
# of running over the cycles of the flash memory which is on the Pico W board.
set(FAMILY rp2040)
set(BOARD pico_w)
set(PICO_BOARD pico_w)

# set(TINYUSB_FAMILY_PROJECT_NAME_PREFIX "tinyusb_dev_")

# Load Wi-Fi settings.
include(wifi.cmake)

# Initialize the SDK
list(APPEND PICO_CONFIG_HEADER_FILES
  # Register the panic function to be used.
  ${CMAKE_SOURCE_DIR}/panic.h
)
pico_sdk_init()

# Do No rely on TinyUSB
# Add pico-pio-usb library.
#set(PICO_PIO_USB_PATH $ENV{PICO_PIO_USB_PATH}/)
#add_subdirectory(${PICO_PIO_USB_PATH} pico_pio_usb)

# This function is provided by TinyUSB hw/bsp/rp2040/family.cmake
# and will compile the library tinyusb_pico_pio_usb.
set(PICO_PIO_USB_PATH $ENV{PICO_SDK_PATH}/lib/tinyusb/hw/mcu/raspberry_pi/Pico-PIO-USB/)
check_and_add_pico_pio_usb_support()

option(USE_WEB_SERVER "Use the HTTP server instead of the TCP server" OFF)

if (USE_WEB_SERVER)
  # Use a script to pack every file under the commander/fs directory as the root
  # of the Website served.
  message("Generate WebRoot to embed in the binary.")

  # Replaced the LwIP file by our local one in order to add the proper mime type
  # for JavaScript files.
  #set(MAKE_FS_DATA_SCRIPT $ENV{PICO_SDK_PATH}/lib/lwip/src/apps/http/makefsdata/makefsdata)
  set(MAKE_FS_DATA_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/makefsdata)
  execute_process(COMMAND
    perl ${MAKE_FS_DATA_SCRIPT}
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    ECHO_OUTPUT_VARIABLE
    ECHO_ERROR_VARIABLE
  )
  file(RENAME fsdata.c _webroot.c)
endif()

add_executable(uf2-batch-flasher)

# Make sure to not encode these in the repository.
target_compile_definitions(uf2-batch-flasher PRIVATE
  WIFI_SSID=\"${WIFI_SSID}\"
  WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
)

target_sources(uf2-batch-flasher PRIVATE
  main.c
  input.c
  panic.c
  usb_host.c
  pipe.c

  # As the main interface is the web interface, dump the stdout to a web page
  # which can be poll-ed for new content.
  stdio_web.c

  # FatFS library provide a set of function which are implemented given some
  # abstract implementation of disk-io functions. While diskio.h implementation
  # exists, it adds dispatch based on whether the disk is a USB, EMMC or RAM,
  # from which the last 2 are of no concern for our current use case.
  ${PICO_TINYUSB_PATH}/lib/fatfs/source/ff.c
  ${PICO_TINYUSB_PATH}/lib/fatfs/source/ffsystem.c
  ${PICO_TINYUSB_PATH}/lib/fatfs/source/ffunicode.c
)

target_include_directories(uf2-batch-flasher PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${PICO_TINYUSB_PATH}/lib/fatfs/source/
)

# Pull dependencies
target_link_libraries(uf2-batch-flasher
  PRIVATE

  # Provide USB host support to identify connected boards as USB Mass Storage
  # Class (MSC) for flashing, and as Communication Device Class (CDC) for
  # switching RP2040 to BOOTSEL mode.
  tinyusb_device
  tinyusb_host
  tinyusb_board

  # Additional library removed from TinyUSB but still referenced to provide the
  # PIO implementation for adding additional USB ports.
  tinyusb_pico_pio_usb

  # The TCP/IP stack is queried frequently by polling the interface manually As
  # the second core is dedicated to USB tasks, the first core is dedicated to
  # IP/TCP/HTTP tasks.
  pico_cyw43_arch_lwip_poll

  # Used to implement an HTTP server.
  pico_lwip_http

  pico_stdlib
  pico_stdio
  pico_time
  hardware_gpio
  hardware_pio
# DMA might be useful to implement a fast memcpy.
#  hardware_dma
  hardware_watchdog
)

if (USE_WEB_SERVER)
  target_sources(uf2-batch-flasher PRIVATE
    web_server.c
  )

  target_link_libraries(uf2-batch-flasher PRIVATE
    # Used to implement an HTTP server.
    pico_lwip_http
  )
else()
  target_sources(uf2-batch-flasher PRIVATE
    tcp_server.c
  )
  target_compile_definitions(uf2-batch-flasher PRIVATE
    USE_TCP_SERVER=1
  )
endif()

# Enable stdin/stdout over UART.
pico_enable_stdio_usb(uf2-batch-flasher 0)
pico_enable_stdio_uart(uf2-batch-flasher 1)
target_compile_definitions(uf2-batch-flasher PRIVATE
  PICO_DEFAULT_UART_TX_PIN=16
  PICO_DEFAULT_UART_RX_PIN=17
)

# Create uf2/map/bin/hex/dis files.
pico_add_extra_outputs(uf2-batch-flasher)

# Add URL via pico_set_program_url
pico_set_program_url(uf2-batch-flasher "https://nbp.name/project/uf2-batch-flasher")
