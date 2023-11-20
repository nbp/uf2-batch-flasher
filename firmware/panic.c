#include "panic.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"

#include "input.h"

void __attribute__((noreturn)) verbose_panic(const char *fmt, ...) {
  char msg[4096];
  if (fmt) {
    va_list args;
    va_start(args, fmt);
    // TODO: Have a look at pico_runtime/runtime.c for inspiration if this fails
    // to compile.
    int n = vsprintf(msg, fmt, args);
    msg[n] = '\n';
    msg[n + 1] = 0;
    va_end(args);
  }

  while (true) {
    char* message = &msg[0];
    switch (input_get_timeout_us(message, 250 * 1000)) {
    case PICO_ERROR_TIMEOUT:
      sleep_ms(250);
      break;
    case 'b':
      __breakpoint();
      break;
    default:
      reset_usb_boot(0, 0);
      break;
    }
  }
}
