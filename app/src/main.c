/**
 * @file main.c
 * @author Theis <theismejnertsen@gmail.com>
 * @date 22-03-2025
 * 
 * @brief 
 * 
 */


/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <zephyr/net/http/parser.h>
#include <zephyr/net/http/parser_url.h>
#include <dsmr_p1/dsmr_p1.h>
#include <sys/errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/data/json.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

#include <zephyr/net/net_ip.h>

#include <zephyr/kernel.h>


/*******************************************************************************
 * Constants
 ******************************************************************************/

LOG_MODULE_REGISTER(main, CONFIG_LOG_MAX_LEVEL);

#define NET_MGMT_EVENT_WIFI_SET (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT | \
        NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)
#define NET_MGMT_EVENT_IP_SET (NET_EVENT_IPV4_ADDR_ADD)
#define NET_MGMT_EVENT_IF_SET (NET_EVENT_IF_UP | NET_EVENT_IF_DOWN)

#define HEARTBEAT_TOGGLE_PERIOD K_MSEC(500)

#define EVENT_FLAG_HEARTBEAT BIT(0)
#define EVENT_FLAG_CONNECT_WIFI BIT(1)
#define EVENT_FLAGS EVENT_FLAG_HEARTBEAT | EVENT_FLAG_CONNECT_WIFI

const struct gpio_dt_spec led_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

// TODO: move to dynamic config
const char wifi_ssid[] = "";
const char wifi_psk[] = "";


/*******************************************************************************
 * Local Function Prototypes
 ******************************************************************************/

static int init_wifi(void);
static int connect_wifi(void);
static void net_mgmt_event_static_handler_cb(uint32_t mgmt_event,
        struct net_if *iface,
        void *info, size_t info_length,
        void *user_data);
static void handle_wifi_connect_result(struct wifi_status *status);
static void handle_ipv4_result(struct net_if *iface);

static int start_http_server(void);
static int http_rx_entry(void *p1, void *p2, void *p3);

static void p1_telegram_received_cb(struct dsmr_p1_telegram telegram, void *user_data);

static void heartbeat_toggle_timeout_cb(struct k_timer *timer);
static void wifi_reconnect_timeout_cb(struct k_timer *timer);


/*******************************************************************************
 * Local Variables
 ******************************************************************************/

K_EVENT_DEFINE(event);
K_TIMER_DEFINE(heartbeat_toggle_timer, heartbeat_toggle_timeout_cb, NULL);
K_TIMER_DEFINE(wifi_reconnect_timer, wifi_reconnect_timeout_cb, NULL);

static size_t resp_len = 0;
static uint8_t resp_buffer[8192] = {0};
static uint16_t http_server_port = 80;
K_THREAD_DEFINE(http_rx, 8192, http_rx_entry, NULL, NULL, NULL, -5, 0, -1);

static int http_sock = 0;

NET_MGMT_REGISTER_EVENT_HANDLER(wifi_net_mgmt_cb, NET_MGMT_EVENT_WIFI_SET, net_mgmt_event_static_handler_cb, NULL);
NET_MGMT_REGISTER_EVENT_HANDLER(ip_net_mgmt_cb, NET_MGMT_EVENT_IP_SET, net_mgmt_event_static_handler_cb, NULL);
NET_MGMT_REGISTER_EVENT_HANDLER(if_net_mgmt_cb, NET_MGMT_EVENT_IF_SET, net_mgmt_event_static_handler_cb, NULL);

K_SEM_DEFINE(scan_done_sem, 0, 1);

static bool has_ip_address = false;
static struct dsmr_p1_telegram last_telegram;
static bool has_data = false;


/*******************************************************************************
 * Public Function Implementation
 ******************************************************************************/

int main(void) {
    int ret;
    LOG_INF("intialising application");

    if (!gpio_is_ready_dt(&led_gpio)) {
        LOG_ERR("led0 is not ready");
        return -EIO;
    }
    ret = gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("could not configure data request gpio: %d", ret);
        return ret;
    }

    ret = init_wifi();
    if (ret < 0) {
        return ret;
    }
    start_http_server();

    dsmr_p1_set_callback(&p1_telegram_received_cb, NULL);
    dsmr_p1_enable();

    k_event_post(&event, EVENT_FLAG_HEARTBEAT | EVENT_FLAG_CONNECT_WIFI);

    LOG_INF("application started");
    uint32_t events = 0;
    for (;;) {
        events = k_event_wait(&event, EVENT_FLAGS, false, K_FOREVER);
        if ((events & EVENT_FLAG_HEARTBEAT) == EVENT_FLAG_HEARTBEAT) {
            gpio_pin_toggle_dt(&led_gpio);
            k_timer_start(&heartbeat_toggle_timer, HEARTBEAT_TOGGLE_PERIOD, K_FOREVER);
        }
        if (events & EVENT_FLAG_CONNECT_WIFI) {
            ret = connect_wifi();
            if (ret < 0) {
                LOG_WRN("could not connect wifi: %d", ret);
                k_timer_start(&wifi_reconnect_timer, K_MSEC(100), K_FOREVER);
            }
        }
        k_event_clear(&event, events);
    }

    return 0;
}


/*******************************************************************************
 * Local Function Implementation
 ******************************************************************************/

static int init_wifi(void) {
    struct net_if *iface = net_if_get_first_wifi();
    if (iface == NULL) {
        LOG_ERR("could not get default interface");
        return -ENETUNREACH;
    }

    LOG_INF("starting network interface %p", iface);
    uint8_t counter = 0;
    net_if_up(iface);
    while (!net_if_is_up(iface) && counter < 10) {
        net_if_up(iface);
        k_sleep(K_MSEC(100));
        counter++;
    }

    if (counter == 100) {
        LOG_ERR("could not start wifi interface");
        return -ETIMEDOUT;
    }

    return 0;
}

