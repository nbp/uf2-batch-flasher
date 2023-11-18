#include "input.h"

#include <stdio.h>

#include "pico/stdlib.h"
#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

void led_init() {
  static int is_initialized = false;
  if (is_initialized) {
    return;
  }
#ifdef PICO_DEFAULT_LED_PIN
  const uint32_t LED_PIN = PICO_DEFAULT_LED_PIN;
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);
#endif
  is_initialized = true;
}

void led_put(bool value) {
#ifdef PICO_DEFAULT_LED_PIN
  gpio_put(PICO_DEFAULT_LED_PIN, value ? 1 : 0);
#endif
#ifdef CYW43_WL_GPIO_LED_PIN
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, value);
#endif
}

int input_get_timeout_us(const char* prompt, uint32_t timeout_us) {
  led_init();
  led_put(true);
  if (prompt[0] != 0) {
    printf("%s> ", prompt);
  }
  int c = getchar_timeout_us(timeout_us);
  led_put(false);
  if (c != PICO_ERROR_TIMEOUT) {
    printf("%c\n", c);
  }
  return c;
}

char input_get_blocking(const char* prompt) {
  led_init();
  led_put(true);
  printf("%s> ", prompt);
  char c = (char) getchar();
  led_put(false);
  printf("%c\n", c);
  return c;
}

void blink_ms_blocking(uint32_t on_ms, uint32_t off_ms) {
  led_init();
  led_put(true);
  sleep_ms(on_ms);
  led_put(false);
  if (off_ms) {
    sleep_ms(off_ms);
  }
}

void binary_blink_ms_blocking(uint8_t c) {
  led_init();
  if (c) {
    while (!(c & 0x80)) {
      c <<= 1;
    }
  }
  do {
    uint32_t ms = c & 0x80 ? 320 : 160;
    led_put(true);
    sleep_ms(ms);
    led_put(false);
    sleep_ms(500 - ms);
    c <<= 1;
  } while (c);
  sleep_ms(1000);
}
