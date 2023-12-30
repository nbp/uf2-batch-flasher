#include <string.h> // memcpy
#include <pico/mutex.h>
#include "pipe.h"

#define BUFFER_SIZE 4096
typedef struct {
  mutex_t mutex;
  size_t start, end;
  uint8_t buffer[BUFFER_SIZE];
} pipe_t;

static pipe_t stream;

// Return whether we can append len bytes in the pipe right now.
bool pipe_has_freespace(size_t len) {
  mutex_enter_blocking(&stream.mutex);
  size_t freespace = (stream.start - stream.end + BUFFER_SIZE - 1) % BUFFER_SIZE;
  mutex_exit(&stream.mutex);
  return len < freespace;
}

// Append content. block until the pipe has enough free-space to save the
// content.
void pipe_enqueue(const uint8_t* content, size_t len) {
  while (!pipe_has_freespace(len)) {
    // Use conditional variable / semaphore to wait instead of using a
    // spin-loop.
  }

  mutex_enter_blocking(&stream.mutex);
  size_t content_offset = 0;
  if (stream.end + len >= BUFFER_SIZE) {
    content_offset = BUFFER_SIZE - stream.end;
    memcpy(&stream.buffer[stream.end], content, content_offset);
    stream.end = 0;
  }

  size_t rest = len - content_offset;
  if (rest > 0) {
    memcpy(&stream.buffer[stream.end], &content[content_offset], rest);
    stream.end += rest;
  }
  mutex_exit(&stream.mutex);
}

// Check if there is any data to be sent yet.
bool pipe_has_data(size_t len) {
  mutex_enter_blocking(&stream.mutex);
  size_t usedspace = (stream.end - stream.start + BUFFER_SIZE) % BUFFER_SIZE;
  mutex_exit(&stream.mutex);
  return len <= usedspace;
}

// dequeue the data from the pipe and move it to the output array, also free
// space to enqueue more incoming data.
void pipe_dequeue(uint8_t* output, size_t len) {
  while (!pipe_has_data(len)) {
    // Spin loop until more data is added.
  }

  mutex_enter_blocking(&stream.mutex);
  size_t output_offset = 0;
  if (stream.start + len >= BUFFER_SIZE) {
    output_offset = BUFFER_SIZE - stream.start;
    memcpy(output, &stream.buffer[stream.start], output_offset);
    len -= output_offset;
    stream.start = 0;
  }

  if (len) {
    memcpy(&output[output_offset], &stream.buffer[stream.start], len);
  }
  mutex_exit(&stream.mutex);
}

#define TASKS_SIZE 32
typedef struct {
  task_t task;
  void* arg;
} callback_t;

typedef struct {
  mutex_t mutex;
  volatile size_t start, end;
  callback_t buffer[TASKS_SIZE];
} callback_pipe_t;

callback_pipe_t web_tasks;
callback_pipe_t usb_tasks;

bool queue_web_task(task_t cb, void *arg) {
  mutex_enter_blocking(&web_tasks.mutex);
  if ((web_tasks.end + 1) % TASKS_SIZE == web_tasks.start) {
    mutex_exit(&web_tasks.mutex);
    return false;
  }

  web_tasks.buffer[web_tasks.end].task = cb;
  web_tasks.buffer[web_tasks.end].arg = arg;
  web_tasks.end = (web_tasks.end + 1) % TASKS_SIZE;
  mutex_exit(&web_tasks.mutex);
  return true;
}

void exec_web_task() {
  mutex_enter_blocking(&web_tasks.mutex);
  if (web_tasks.start == web_tasks.end) {
    mutex_exit(&web_tasks.mutex);
    return;
  }

  task_t cb = web_tasks.buffer[web_tasks.start].task;
  void* arg = web_tasks.buffer[web_tasks.start].arg;
  web_tasks.start = (web_tasks.start + 1) % TASKS_SIZE;
  mutex_exit(&web_tasks.mutex);

  cb(arg);
}

bool queue_usb_task(task_t cb, void* arg) {
  mutex_enter_blocking(&usb_tasks.mutex);
  if ((usb_tasks.end + 1) % TASKS_SIZE == usb_tasks.start) {
    mutex_exit(&usb_tasks.mutex);
    return false;
  }

  usb_tasks.buffer[usb_tasks.end].task = cb;
  usb_tasks.buffer[usb_tasks.end].arg = arg;
  usb_tasks.end = (usb_tasks.end + 1) % TASKS_SIZE;
  mutex_exit(&usb_tasks.mutex);
  return true;
}

void exec_usb_task() {
  mutex_enter_blocking(&usb_tasks.mutex);
  if (usb_tasks.start == usb_tasks.end) {
    mutex_exit(&usb_tasks.mutex);
    return;
  }

  task_t cb = usb_tasks.buffer[usb_tasks.start].task;
  void* arg = usb_tasks.buffer[usb_tasks.start].arg;
  usb_tasks.start = (usb_tasks.start + 1) % TASKS_SIZE;
  mutex_exit(&usb_tasks.mutex);

  cb(arg);
}

void pipes_init() {
  mutex_init(&stream.mutex);
  stream.start = 0;
  stream.end = 0;

  mutex_init(&web_tasks.mutex);
  web_tasks.start = 0;
  web_tasks.end = 0;

  mutex_init(&usb_tasks.mutex);
  usb_tasks.start = 0;
  usb_tasks.end = 0;
}
