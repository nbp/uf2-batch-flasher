#if defined(USE_TCP_SERVER)
#include "hardware/watchdog.h"  // watchdog_reboot
#include "pico/bootrom.h" // reset_usb_boot

// Handle TCP stack.
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

// Handle Wifi network setup.
#include "pico/cyw43_arch.h"

// Provide the pipe interface used to flash content to the USB device.
#include "pipe.h"

// Pipe interface used for stdio functions.
#include "stdio_web.h"

// Collect references to callback tasks.
#include "usb_host.h"

// Some debugging
#include "input.h"

#include "tcp_server.h"

// ---------------------------------------------------------
//  TCP connection status

#define TCP_PORT 5656

// Number of 
#define BUF_QUEUE_SIZE 16

typedef struct {
  void *conn; // tcp_server_t pointer.
  uint8_t buf[TCP_MSS];
  uint16_t len;
} buffer_t;

typedef struct {
  struct tcp_pcb *server_pcb;
  struct tcp_pcb *client_pcb;

  // Information to transmit to USB callbacks.
  void* usb_context;

  buffer_t recv_queue[BUF_QUEUE_SIZE];
  uint8_t live_buf;  // Count number of live pbuf.
  uint8_t last_buf;  // Where to insert pbuf.
  // Total number of bytes received to be flashed, between FLASH_START and FLASH_END.
  size_t total_flashed;
} tcp_server_t;

static tcp_server_t *tcp_server_init(void) {
  tcp_server_t *state = calloc(1, sizeof(tcp_server_t));
  if (!state) {
    printf("failed to allocate state\n");
    return NULL;
  }
  for (size_t i = 0; i < BUF_QUEUE_SIZE; i++) {
    state->recv_queue[i].conn = state;
  }
  return state;
}

static err_t tcp_server_close(tcp_server_t *state) {
  err_t err = ERR_OK;

  // Clear state attached to listen to client connections.
  if (state->client_pcb != NULL) {
    tcp_arg(state->client_pcb, NULL);
    tcp_poll(state->client_pcb, NULL, 0);
    tcp_sent(state->client_pcb, NULL);
    tcp_recv(state->client_pcb, NULL);
    tcp_err(state->client_pcb, NULL);
    err = tcp_close(state->client_pcb);
    if (err != ERR_OK) {
      printf("close failed %d, calling abort\n", err);
      tcp_abort(state->client_pcb);
      err = ERR_ABRT;
    }
    state->client_pcb = NULL;
  }

  // Clear state attached to server listenning port.
  if (state->server_pcb) {
    tcp_arg(state->server_pcb, NULL);
    tcp_close(state->server_pcb);
    state->server_pcb = NULL;
  }

  return err;
}

static err_t tcp_server_result(tcp_server_t *state, int status) {
  if (status == 0) {
    printf("test success\n");
  } else {
    printf("test failed %d\n", status);
  }
  return tcp_server_close(state);
}

// Send data to the connected client.
static err_t tcp_server_send_data(tcp_server_t *state, uint8_t *buf, uint16_t len) {
  // this method is callback from lwIP, so cyw43_arch_lwip_begin is not
  // required, however you can use this method to cause an assertion in debug
  // mode, if this method is called when cyw43_arch_lwip_begin IS needed
  cyw43_arch_lwip_check();

  err_t err = tcp_write(state->client_pcb, buf, len, TCP_WRITE_FLAG_COPY);
  if (err != ERR_OK) {
    printf("Failed to write data %d\n", err);
    return tcp_server_result(state, -1);
  }

  // Drain the content in a TCP packet.
  err = tcp_output(state->client_pcb);
  if (err != ERR_OK) {
    printf("Failed to write data %d\n", err);
    return tcp_server_result(state, -1);
  }
  return ERR_OK;
}

static void send_ack(tcp_server_t *state, uint8_t ack) {
  uint8_t buffer[1] = { ack };
  tcp_server_send_data(state, buffer, 1);
}

