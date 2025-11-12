/**
 * @file main.c
 * @author Theis <theismejnertsen@gmail.com>
 * @date 2025-11-02
 */

/******************************************************************************
 * Includes
 *****************************************************************************/

#include "server.h"

#include <dsmr_p1/dsmr_p1.h>

#include <sys/errno.h>
#include <zephyr/app_version.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/net/http/status.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/hash_map.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

/******************************************************************************
 * Constants
 *****************************************************************************/

#define NET_MGMT_EVENT_WIFI_SET                                                \
    (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |        \
     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)

enum main_event {
    MAIN_EVENT_WDT_FEED = BIT(0),
    MAIN_EVENT_DSMR_TELEGRAM_RECEIVED = BIT(1),
    MAIN_EVENT_WIFI_RECONNECT = BIT(2),
    MAIN_EVENT_WIFI_CONFIG_UPDATED = BIT(3),
    MAIN_EVENT_WIFI_AP_ENABLE = BIT(4),
    MAIN_EVENT_WIFI_AP_DISABLE = BIT(5),
};

#define WDT_MAX_WINDOW_MS 60000U
#define WDT_MIN_WINDOW_MS 0U
#define WDT_FEED_TIMEOUT K_MSEC(20000)
#define WDT_OPT WDT_OPT_PAUSE_HALTED_BY_DBG

#define LED_ON_TIME K_MSEC(100)
#define WIFI_AP_DISABLE_TIMEOUT K_MINUTES(2)

static const struct device *wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
static const struct gpio_dt_spec led_gpio =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};

static const uint8_t main_js_gz[] = {
#include "main.js.gz.inc"
};

static const uint8_t favicon_ico_gz[] = {
#include "favicon.ico.gz.inc"
};

struct wifi_config {
    char ssid[WIFI_SSID_MAX_LEN];
    char psk[WIFI_PSK_MAX_LEN];
};

struct config {
    struct wifi_config wifi;
};

static const struct json_obj_descr wifi_config_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct wifi_config, ssid, JSON_TOK_STRING_BUF),
    JSON_OBJ_DESCR_PRIM(struct wifi_config, psk, JSON_TOK_STRING_BUF),
};

static const struct json_obj_descr config_descr[] = {
    JSON_OBJ_DESCR_OBJECT(struct config, wifi, wifi_config_descr),
};

/******************************************************************************
 * Private Function Prototypes
 *****************************************************************************/

static int init_wdt(void);
static void wdt_feed_timeout_cb(struct k_timer *timer);
static void led_disable_timeout_cb(struct k_timer *timer);

static void net_mgmt_event_static_handler_cb(uint64_t mgmt_event,
                                             struct net_if *iface, void *info,
                                             size_t info_length,
                                             void *user_data);
static void handle_wifi_connect_result(struct wifi_status *status);
static void wifi_ap_disable_timeout_cb(struct k_timer *timer);
static void wifi_reconnect_timeout_cb(struct k_timer *timer);
static void autoconnect_wifi(void);
static void update_config_from_wifi_cred(void *user_data, const char *ssid,
                                         size_t ssid_len);
static void update_wifi_cred_from_config(void);
static int enable_ap_mode(void);
static int disable_ap_mode(void);
static void enable_dhcpv4_server(struct net_if *iface);
static void disable_dhcpv4_server(struct net_if *iface);

static void apply_config(struct config new, int64_t new_fields_bitmap);

static void telegram_received_cb(const uint8_t *data, size_t len,
                                 void *user_data);

static int resource_handle_index(const struct server_request *req,
                                 struct server_response *res);
static int resource_handle_main_js(const struct server_request *req,
                                   struct server_response *res);
static int resource_handle_favicon(const struct server_request *req,
                                   struct server_response *res);
static int resource_handle_data(const struct server_request *req,
                                struct server_response *res);
static void resource_handle_data_on_done(int err, void *user_data);
static int resource_handle_version(const struct server_request *req,
                                   struct server_response *res);
static int resource_handle_config(const struct server_request *req,
                                  struct server_response *res);
static void resource_handle_config_on_done(int err, void *user_data);

/******************************************************************************
 * Private Variables
 *****************************************************************************/

LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

static K_EVENT_DEFINE(main_event);

