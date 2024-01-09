#ifndef USB_HOST_H
#define USB_HOST_H

#include <stdbool.h>
#include <stdint.h>

#define USB_DEVICES 64

typedef enum {
  // The device has not yet been tested.
  DEVICE_UNKNOWN = 0x00,

  // Switch to BootSelect mode.
  DEVICE_BOOTSEL_REQUEST,
  DEVICE_BOOTSEL_COMPLETE,
  // Request data transmition from the Web client.
  DEVICE_FLASH_REQUEST,
  // Device is processing the transmitted data.
  DEVICE_FLASH_DISK_INIT,
  DEVICE_FLASH_DISK_READ_BUSY,
  DEVICE_FLASH_DISK_WRITE_BUSY,
  DEVICE_FLASH_DISK_IO_COMPLETE,
  // Data succesfully flashed to the device.
  DEVICE_FLASH_COMPLETE,

  // The device failed to answer when trying flash it.
  DEVICE_ERROR_BOOTSEL_MISS = 0x10,
  // An error occured while flashing the content to the USB drive.
  DEVICE_ERROR_FLASH_INQUIRY,
  DEVICE_ERROR_FLASH_MOUNT,
  DEVICE_ERROR_FLASH_OPEN,
  DEVICE_ERROR_FLASH_WRITE,
  DEVICE_ERROR_FLASH_CLOSE,

  // Set when the device has been disconnected while not being previously
  // terminated properly with DEVICE_FLASH_COMPLETE.
  DEVICE_DISCONNECTED,

  // Bit flags.
  DEVICE_IS_ERROR = 0x10,
  DEVICE_TUH_MOUNTED = 0x20,
  DEVICE_MSC_MOUNTED = 0x40,
  DEVICE_CDC_MOUNTED = 0x80,

  DEVICE_IS_MOUNTED = 0xe0,
} usb_status_t;

usb_status_t get_usb_device_status(size_t d);

// Given an uintptr_t as argument, which represent the index of the USB port to
// enable, switch off the currently active device and enable the requested
// device.
void select_device_cb(void* arg);

// This function will setup the USB device based on pio pins and would block the
// thread it is spawned on.
void usb_host_setup();

void usb_copy_file_chunk(const uint8_t* buf, size_t len);

// -------------------------------------------------------------------
// List of callbacks to use as tasks by the Web server.

// Start opening a file for flashing the content.
void open_file(void*);

// Request to flash part of the content to the USB device.
void write_file_content(void*);
void stream_file_content(void*);

// Close the file once the flash request is ended.
void close_file(void*);

#endif // !USB_HOST_H
