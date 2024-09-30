#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h> // calloc

#include "hardware/xosc.h"
#include "pico/stdlib.h"

#include "pico/bootrom.h" // reset_usb_boot

#include "usb_host.h"
#include "tcp_server.h"
#include "web_server.h"
#include "pipe.h"
#include "stdio_web.h"

#include "input.h"

int main() {
  // Initialize the cristall oscillator. This is necessary to have a minimal
  // clock drift and jitter. Then initialize the clock at a multiple of the USB
  // 12 Mbits/s (x10) for bit-banging USB using PIO.
  xosc_init();
  set_sys_clock_khz(120000, true);

  // Create stdio drivers to output printed content to either a UART interface
  // or the web interface.
  stdio_uart_init();
  printf("STDIO UART initialized!\n");
  stdio_init_web();
  printf("STDIO Web initialized!\n");

  // Initialize pipe communication between the 2 cores.
  pipes_init();
  printf("Pipes across cores initialized!\n");

  // Setup USB devices.
  usb_host_setup();

#if defined(USE_TCP_SERVER)
  // Setup the TCP server.
  if (!tcp_server_setup()) {
    reset_usb_boot(0, 0);
    return 1;
  }

  // Core 0 main loop.
  printf("Starting TCP server loop.\n");
  tcp_server_loop();
#else
  // Setup the Web server.
  if (!web_server_setup()) {
    reset_usb_boot(0, 0);
    return 1;
  }

  // Core 0 main loop.
  printf("Starting Web server loop.\n");
  web_server_loop();
#endif

  // Use the on-board LED to report the data status of the USB line.
  led_init();


  return 0;
}