static K_TIMER_DEFINE(wdt_feed_timer, wdt_feed_timeout_cb, NULL);
static K_TIMER_DEFINE(led_disable_timer, led_disable_timeout_cb, NULL);
static K_TIMER_DEFINE(wifi_ap_disable_timer, wifi_ap_disable_timeout_cb, NULL);
static K_TIMER_DEFINE(wifi_reconnect_timer, wifi_reconnect_timeout_cb, NULL);

NET_MGMT_REGISTER_EVENT_HANDLER(wifi_net_mgmt_cb, NET_MGMT_EVENT_WIFI_SET,
                                net_mgmt_event_static_handler_cb, NULL);
static int wdt_channel_id = 0;

static size_t last_telegram_len = 0;
static uint8_t last_telegram[DSMR_P1_TELEGRAM_MAX_SIZE] = {0};
static K_MUTEX_DEFINE(telegram_mu);

static struct config config = {};

static struct net_if *sta_iface = NULL;
static struct net_if *ap_iface = NULL;

/******************************************************************************
 * Public Functions
 *****************************************************************************/

int main(void) {
    int ret;

    ret = init_wdt();
    if (ret < 0) {
        LOG_ERR("failed to init wdt");
        return ret;
    }

    sta_iface = net_if_get_wifi_sta();
    if (!sta_iface) {
        LOG_INF("STA iface: not initialized");
        return -EIO;
    }

    ap_iface = net_if_get_wifi_sap();
    if (!ap_iface) {
        LOG_INF("AP iface: not initialized");
        return -EIO;
    }

    if (!gpio_is_ready_dt(&led_gpio)) {
        LOG_ERR("led0 is not ready");
        return -EIO;
    }
    ret = gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("could not configure led0: %d", ret);
        return ret;
    }

    if (!wifi_credentials_is_empty()) {
        wifi_credentials_for_each_ssid(&update_config_from_wifi_cred, NULL);
    }

    autoconnect_wifi();
    server_add_resource("/", &resource_handle_index);
    server_add_resource("/main.js", &resource_handle_main_js);
    server_add_resource("/favicon.ico", &resource_handle_favicon);
    server_add_resource("/data", &resource_handle_data);
    server_add_resource("/version", &resource_handle_version);
    server_add_resource("/config", &resource_handle_config);
    server_start();

    ret = enable_ap_mode();
    if (ret < 0) {
        LOG_ERR("failed to enable AP: %d", ret);
        return ret;
    }

    ret = dsmr_p1_set_callback(telegram_received_cb, NULL);
    if (ret < 0) {
        LOG_ERR("failed to set dsmr p1 callback: %d", ret);
        return ret;
    }
    ret = dsmr_p1_enable();
    if (ret < 0) {
        LOG_WRN("failed to enable dsmr p1: %d", ret);
    }

    k_timer_start(&wdt_feed_timer, WDT_FEED_TIMEOUT, K_FOREVER);

    uint32_t events;
    while (true) {
        events = k_event_wait(&main_event, UINT32_MAX, true, K_FOREVER);
        LOG_DBG("events: 0x%04x", events);

        if (events & MAIN_EVENT_WDT_FEED) {
            wdt_feed(wdt, wdt_channel_id);
            k_timer_start(&wdt_feed_timer, WDT_FEED_TIMEOUT, K_FOREVER);
        }
        if (events & MAIN_EVENT_DSMR_TELEGRAM_RECEIVED) {
            gpio_pin_set_dt(&led_gpio, 1);
            k_timer_start(&led_disable_timer, LED_ON_TIME, K_FOREVER);
        }
        if (events & MAIN_EVENT_WIFI_RECONNECT) {
            autoconnect_wifi();
        }
        if (events & MAIN_EVENT_WIFI_CONFIG_UPDATED) {
            update_wifi_cred_from_config();
        }
        if (events & MAIN_EVENT_WIFI_AP_ENABLE) {
            enable_ap_mode();
        }
        if (events & MAIN_EVENT_WIFI_AP_DISABLE) {
            disable_ap_mode();
        }
    }

    return 0;
}

/******************************************************************************
 * Private Functions
 *****************************************************************************/

