// This file defines the set of options used to configure LwIP library to make
// it light weight as well as working with the RP2040 MCU.

#ifndef LWIP_OPTS_H
#define LWIP_OPTS_H

// Running on the RP2040 without an operating system.
#define NO_SYS 1

//#if DEBUG
#define LWIP_DEBUG                  1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#define IP_DEBUG                    LWIP_DBG_ON
#define TCP_DEBUG                   LWIP_DBG_ON
#define HTTPD_DEBUG                 LWIP_DBG_ON
#define HTTPD_DEBUG_TIMING          LWIP_DBG_ON
#define LWIP_DBG_TYPES_ON           LWIP_DBG_ON
// (LWIP_DBG_TRACE|LWIP_DBG_STATE|LWIP_DBG_FRESH|LWIP_DBG_HALT)
#define LWIP_DBG_MIN_LEVEL          LWIP_DBG_LEVEL_ALL
//#endif

// httpd_opts. https://www.nongnu.org/lwip/2_1_x/httpd__opts_8h.html

// TODO: Review the following settings, and make your own based on
// https://www.nongnu.org/lwip/2_1_x/group__lwip__opts.html
//
// Imported from krzmaz/pico-w-webserver-example
#ifndef LWIP_SOCKET
#define LWIP_SOCKET                 0
#endif
#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC             1
#else
// MEM_LIBC_MALLOC is incompatible with non polling versions
#define MEM_LIBC_MALLOC             0
#endif
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    4000
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETCONN                0
#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
// #define ETH_PAD_SIZE                2
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

// ------ Enable HTTP server.
#define LWIP_HTTPD 1

// ------ Producing dynamically generated content.
#define LWIP_HTTPD_SSI 1

// When replacing Tags such as /* # tag */, do not include the tag in the
// generated output. One of the reason being that json does not allow comments.
#define LWIP_HTTPD_SSI_INCLUDE_TAG   0

// Allow tags to be replaced by content which can be as big as ...
#define LWIP_HTTPD_MAX_TAG_INSERT_LEN 8192

// ------ Process query dynamically.
#define LWIP_HTTPD_CGI 1

// In order to send binary data, we rely on the POST method instead of GET to
// send the bytes.
#define LWIP_HTTPD_SUPPORT_POST 1
// Maximum length of the filename to send as a response to a POST request.
// #define LWIP_HTTPD_POST_MAX_RESPONSE_URI_LEN 63

// NOTE: uncomment the following lines if post_auto_wnd the automatic window of
// TCP is not large enough and packets seems to drop. (TCP ZeroWindow flags in
// packet)
//
//#define MBEDTLS_SSL_MAX_CONTENT_LEN             2048
//#define TCP_MSS                         800
//#define TCP_WND                         (2048 + 2048 / 4)

// ------ Reply with statically listed files.
// use generated fsdata
#define HTTPD_FSDATA_FILE "_webroot.c"

// Generate HTML headers as part of makefsdata instead of generating them on
// demand, in order to support proper content type for json files.
#define LWIP_HTTPD_DYNAMIC_HEADERS   0


#endif // LWIP_OPTS_H