// ---------------------------------------------------------
// Callbacks exposed to the USB thread.

static void* last_usb_context = NULL;
void request_flash(void* arg)
{
  // state is not available...
  //send_ack(state, FLASH_READY);
  last_usb_context = arg;
}

void report_file_opened(void* arg)
{
  buffer_t *p = (buffer_t*) arg;
  tcp_server_t *state = (tcp_server_t*) p->conn;
  send_ack(state, FLASH_START);
}

void report_file_closed(void* arg)
{
  buffer_t *p = (buffer_t*) arg;
  tcp_server_t *state = (tcp_server_t*) p->conn;
  send_ack(state, FLASH_END);
}

void write_error(void* arg)
{
  buffer_t *p = (buffer_t*) arg;
  tcp_server_t *state = (tcp_server_t*) p->conn;
  send_ack(state, FLASH_ERROR);
}

uint8_t* get_postmsg_buffer(void* arg)
{
  buffer_t *p = (buffer_t*) arg;
  return p->buf;
}

size_t get_postmsg_length(void* arg)
{
  buffer_t *p = (buffer_t*) arg;
  return p->len;
}

void *get_postmsg_usb_info(void* arg)
{
  buffer_t *p = (buffer_t*) arg;
  tcp_server_t *state = (tcp_server_t*) p->conn;
  return state->usb_context;
}

void free_postmsg(void* arg)
{
  buffer_t *p = (buffer_t*) arg;
  p->len = 0;
  tcp_server_t *state = (tcp_server_t*) p->conn;
  state->live_buf--;
  send_ack(state, FLASH_PART_WRITTEN);
}

// ---------------------------------------------------------
// State machine which manages how are interpreted buffers
// which are received.

static void send_status(tcp_server_t *state) {
  const size_t len = USB_DEVICES + 1 + sizeof(uint16_t);
  uint8_t buffer[USB_DEVICES + 1 + sizeof(uint16_t)];
  buffer[0] = UPDATE_STATUS;
  buffer[1] = USB_DEVICES & 0xff;
  buffer[2] = (USB_DEVICES >> 8) & 0xff;
  uint8_t *status = &buffer[3];
  for (size_t device = 0; device < USB_DEVICES; device++) {
    status[device] = get_usb_device_status(device);
  }

  tcp_server_send_data(state, buffer, len);
}

static void send_stdout(tcp_server_t *state) {
  char buffer[512 + 1 + sizeof(uint16_t)];
  buffer[0] = UPDATE_STDOUT;
  uint16_t len = stdout_ssi(&buffer[3], sizeof(buffer) - 3);
  if (len == 1) {
    return;
  }
  buffer[1] = (char) (len & 0xff);
  buffer[2] = (char) ((len >> 8) & 0xff);
  tcp_server_send_data(state, (uint8_t*) buffer, len + 3);
}

static void send_decode_failure(tcp_server_t *state) {
  send_ack(state, DECODE_FAILURE);
}

static uint16_t recv_select_device(tcp_server_t *state, struct pbuf *buf, uint16_t offset) {
  if (buf->tot_len - offset < 2) {
    send_decode_failure(state);
    return 1;
  }

  int8_t device = (int8_t) pbuf_get_at(buf, offset + 1);
  
  if (device >= 0) {
    printf("Queue USB select_device: %d\n", device);
    queue_usb_task(&select_device_cb, (void*) (intptr_t) device);
  } else {
    printf("Queue reset all USB status (%d)\n", device);
    queue_usb_task(&clear_usb_status_cb, (void*) 0);
  }
  return 2;
}

static void recv_reboot_for_flash(tcp_server_t *state) {
  // Reboot in order to flash a new image.
  tcp_server_close(state);
  reset_usb_boot(0, 0);
}

static void recv_reboot_soft(tcp_server_t *state) {
  // Restart the program loaded in the flash.
  tcp_server_close(state);
  watchdog_reboot(0, 0, 0);
  watchdog_enable(0, true);
}