static int init_wdt(void) {
    int ret;
    if (!device_is_ready(wdt)) {
        LOG_ERR("%s: device not ready.\n", wdt->name);
        return -ENODEV;
    }

    struct wdt_timeout_cfg wdt_config = {
        /* Reset SoC when watchdog timer expires. */
        .flags = WDT_FLAG_RESET_SOC,

        /* Expire watchdog after max window */
        .window.min = WDT_MIN_WINDOW_MS,
        .window.max = WDT_MAX_WINDOW_MS,
    };

    ret = wdt_install_timeout(wdt, &wdt_config);
    if (ret < 0) {
        LOG_ERR("Watchdog install error: %d", ret);
        return ret;
    }
    wdt_channel_id = ret;

    ret = wdt_setup(wdt, WDT_OPT);
    if (ret < 0) {
        LOG_ERR("Watchdog setup error: %d", ret);
        return ret;
    }
}

static void wdt_feed_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&main_event, MAIN_EVENT_WDT_FEED);
}

static void led_disable_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    gpio_pin_set_dt(&led_gpio, 0);
}

static void net_mgmt_event_static_handler_cb(uint64_t mgmt_event,
                                             struct net_if *iface, void *info,
                                             size_t info_length,
                                             void *user_data) {
    ARG_UNUSED(iface);
    ARG_UNUSED(info_length);
    ARG_UNUSED(user_data);
    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        handle_wifi_connect_result((struct wifi_status *)info);
        break;

    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        LOG_WRN("wifi disconnected");
        k_timer_start(&wifi_reconnect_timer, K_MSEC(500), K_FOREVER);
        k_event_post(&main_event, MAIN_EVENT_WIFI_AP_ENABLE);
        break;

    default:
        break;
    }
}

static void handle_wifi_connect_result(struct wifi_status *status) {
    if (status->status) {
        LOG_WRN("connection request failed: %d", status->status);
    } else {
        LOG_INF("wifi connected");
        k_timer_start(&wifi_ap_disable_timer, WIFI_AP_DISABLE_TIMEOUT,
                      K_FOREVER);
    }
}

static void wifi_ap_disable_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&main_event, MAIN_EVENT_WIFI_AP_DISABLE);
}

static void wifi_reconnect_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&main_event, MAIN_EVENT_WIFI_RECONNECT);
}

static void autoconnect_wifi(void) {
    int ret;
    if (!wifi_credentials_is_empty()) {
        LOG_INF("auto connect wifi");
        ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, sta_iface, NULL, 0);
        if (ret) {
            LOG_ERR("could not auto-connect to network. %d", ret);
        }
    }
}

static void update_config_from_wifi_cred(void *user_data, const char *ssid,
                                         size_t ssid_len) {
    struct wifi_credentials_personal creds = {};

    memcpy(config.wifi.ssid, ssid, MIN(sizeof(config.wifi.ssid), ssid_len));
    int ret =
        wifi_credentials_get_by_ssid_personal_struct(ssid, ssid_len, &creds);
    if (ret < 0) {
        LOG_ERR("failed to get wifi creds of ssid: %s (%d):", ssid, ret);
        return;
    }
    memcpy(config.wifi.psk, creds.password,
           MIN(sizeof(config.wifi.psk), creds.password_len));
}

static void update_wifi_cred_from_config(void) {
    LOG_INF("wifi credentials updated");
    struct wifi_credentials_personal creds = {};
    memcpy(creds.header.ssid, config.wifi.ssid, sizeof(config.wifi.ssid));
    creds.header.ssid_len = strlen(config.wifi.ssid);

    creds.header.type = WIFI_SECURITY_TYPE_PSK;
    memcpy(creds.password, config.wifi.psk, sizeof(config.wifi.psk));
    creds.password_len = strlen(config.wifi.psk);

    (void)wifi_credentials_delete_all();
    int ret = wifi_credentials_set_personal_struct(&creds);
    if (ret < 0) {
        LOG_ERR("failed to update wifi creds");
    }

    ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, sta_iface, NULL, 0);
    if (ret) {
        LOG_WRN("could not disconnect from network. %d", ret);
    }
}

