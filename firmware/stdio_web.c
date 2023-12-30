#include <string.h>
#include <pico/stdio/driver.h>
#include <pico/time.h>
#include <pico/mutex.h>

#define OUT_SIZE 8192

typedef struct {
  size_t start, end;
  mutex_t mutex;
  char buffer[OUT_SIZE];
} stdio_pipe_t;

static stdio_pipe_t stdout;

static absolute_time_t timeout() {
  return make_timeout_time_ms(PICO_STDIO_DEADLOCK_TIMEOUT_MS);
}

uint16_t stdout_ssi(char *insert_at, int ins_len)
{
  if (!mutex_try_enter_block_until(&stdout.mutex, timeout())) {
    return 0;
  }

  // Truncate to what is stored in the buffer.
  size_t len = (size_t) ins_len;
  if (stdout.start <= stdout.end) {
    if (stdout.end - stdout.start < len) {
      len = stdout.end - stdout.start;
    }
  } else {
    if (stdout.end + OUT_SIZE - stdout.start < len) {
      len = stdout.end + OUT_SIZE - stdout.start;
    }
  }

  size_t half = 0;
  if (stdout.start + len >= OUT_SIZE) {
    half = OUT_SIZE - stdout.start;
    memcpy(insert_at, &stdout.buffer[stdout.start], half);
    stdout.start = 0;
    len -= half;
  }
  if (len) {
    memcpy(&insert_at[half], &stdout.buffer[stdout.start], len);
    stdout.start += len;
  }
  mutex_exit(&stdout.mutex);
  return (uint16_t) len;
}

void stdio_web_out_chars(const char *buf, int length)
{
  if (!mutex_try_enter_block_until(&stdout.mutex, timeout())) {
    return;
  }
  size_t len = (size_t) length;
  if (len >= OUT_SIZE) {
    // Truncate the output as everything would be erased in the circular buffer.
    buf = buf + len - OUT_SIZE;
    len = OUT_SIZE;
    // Clear the content of the buffer and restart from scratch.
    stdout.start = 0;
    stdout.end = 0;
  }

  size_t half = 0;
  if (stdout.end + len >= OUT_SIZE) {
    half = OUT_SIZE - stdout.end;
    memcpy(&stdout.buffer[stdout.end], buf, half);
    if (stdout.start > stdout.end) {
      stdout.start = 1;
    }
    stdout.end = 0;
    len -= half;
  }
  if (len) {
    memcpy(&stdout.buffer[stdout.end], &buf[half], len);
    if (stdout.start > stdout.end && stdout.start <= stdout.end + len) {
      stdout.start = (stdout.end + len + 1) % OUT_SIZE;
    }
    stdout.end += len;
  }
  mutex_exit(&stdout.mutex);
}

int stdio_web_in_chars(char *buf, int length)
{
  // At the moment we do not plan on adding anything here, but if we wanted to
  // do, we would need to use httpd GET/POST request to get some inputs.
  return PICO_ERROR_NO_DATA;
}

stdio_driver_t stdio_web = {
    .out_chars = stdio_web_out_chars,
    .in_chars = stdio_web_in_chars,
};

bool stdio_init_web(void)
{
  mutex_init(&stdout.mutex);
  stdout.start = 0;
  stdout.end = 0;
  stdio_set_driver_enabled(&stdio_web, true);
  return true;
}