static void recv_start_flash(tcp_server_t *state) {
  buffer_t *p = &state->recv_queue[state->last_buf];
  state->total_flashed = 0;
  state->usb_context = last_usb_context;
  queue_usb_task(&open_file, p);
}

static uint16_t recv_write_flash_part(tcp_server_t *state, struct pbuf *buf, uint16_t offset) {
  buffer_t *p = &state->recv_queue[state->last_buf];
  uint16_t len = TCP_MSS;
  if (buf->tot_len < 3) {
    printf("recv_write_flash_part: incomplete header (%d < %d)",
           buf->tot_len - offset, 3);
    return 0;
  }
  uint8_t lo = pbuf_get_at(buf, offset + 1);
  uint8_t hi = pbuf_get_at(buf, offset + 2);
  uint16_t actual_len = (uint16_t) (lo + (hi << 8));
  if (actual_len < len) {
    len = actual_len;
  }
  // If the message is incomplete leave it for the next time.
  if (offset + 3 + len < buf->tot_len) {
    printf("recv_write_flash_part: incomplete message (%d < %d)",
           buf->tot_len - offset - 3, len);
    return 0;
  }
  uint16_t recv = pbuf_copy_partial(buf, (void*) p->buf, len, offset + 3);
  p->len = recv;
  state->live_buf--;
  state->last_buf += 1;
  state->last_buf %= BUF_QUEUE_SIZE;
  state->total_flashed += recv;
  send_ack(state, FLASH_PART_RECEIVED);
  queue_usb_task(&write_file_content, (void*) p);
  return recv + 3;
}

static void recv_end_flash(tcp_server_t *state) {
  buffer_t *p = &state->recv_queue[state->last_buf];
  printf("POST finished: received %u bytes.\n", state->total_flashed);
  queue_usb_task(&close_file, p);
}

// TCP is a stream protocol, this function will convert the stream into
// messages. It will return how many bytes are read for each message.
static uint16_t tcp_recv_message(tcp_server_t *state, struct pbuf *buf, uint16_t offset) {
  if (buf->tot_len < 1) {
    return 0;
  }
  uint8_t msg_id = pbuf_get_at(buf, offset);

  // The first byte is assumed to be the message type.
  switch (msg_id) {
  case REQUEST_STATUS:
    send_status(state);
    return 1;
  case REQUEST_STDOUT:
    send_stdout(state);
    return 1;
  case SELECT_DEVICE:
    return recv_select_device(state, buf, offset);
  case REBOOT_FOR_FLASH:
    recv_reboot_for_flash(state);
    return 1;
  case REBOOT_SOFT:
    recv_reboot_soft(state);
    return 1;
  case START_FLASH:
    recv_start_flash(state);
    return 1;
  case WRITE_FLASH_PART:
    return recv_write_flash_part(state, buf, offset);
  case END_FLASH:
    recv_end_flash(state);
    return 1;
  default:
    send_decode_failure(state);
    return 1;
  }
}

// ---------------------------------------------------------
//  All callback for managing the state of the connection
//  and data transfers.

static err_t tcp_server_sent_cb(void *arg, struct tcp_pcb *tpcb, u16_t len) {
  return ERR_OK;
}

static err_t tcp_server_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p,
                                err_t err) {
  tcp_server_t *state = (tcp_server_t*) arg;
  if (!p) {
    return tcp_server_result(state, -1);
  }
  // this method is callback from lwIP, so cyw43_arch_lwip_begin is not
  // required, however you can use this method to cause an assertion in debug
  // mode, if this method is called when cyw43_arch_lwip_begin IS needed
  cyw43_arch_lwip_check();

  uint16_t recv = 0;
  while (recv < p->tot_len) {
    // printf("tcp_server_recv %d err %d\n", p->tot_len, err);

    // free the buffer once everything is consumed.
    uint16_t processed = tcp_recv_message(state, p, recv);
    recv += processed;
    if (processed == 0) {
      break;
    }
  }

  tcp_recved(tpcb, recv);
  pbuf_free(p);
  return ERR_OK;
}

