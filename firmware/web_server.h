#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <stdbool.h>

// Setup the web server which then uses IRQ to interrut and call the signal
// handler each time a response has to be made.
//
// This handle the Wifi, TCP and HTTP stack as well what the setup of the
// content provided and handled by the web server.
bool web_server_setup();

// Stop the Wifi and the web server.
void web_server_stop();

// Execute the tasks coming from the network or coming from the other core.
void web_server_loop();

// -------------------------------------------------------------------
// List of callbacks to use as tasks by the USB interactions.

// Ask the web client to send the uf2 images back to us.
void request_flash(void*);

// Report any error to write on the USB device.
void write_error(void*);

#endif // !WEB_SERVER_H
