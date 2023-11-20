#include "hardware/watchdog.h"  // watchdog_reboot
#include "pico/bootrom.h" // reset_usb_boot

// Handle TCP and HTTP stacks.
#include "lwip/apps/httpd.h"

// Handle Wifi network setup.
#include "pico/cyw43_arch.h"

// Provide the pipe interface used to flash content to the USB device.
#include "pipe.h"

// Pipe interface used for stdio functions.
#include "stdio_web.h"

// Collect references to callback tasks.
#include "usb_host.h"

#include "web_server.h"

// ---------------------------------------------------------
//  Dynamically Processed Content (CGI / GET request)

// Handle GET query, by giving the ?aaa=bb parameters as an array of params and
// values strings.
const char *select_cgi(int index, int num_params, char *params[], char *values[]) {
  for (int p = 0; p < num_params; p++) {
    const char *param = params[p];
    const char *value = values[p];
    if (strcmp(param, "active_device") == 0) {
      // Select a given USB port.
      uintptr_t idx = (uintptr_t) atoi(value);
      queue_usb_task(&select_device, (void*) idx);
    }
  }
  return "/status.json";
}

const char *reboot_cgi(int index, int num_params, char *params[], char *values[]) {
  bool bootsel_reboot = false;
  for (int p = 0; p < num_params; p++) {
    const char *param = params[p];
    const char *value = values[p];
    if (strcmp(param, "bootsel") == 0) {
      uint32_t idx = (uint32_t) atoi(value);
      bootsel_reboot = idx;
    }
  }
  if (bootsel_reboot) {
    // Reboot in order to flash a new image.
    web_server_stop();
    reset_usb_boot(0, 0);
  } else {
    // Restart the program loaded in the flash.
    web_server_stop();
    watchdog_reboot(0, 0, 0);
    watchdog_enable(0, true);
  }
  // Not reachable !!!
  return "/status.json";
}

// List of CGI handlers, used to map a resource name to a handler to process the
// request.
static const tCGI cgi_handlers[] = {
  { "/select", select_cgi },
  { "/reboot", reboot_cgi }
};

void cgi_init() {
  http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
}

// ---------------------------------------------------------
//  Dynamically Generated Content (SSI)

const char *ssi_tags[] = {
#define SSI_TAG__STS 0
  "sts", // ss0
#define SSI_TAG__OUT 1
  "out"  // 1
};

// The SSI handler intercept and substitute tags in files which are sent as a
// response. If a tag such as `/*# sts */` appears in one of the file, the
// following function would be called to replace the content by being called
// with the index being set to SSI_TAG__STS.
uint16_t ssi_handler(int index, char *insert_at, int ins_len) {
  size_t insert_len = (size_t) ins_len;
  int out_len = 0;
  int inc_len = 0;
  switch (index) {
  // Used in status.json
  case SSI_TAG__STS: {
    // Generate an array of integer where each index corresponds to one USB
    // device which can be selected by the USB host, and each value corresponds
    // of the last status code recorded while attempting to flash the device.
    inc_len = snprintf(insert_at, insert_len, "[");
    out_len += out_len;
    insert_at += out_len;
    insert_len -= (size_t) inc_len;
    size_t device = 0;
    for (; device < USB_DEVICES - 1; device++) {
      inc_len += snprintf(insert_at, insert_len, "%d,",
                          get_usb_device_status(device));
      out_len += out_len;
      insert_at += out_len;
      insert_len -= (size_t) inc_len;
    }
    inc_len += snprintf(insert_at, insert_len, "%d]",
                        get_usb_device_status(device));
    out_len += out_len;
    insert_at += out_len;
    insert_len -= (size_t) inc_len;
    break;
  }
  case SSI_TAG__OUT: {
    out_len = stdout_ssi(insert_at, ins_len);
    break;
  }
  }
  LWIP_ASSERT("Sane length", (int) (uint16_t) out_len == out_len);
  return (uint16_t) out_len;
}

void ssi_init() {
  for (size_t t = 0; t < LWIP_ARRAYSIZE(ssi_tags); t++) {
    LWIP_ASSERT("Tags are restricted to LWIP_HTTPD_MAX_TAG_NAME_LEN",
                strlen(ssi_tags[t]) <= LWIP_HTTPD_MAX_TAG_NAME_LEN);
  }
  http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
}

// ---------------------------------------------------------
//  Uploading Large Content (POST)

static void* current_connection = NULL;
static void* current_usb_context = NULL;
static bool pending_usb_error_report = false;
static bool pending_usb_request_flash = false;

void request_flash(void* arg)
{
  pending_usb_request_flash = true;
  current_usb_context = arg;
}

// Enabling LWIP_HTTPD_SUPPORT_POST in lwipopts.h implies that we have to define
// a few handlers which are expected by LwIP, namely httpd_post_begin,
// httpd_post_receive_data and httpd_post_finished.
err_t httpd_post_begin(void* connection, const char* uri, const char* http_request,
                       uint16_t http_request_len, int content_len, char* err_response_uri,
                       uint16_t err_response_uri_len, uint8_t* post_auto_wnd)
{
  LWIP_UNUSED_ARG(http_request);
  LWIP_UNUSED_ARG(http_request_len);
  LWIP_UNUSED_ARG(content_len);

  if (current_connection != NULL) {
    // One POST connection is still in progress, reject this new one until the
    // previous is finished.
    strncpy(err_response_uri, "/status.json", err_response_uri_len);
    return ERR_ABRT;
  }
  current_connection = connection;

#if LWIP_HTTPD_POST_MANUAL_WND
  // The network is faster than the flash, and the RAM of the usb host is
  // not enough to hold in memory what would be flashed. Thus we want to
  // throttle the speed manually.
  *post_auto_wnd = 0;
#else
  *post_auto_wnd = 1;
#endif

  // Match that the URI.
  if (strcmp(uri, "/flash") != 0) {
    strncpy(err_response_uri, "/status.json", err_response_uri_len);
    return ERR_ABRT;
  }

  // TODO: transfer meta data, such as content_len or the file names.
  pending_usb_error_report = false;
  queue_usb_task(&open_file, current_usb_context);
  return ERR_OK;
}

void write_error(void* arg)
{
  pending_usb_error_report = true;
}

err_t httpd_post_receive_data(void* connection, struct pbuf* p)
{
  if (connection != current_connection) {
    return ERR_VAL;
  }
  if (pending_usb_error_report) {
    return ERR_ABRT;
  }

  pipe_enqueue(p->payload, p->len);
  queue_usb_task(&write_file_content, current_usb_context);

#if LWIP_HTTPD_POST_MANUAL_WND
  // Update the TCP window to throttle data reception.
  httpd_post_data_recved(connection, p->len);
#endif
  pbuf_free(p);
  return ERR_OK;
}

void httpd_post_finished(void* connection, char* response_uri, uint16_t response_uri_len)
{
  if (connection != current_connection) {
    return;
  }

  queue_usb_task(&close_file, current_usb_context);

  const char* return_to = "/status.json";
  strncpy(response_uri, return_to, response_uri_len);
  current_connection = NULL;
  current_usb_context = NULL;
}

// ---------------------------------------------------------
//  Setup the Web Server

bool web_server_setup() {
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
  if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
    printf("Failure to connect to the Wi-Fi network.\n");
    return false;
  }
  printf("Connected.\n");

  httpd_init();
  ssi_init();
  cgi_init();
  printf("HTTPD initialized.\n");
  return true;
}

void web_server_loop() {
  while (true) {
    cyw43_arch_poll();
    exec_web_task();
  }
}

void web_server_stop() {
  cyw43_arch_deinit();
}