static err_t tcp_server_poll_cb(void *arg, struct tcp_pcb *tpcb) {
  tcp_server_t *state = (tcp_server_t*) arg;
  printf("tcp_server_poll_fn\n");
  return tcp_server_result(state, -1); // no response is an error?
}

static void tcp_server_err_cb(void *arg, err_t err) {
  tcp_server_t *state = (tcp_server_t*) arg;
  if (err != ERR_ABRT) {
    printf("tcp_client_err_fn %d\n", err);
    tcp_server_result(state, err);
  }
}

// ---------------------------------------------------------
//  Setup the Web Server

static err_t tcp_server_accept_cb(void *arg, struct tcp_pcb *client_pcb,
                                  err_t err) {
  tcp_server_t *state = (tcp_server_t*) arg;
  if (err != ERR_OK || client_pcb == NULL) {
    printf("Failure in accept\n");
    tcp_server_result(state, err);
    return ERR_VAL;
  }
  printf("Client connected\n");

  state->client_pcb = client_pcb;
  client_pcb->keep_intvl = 1000; // 1000ms
  const uint8_t poll_time_s = 1;
  // Setup the callback and the tcp_server_t* argument given to all callbacks.
  tcp_arg(client_pcb, state);
  tcp_sent(client_pcb, tcp_server_sent_cb);
  tcp_recv(client_pcb, tcp_server_recv_cb);
  tcp_poll(client_pcb, tcp_server_poll_cb, poll_time_s * 2);
  tcp_err(client_pcb, tcp_server_err_cb);
  return ERR_OK;
}

static bool tcp_server_open(tcp_server_t *state) {
  printf("Starting server at %s on port %u\n",
         ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!pcb) {
    printf("failed to create pcb\n");
    return false;
  }

  err_t err = tcp_bind(pcb, NULL, TCP_PORT);
  if (err) {
    printf("failed to bind to port %u\n", TCP_PORT);
    return false;
  }

  state->server_pcb = tcp_listen_with_backlog(pcb, 1);
  if (!state->server_pcb) {
    printf("failed to listen\n");
    if (pcb) {
      tcp_close(pcb);
    }
    return false;
  }

  tcp_arg(state->server_pcb, state);
  tcp_accept(state->server_pcb, tcp_server_accept_cb);
  return true;
}

bool tcp_server_setup() {
  // Initializes the cyw43_driver and the lwIP stack.
  if (cyw43_arch_init()) {
    printf("Failure to initialize Wifi chip.\n");
    return false;
  }

  // Enables Wi-Fi Station (STA) mode, such that connections can be made to
  // other Wi-Fi Access Points.
  cyw43_arch_enable_sta_mode();

  // Connect to a wireless Access Point, block for a given period of time and
  // fails if the timeout is expired.
  //
  // TODO: Find a way such that we wait for configuring the device Wifi using
  // the Web interface. In which case we should probably wait on some default
  // public Wifi setup...
  printf("Connecting to Wi-Fi...\n");
  if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                         CYW43_AUTH_WPA2_AES_PSK, 30000)) {
    printf("Failure to connect to the Wi-Fi network.\n");
    return false;
  }
  printf("Connected.\n");

  tcp_server_t *state = tcp_server_init();
  if (!state) {
    printf("Failure to allocate the TCP server.\n");
    return false;
  }
  if (!tcp_server_open(state)) {
    printf("Failure to initialized the TCP server.\n");
    tcp_server_result(state, -1);
    return false;
  }

  printf("TCP Server initialized.\n");
  return true;
}

void tcp_server_loop() {
  while (true) {
    cyw43_arch_poll();
    exec_web_task();
  }
}

void tcp_server_stop() {
  cyw43_arch_deinit();
}

#endif  // defined(USE_TCP_SERVER)
