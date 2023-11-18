#ifndef PIPE_H
#define PIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*task_t)(void* arg);

// Return whether we can append len bytes in the pipe right now.
bool pipe_has_freespace(size_t len);

// Append content. block until the pipe has enough free-space to save the
// content.
void pipe_enqueue(const uint8_t* content, size_t len);

// Check if there is any data to be sent yet.
bool pipe_has_data(size_t len);

// dequeue the data from the pipe and move it to the output array, also free
// space to enqueue more incoming data.
void pipe_dequeue(uint8_t* output, size_t len);

// Queue tasks to be executed by the Web server.
bool queue_web_task(task_t cb, void *arg);

// Execute one of the queued task for the web server.
void exec_web_task();

// Queue tasks to be executed by the USB host.
bool queue_usb_task(task_t cb, void* arg);

// Execute one of the queued task for the USB host.
void exec_usb_task();

// Initialize all pipes.
void pipes_init();

#endif // !PIPE_H