static int enable_ap_mode(void) {
    LOG_INF("Turning on AP Mode");
    static struct wifi_connect_req_params ap_config = {};
    ap_config.ssid = (const uint8_t *)CONFIG_WIFI_AP_SSID;
    ap_config.ssid_length = strlen(CONFIG_WIFI_AP_SSID);
    ap_config.psk = (const uint8_t *)CONFIG_WIFI_AP_PSK;
    ap_config.psk_length = strlen(CONFIG_WIFI_AP_PSK);
    ap_config.channel = WIFI_CHANNEL_ANY;
    ap_config.band = WIFI_FREQ_BAND_2_4_GHZ;

    if (strlen(CONFIG_WIFI_AP_PSK) == 0) {
        ap_config.security = WIFI_SECURITY_TYPE_NONE;
    } else {

        ap_config.security = WIFI_SECURITY_TYPE_PSK;
    }

    enable_dhcpv4_server(ap_iface);

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, ap_iface, &ap_config,
                       sizeof(struct wifi_connect_req_params));
    if (ret) {
        LOG_ERR("NET_REQUEST_WIFI_AP_ENABLE failed, err: %d", ret);
    }

    return ret;
}

static int disable_ap_mode(void) {
    disable_dhcpv4_server(ap_iface);

    int ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, ap_iface, NULL, 0);
    if (ret) {
        LOG_ERR("NET_REQUEST_WIFI_AP_DISABLE failed, err: %d", ret);
    }

    return ret;
}

static void enable_dhcpv4_server(struct net_if *iface) {
    static struct in_addr addr;
    static struct in_addr netmaskAddr;

    __ASSERT(iface != NULL, "null iface");

    if (net_addr_pton(AF_INET, CONFIG_WIFI_AP_IP_ADDRESS, &addr)) {
        LOG_ERR("Invalid address: %s", CONFIG_WIFI_AP_IP_ADDRESS);
        return;
    }

    if (net_addr_pton(AF_INET, CONFIG_WIFI_AP_NETMASK, &netmaskAddr)) {
        LOG_ERR("Invalid netmask: %s", CONFIG_WIFI_AP_NETMASK);
        return;
    }

    net_if_ipv4_set_gw(iface, &addr);

    if (net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0) == NULL) {
        LOG_ERR("could not set IP address for AP interface");
    }

    if (!net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmaskAddr)) {
        LOG_ERR("could not set netmask for AP interface: %s",
                CONFIG_WIFI_AP_NETMASK);
    }

    addr.s4_addr[3] += 10; /* Starting IPv4 address for DHCPv4 address pool. */

    if (net_dhcpv4_server_start(iface, &addr) != 0) {
        LOG_ERR("DHCP server is not started for desired IP");
        return;
    }

    LOG_INF("DHCPv4 server started...");
}

static void disable_dhcpv4_server(struct net_if *iface) {
    __ASSERT(iface != NULL, "null iface");
    if (net_dhcpv4_server_stop(iface) != 0) {
        LOG_ERR("failed to stop DHCP server");
    }
    LOG_INF("DHCPv4 server stopped...");
}

static void apply_config(struct config new, int64_t new_fields_bitmap) {
    LOG_INF("config update with 0x%llx", new_fields_bitmap);
    if (new_fields_bitmap && BIT(0)) {
        memcpy(config.wifi.ssid, new.wifi.ssid, sizeof(config.wifi.ssid));
        memcpy(config.wifi.psk, new.wifi.psk, sizeof(config.wifi.psk));
        k_event_post(&main_event, MAIN_EVENT_WIFI_CONFIG_UPDATED);
    }
}

static void telegram_received_cb(const uint8_t *data, size_t len,
                                 void *user_data) {
    k_event_post(&main_event, MAIN_EVENT_DSMR_TELEGRAM_RECEIVED);

    int ret = k_mutex_lock(&telegram_mu, K_NO_WAIT);
    if (ret < 0) {
        LOG_ERR("could not lock telegram mutex: %d", ret);
        return;
    }
    last_telegram_len = MIN(len, sizeof(last_telegram));
    memcpy(last_telegram, data, last_telegram_len);
    k_mutex_unlock(&telegram_mu);
}

static int resource_handle_index(const struct server_request *req,
                                 struct server_response *res) {
    if (req->method != HTTP_GET) {
        return -ENOSYS;
    }

    res->status = HTTP_200_OK;
    sys_hashmap_insert(&res->headers, (uint64_t)"Content-Type",
                       (uint64_t)"text/html", NULL);
    sys_hashmap_insert(&res->headers, (uint64_t)"Content-Encoding",
                       (uint64_t)"gzip", NULL);
    res->body = index_html_gz;
    res->body_len = sizeof(index_html_gz);
    return 0;
}

