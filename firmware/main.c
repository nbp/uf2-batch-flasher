#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h> // calloc

#include "hardware/xosc.h"
#include "pico/stdlib.h"

#include "pico/bootrom.h" // reset_usb_boot

#include "usb_host.h"
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

  // Create a new stdio driver which buffers everything until the web
  // interface is queried.
  stdio_init_web();

  // Initialize pipe communication between the 2 cores.
  pipes_init();

  // Setup USB devices.
  usb_host_setup();

  // Setup the Web server.
  if (!web_server_setup()) {
    reset_usb_boot(0, 0);
    return 1;
  }

  // Core 0 main loop, waiting for UART / USB input.
  while (true) {
    web_server_task();
  }

  return 0;
}
