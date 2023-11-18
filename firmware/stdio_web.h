
#ifndef STDIO_WEB_H
#define STDIO_WEB_H

#include <stdint.h>
#include <stdbool.h>

// Dump the content of stdout to the `insert_at` buffer, and return the len
// which has been copied.
uint16_t stdout_ssi(char *insert_at, int ins_len);

// Initialize the HTTPD stdout output. Register the stdio_driver which is used
// as a backend for printing functions.
bool stdio_init_web(void);

#endif // !STDIO_WEB_H