static int resource_handle_main_js(const struct server_request *req,
                                   struct server_response *res) {
    if (req->method != HTTP_GET) {
        return -ENOSYS;
    }

    res->status = HTTP_200_OK;
    sys_hashmap_insert(&res->headers, (uint64_t)"Content-Type",
                       (uint64_t)"text/javascript", NULL);
    sys_hashmap_insert(&res->headers, (uint64_t)"Content-Encoding",
                       (uint64_t)"gzip", NULL);
    res->body = main_js_gz;
    res->body_len = sizeof(index_html_gz);
    return 0;
}

static int resource_handle_favicon(const struct server_request *req,
                                   struct server_response *res) {
    if (req->method != HTTP_GET) {
        return -ENOSYS;
    }

    res->status = HTTP_200_OK;
    sys_hashmap_insert(&res->headers, (uint64_t)"Content-Type",
                       (uint64_t)"image/svg+xml", NULL);
    sys_hashmap_insert(&res->headers, (uint64_t)"Content-Encoding",
                       (uint64_t)"gzip", NULL);
    res->body = favicon_ico_gz;
    res->body_len = sizeof(favicon_ico_gz);
    return 0;
}

static int resource_handle_data(const struct server_request *req,
                                struct server_response *res) {
    if (req->method != HTTP_GET) {
        res->status = HTTP_405_METHOD_NOT_ALLOWED;
        return 0;
    }

    int ret = k_mutex_lock(&telegram_mu, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    res->status = HTTP_200_OK;
    sys_hashmap_insert(&res->headers, (uint64_t)"Content-Type",
                       (uint64_t)"text/plain", NULL);
    res->body = last_telegram;
    res->body_len = last_telegram_len;
    res->on_done = resource_handle_data_on_done;
    return 0;
}

static void resource_handle_data_on_done(int err, void *user_data) {
    ARG_UNUSED(err);
    ARG_UNUSED(user_data);
    k_mutex_unlock(&telegram_mu);
}

static int resource_handle_version(const struct server_request *req,
                                   struct server_response *res) {
    if (req->method != HTTP_GET) {
        res->status = HTTP_405_METHOD_NOT_ALLOWED;
        return 0;
    }

    res->status = HTTP_200_OK;
    res->body = APP_VERSION_STRING;
    res->body_len = sizeof(APP_VERSION_STRING) - 1;
    return 0;
}

static int resource_handle_config(const struct server_request *req,
                                  struct server_response *res) {
    int ret;
    size_t payload_len = 1024;
    uint8_t *payload = malloc(payload_len);
    if (!payload) {
        LOG_ERR("failed to allocate response");
        return -ENOMEM;
    }
    memset(payload, 0, payload_len);

    res->status = HTTP_200_OK;
    res->body = payload;
    res->body_len = 0;
    res->on_done = &resource_handle_config_on_done;
    res->user_data = payload;

    struct config new_config;
    switch (req->method) {
    case HTTP_GET:
        sys_hashmap_insert(&res->headers, (uint64_t)"Content-Type",
                           (uint64_t)"application/json", NULL);
        ret = json_obj_encode_buf(config_descr, ARRAY_SIZE(config_descr),
                                  &config, payload, payload_len - 1);
        if (ret < 0) {
            LOG_ERR("failed to encode payload: %d", ret);
            return ret;
        }
        res->body_len = strlen(payload);
        break;
    case HTTP_POST:
        ret = json_obj_parse((char *)req->body, req->body_len, config_descr,
                             ARRAY_SIZE(config_descr), &new_config);
        if (ret < 0) {
            res->status = HTTP_400_BAD_REQUEST;
            LOG_ERR("failed to decode payload: %d", ret);
            break;
        }
        LOG_INF("new config SSID: %s PSK: %s", new_config.wifi.ssid,
                new_config.wifi.psk);
        apply_config(new_config, ret);
        break;
    default:
        res->status = HTTP_405_METHOD_NOT_ALLOWED;
        break;
    }

    return 0;
}

static void resource_handle_config_on_done(int err, void *user_data) {
    ARG_UNUSED(err);
    free(user_data);
}
