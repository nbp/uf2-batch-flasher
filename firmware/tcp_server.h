
#if !defined(TCP_SERVER_H) && defined(USE_TCP_SERVER)
#define TCP_SERVER_H

#include <stdbool.h>

typedef enum {
  // REQUEST_STATUS is answered with UPDATE_STATUS
  REQUEST_STATUS = 0x00,

  // REQUEST_STDOUT is answered with UPDATE_STDOUT
  REQUEST_STDOUT,

  // SELECT_DEVICE is answered with UPDATE_STATUS.
  SELECT_DEVICE,

  // START_FLASH will open the file on the selected device and reply with
  // FLASH_START.
  START_FLASH,

  // WRITE_FLASH_PART will write part of the file on the flash and acknowledged
  // with FLASH_PART_RECEIVED and FLASH_PART_WRITTEN.
  WRITE_FLASH_PART,

  // END_FLASH will close the file on the flash and acknowledge with FLASH_END.
  END_FLASH,

  // Reboot the uf2-batch-flasher to be updated.
  REBOOT_FOR_FLASH,

  // Reboot the uf2-batch-flasher to reset it state.
  REBOOT_SOFT
} client_msg_t;

typedef enum {
  // Send the array of status of USB devices.
  UPDATE_STATUS = 0x80,

  // Send the content of the stdio which is buffered.
  UPDATE_STDOUT,

  // Report that the device is mounted as filesystem and that it is ready to be
  // flashed.
  // FLASH_READY,

  // Report that the file is ready to be written.
  FLASH_START,

  // Ackowledge a received part and reply with the number of free bytes in the
  // buffer, to transmit to data to the device.
  FLASH_PART_RECEIVED,

  // Ackowledge a received part has been written, and that there is now room for
  // more.
  FLASH_PART_WRITTEN,

  // Acknowledge that the file has been closed.
  FLASH_END,

  // Report some internal error while flashing.
  FLASH_ERROR,

  // Replied when a message has not been decoded properly.
  DECODE_FAILURE
} server_msg_t;

// Functions which are used to expose the internal buffer containing the content
// to be flashed. They can be executed from any thread.
uint8_t* get_postmsg_buffer(void* arg);
size_t get_postmsg_length(void* arg);
void* get_postmsg_usb_info(void* arg);

// Should be executed on the TCP thread using the task queue.
void free_postmsg(void* arg);

// This handle the Wifi, TCP stack setup.
bool tcp_server_setup();

// Stop the Wifi and the tcp server.
void tcp_server_stop();

// Execute the tasks coming from the network or coming from the other core.
void tcp_server_loop();

// -------------------------------------------------------------------
// List of callbacks to use as tasks by the USB interactions.

// Ask the tcp client to send the uf2 images back to us.
void request_flash(void*);

void report_file_opened(void*);
void report_file_closed(void*);

// Report any error to write on the USB device.
void write_error(void*);

#endif // !TCP_SERVER_H