static int connect_wifi(void) {
    int ret = 0;
    
    struct net_if *iface = net_if_get_first_wifi();
    if (iface == NULL) {
        LOG_ERR("could not get default interface");
        return -ENETUNREACH;
    }

    struct wifi_connect_req_params wifi_params = {0};
    wifi_params.ssid = wifi_ssid;
    wifi_params.psk = wifi_psk;
    wifi_params.ssid_length = strlen(wifi_ssid);
    wifi_params.psk_length = strlen(wifi_psk);
    wifi_params.channel = WIFI_CHANNEL_ANY;
    wifi_params.security = WIFI_SECURITY_TYPE_PSK;
    wifi_params.band = WIFI_FREQ_BAND_2_4_GHZ;
    wifi_params.mfp = WIFI_MFP_OPTIONAL;
    wifi_params.timeout = 10;

    LOG_INF("Connecting to SSID: %s", wifi_params.ssid);

    ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wifi_params,
            sizeof(struct wifi_connect_req_params));
    if (ret) {
        LOG_ERR("WiFi Connection Request Failed: %d", ret);
        return ret;
    }
    return 0;
}

static void net_mgmt_event_static_handler_cb(uint32_t mgmt_event,
        struct net_if *iface,
        void *info, size_t info_length,
        void *user_data) {
    switch (mgmt_event) {
        case NET_EVENT_IF_UP:
            LOG_INF("if up");
            break;

        case NET_EVENT_IF_DOWN:
            LOG_INF("if down");
            break;

        case NET_EVENT_WIFI_CONNECT_RESULT:
            LOG_INF("wifi connect result");
            handle_wifi_connect_result((struct wifi_status *)info);
            break;

        case NET_EVENT_WIFI_DISCONNECT_RESULT:
            // Start AP mode and the provisioning http server
            LOG_WRN("wifi disconnected");
            k_event_post(&event, EVENT_FLAG_CONNECT_WIFI);
            break;

        case NET_EVENT_IPV4_ADDR_ADD:
            handle_ipv4_result(iface);
            break;

        case NET_EVENT_WIFI_SCAN_RESULT: {
                                             const struct wifi_scan_result *scan_result = info;
                                             if (scan_result->rssi > -80) {
                                                 LOG_INF("scanned: %d, %s, %d, %d", scan_result->rssi, scan_result->ssid,
                                                         scan_result->band, scan_result->channel);
                                             }
                                             break;
                                         }
        case NET_EVENT_WIFI_SCAN_DONE:
                                         LOG_INF("scan done");
                                         k_sem_give(&scan_done_sem);
                                         break;

        default:
                                         break;
    }
}

static void handle_wifi_connect_result(struct wifi_status *status) {
    if (status->status) {
        LOG_WRN("Connection request failed (%d)", status->status);
    } else {
        LOG_INF("Connected");
    }
}

static void handle_ipv4_result(struct net_if *iface) {
    int i = 0;

    for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {

        char buf[NET_IPV4_ADDR_LEN];

        if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
            continue;
        }

        LOG_INF("IPv4 address: %s",
                net_addr_ntop(
                    AF_INET, &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
                    buf, sizeof(buf)));
        LOG_INF("Subnet: %s",
                net_addr_ntop(AF_INET, &iface->config.ip.ipv4->unicast[i].netmask, buf,
                    sizeof(buf)));
        LOG_INF("Router: %s",
                net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, buf,
                    sizeof(buf)));
        LOG_INF("IP: %s", net_addr_ntop(AF_INET, 
                    &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr, 
                    buf, sizeof(buf)));
    }

    has_ip_address = true;
}

static int start_http_server(void) {
    int ret;
    struct sockaddr_in6 addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(http_server_port);

    http_sock = zsock_socket(addr.sin6_family, SOCK_DGRAM, IPPROTO_UDP);
    if (http_sock < 0) {
        LOG_ERR("could not get coap server socket: %s", strerror(errno));
        return -errno;
    }

    ret = zsock_bind(http_sock, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0) {
        LOG_ERR("could not bind coap server socket: %s", strerror(errno));
        return -errno;
    }

    k_thread_start(http_rx);
    return 0;
}

static int http_rx_entry(void *p1, void *p2, void *p3) {
    int ret = 0;
    int recv_len;
    struct sockaddr addr;
    socklen_t addr_len;
    uint8_t recv_data[1024];

    do {
        addr_len = sizeof(addr);
        recv_len = zsock_recvfrom(http_sock, recv_data, sizeof(recv_data), 0,
                &addr, &addr_len);
        if (recv_len < 0) {
            LOG_ERR("connection error %d", errno);
            return -errno;
        }
        LOG_HEXDUMP_INF(recv_data, recv_len, "data received: ");

      struct http_parser parser = {0};
      struct http_parser_settings settings = {0};
      http_parser_init(&parser, HTTP_REQUEST);
      http_parser_settings_init(&settings);
      http_parser_execute(&parser, &settings, recv_data, recv_len);

      // handle http
    } while (true);

    return ret;
}

static void p1_telegram_received_cb(struct dsmr_p1_telegram telegram, void *user_data) {
    ARG_UNUSED(user_data);
    LOG_INF("p1 telegram received");
    last_telegram = telegram;
    has_data = true;
}

static void heartbeat_toggle_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&event, EVENT_FLAG_HEARTBEAT);
}

static void wifi_reconnect_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&event, EVENT_FLAG_CONNECT_WIFI);
}
