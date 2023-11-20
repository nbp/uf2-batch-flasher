// This file is used by TinyUSB library to register the configuration of the board.

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

//--------------------------------------------------------------------
// COMMON CONFIGURATION
//--------------------------------------------------------------------

// defined by compiler flags for flexibility
#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#ifdef DEBUG
#define CFG_TUSB_DEBUG        3
#else
#define CFG_TUSB_DEBUG        0
#endif
#endif

//--------------------------------------------------------------------
// DEVICE CONFIGURATION (disabled)
//--------------------------------------------------------------------

// TODO: Enable the device CDC in order to switch to BOOTSEL mode as the
// stdio_usb library does using the baud rate, as well as re-implementing the
// stdio_usb output.

// RHPort number used for device can be defined by board.mk, default to port 0
#ifndef BOARD_TUD_RHPORT
#define BOARD_TUD_RHPORT      0
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUD_MAX_SPEED
#define BOARD_TUD_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

// Enable Device stack, Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_ENABLED       0
#define CFG_TUD_MAX_SPEED     BOARD_TUD_MAX_SPEED

// No Special section for the RP2040 while using DMA.
#define CFG_TUD_MEM_SECTION
#define CFG_TUD_MEM_ALIGN        __attribute__ ((aligned(4)))

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE    64
#endif

//------------- CLASS -------------//
#define CFG_TUD_CDC              0

// CDC FIFO size of TX and RX
#define CFG_TUD_CDC_RX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)
#define CFG_TUD_CDC_TX_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)

// CDC Endpoint transfer buffer size, more is faster
#define CFG_TUD_CDC_EP_BUFSIZE   (TUD_OPT_HIGH_SPEED ? 512 : 64)

//--------------------------------------------------------------------
// HOST CONFIGURATION
//--------------------------------------------------------------------

// RHPort number used for host can be defined by board.mk, default to port 1
#ifndef BOARD_TUH_RHPORT
#define BOARD_TUH_RHPORT      1
#endif

// RHPort max operational speed can defined by board.mk
#ifndef BOARD_TUH_MAX_SPEED
#define BOARD_TUH_MAX_SPEED   OPT_MODE_DEFAULT_SPEED
#endif

// Enable Host stack, Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUH_ENABLED       1
#define CFG_TUH_MAX_SPEED     BOARD_TUH_MAX_SPEED

// Use pico-pio-usb as host controller for raspberry rp2040
#define CFG_TUH_RPI_PIO_USB   1

// Size of buffer to hold descriptors and other data used for enumeration
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// No Special section for the RP2040 while using DMA.
#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN           __attribute__ ((aligned(4)))

//------------- CLASS -------------//

// The UF2 batch flasher has 64 ports, which should be good enough for now.
#define CFG_TUH_HUB                 1
#define CFG_TUH_DEVICE_MAX          1
// (CFG_TUH_HUB ? 4 : 1) // hub typically has 4 ports
// #define CFG_TUH_MSC_MAXLUN          1 // typically 4

// Setup the current Raspberry Pi Pico to be used as a host for reading the
// file system of the device.
#define CFG_TUH_MSC                 1 // Mass Storage Class

// The RP2040 can be configured by the host to be reprogrammed, using the
// following macros:
//  - PICO_STDIO_USB_ENABLE_RESET_VIA_VENDOR_INTERFACE
//  - PICO_STDIO_USB_RESET_INTERFACE_SUPPORT_RESET_TO_BOOTSEL
//  - PICO_STDIO_USB_ENABLE_RESET_VIA_BAUD_RATE
//
// These macros are enabled by default when stdio_usb is used.
#define CFG_TUH_CDC                 1 // Communication Device Class

#define CFG_TUH_HID                 0 // keyboard / mouse
#define CFG_TUH_MIDI                0 // MIDI streaming interface.
#define CFG_TUH_VENDOR              0
// #define CFG_TUH_HID_EPIN_BUFSIZE    64
// #define CFG_TUH_HID_EPOUT_BUFSIZE   64

#endif // !TUSB_CONFIG_H
