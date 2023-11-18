#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <pico/stdlib.h>

// Display a prompt and wait timeout_us for a character to be pressed. While
// waiting turn the PICO_DEFAULT_LED_PIN on to prompt that inputs are being
// awaited.
//
// Returns a character in the range of 0-255 or PICO_ERROR_TIMEOUT if timeout occurs.
int input_get_timeout_us(const char* prompt, uint32_t timeout_us);

// Display a prompt and blcok until a character is pressed. While waiting turn
// the PICO_DEFAULT_LED_PIN on to prompt that inputs are being awaited.
char input_get_blocking(const char* prompt);

// Blink the PICO_DEFAULT_LED_PIN as a single bit information.
void blink_ms_blocking(uint32_t on_ms, uint32_t off_ms);

// Use a character bits as the blinking pattern to be used.
void binary_blink_ms_blocking(uint8_t c);

void led_init();
void led_put(bool value);

#endif // ! defined INPUT_H
