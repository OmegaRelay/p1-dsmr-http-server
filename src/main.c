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

#include <dsmr_p1/dsmr_p1.h>
#include <zephyr/data/json.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
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

#define NET_MGMT_EVENT_WIFI_SET                                                \
    (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |        \
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
static void net_mgmt_event_static_handler_cb(uint64_t mgmt_event,
                                             struct net_if *iface, void *info,
                                             size_t info_length,
                                             void *user_data);
static void handle_wifi_connect_result(struct wifi_status *status);
static void handle_ipv4_result(struct net_if *iface);

static int data_handler(struct http_client_ctx *client,
                        enum http_data_status status,
                        const struct http_request_ctx *request_ctx,
                        struct http_response_ctx *response_ctx,
                        void *user_data);

static void p1_telegram_received_cb(const uint8_t *data, size_t len,
                                    void *user_data);

static void heartbeat_toggle_timeout_cb(struct k_timer *timer);
static void wifi_reconnect_timeout_cb(struct k_timer *timer);

/*******************************************************************************
 * Local Variables
 ******************************************************************************/

K_EVENT_DEFINE(event);
K_TIMER_DEFINE(heartbeat_toggle_timer, heartbeat_toggle_timeout_cb, NULL);
K_TIMER_DEFINE(wifi_reconnect_timer, wifi_reconnect_timeout_cb, NULL);

static uint8_t resp_buffer[8192] = {0};
struct http_resource_detail_dynamic server_data_resource_detail = {
    .common =
        {
            .type = HTTP_RESOURCE_TYPE_DYNAMIC,
            .bitmask_of_supported_http_methods = BIT(HTTP_GET),
            .content_encoding = "json",
            .content_type = "application/json",
        },
    .cb = &data_handler,
    .user_data = NULL,
};

static uint16_t http_service_port = 80;

HTTP_SERVICE_DEFINE(http_service, "", &http_service_port, 1, 10, NULL, NULL);

HTTP_RESOURCE_DEFINE(p1_data_resource, http_service, "/data",
                     &server_data_resource_detail);

static const struct json_obj_descr json_tariff_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct tarrif, tarrif_1, JSON_TOK_DOUBLE_FP),
    JSON_OBJ_DESCR_PRIM(struct tarrif, tarrif_2, JSON_TOK_DOUBLE_FP),
};

static const struct json_obj_descr json_phase_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct phase, voltage, JSON_TOK_FLOAT_FP),
    JSON_OBJ_DESCR_PRIM(struct phase, nr_voltage_sags, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct phase, nr_voltage_swells, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct phase, current, JSON_TOK_NUMBER),
};

static const struct json_obj_descr json_telegram_descr[] = {
    JSON_OBJ_DESCR_PRIM(struct dsmr_p1_telegram, version, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_PRIM(struct dsmr_p1_telegram, timestamp, JSON_TOK_NUMBER),
    // FIXME: string buffer causes crash during encoding
    // JSON_OBJ_DESCR_PRIM(struct dsmr_p1_telegram, equipment_id,
    // JSON_TOK_STRING),
    JSON_OBJ_DESCR_PRIM(struct dsmr_p1_telegram, device_type, JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_OBJECT(struct dsmr_p1_telegram, elec_to_client,
                          json_tariff_descr),
    JSON_OBJ_DESCR_OBJECT(struct dsmr_p1_telegram, elec_by_client,
                          json_tariff_descr),
    JSON_OBJ_DESCR_PRIM(struct dsmr_p1_telegram, tarrif_indicator,
                        JSON_TOK_NUMBER),
    JSON_OBJ_DESCR_OBJECT(struct dsmr_p1_telegram, pl1, json_phase_descr),
    JSON_OBJ_DESCR_OBJECT(struct dsmr_p1_telegram, pl2, json_phase_descr),
    JSON_OBJ_DESCR_OBJECT(struct dsmr_p1_telegram, pl3, json_phase_descr),
};

NET_MGMT_REGISTER_EVENT_HANDLER(wifi_net_mgmt_cb, NET_MGMT_EVENT_WIFI_SET,
                                net_mgmt_event_static_handler_cb, NULL);
NET_MGMT_REGISTER_EVENT_HANDLER(ip_net_mgmt_cb, NET_MGMT_EVENT_IP_SET,
                                net_mgmt_event_static_handler_cb, NULL);
NET_MGMT_REGISTER_EVENT_HANDLER(if_net_mgmt_cb, NET_MGMT_EVENT_IF_SET,
                                net_mgmt_event_static_handler_cb, NULL);

K_SEM_DEFINE(scan_done_sem, 0, 1);

static bool has_ip_address = false;
static uint8_t last_telegram[DSMR_P1_TELEGRAM_MAX_SIZE] = {0};
static size_t telegram_len = 0;

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
    http_server_start();

    dsmr_p1_set_callback(&p1_telegram_received_cb, NULL);
    dsmr_p1_enable();

    k_event_post(&event, EVENT_FLAG_HEARTBEAT | EVENT_FLAG_CONNECT_WIFI);

    LOG_INF("application started");
    uint32_t events = 0;
    for (;;) {
        events = k_event_wait(&event, EVENT_FLAGS, false, K_FOREVER);
        if ((events & EVENT_FLAG_HEARTBEAT) == EVENT_FLAG_HEARTBEAT) {
            gpio_pin_toggle_dt(&led_gpio);
            k_timer_start(&heartbeat_toggle_timer, HEARTBEAT_TOGGLE_PERIOD,
                          K_FOREVER);
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

static void net_mgmt_event_static_handler_cb(uint64_t mgmt_event,
                                             struct net_if *iface, void *info,
                                             size_t info_length,
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

    case NET_EVENT_WIFI_SCAN_RESULT:
        const struct wifi_scan_result *scan_result = info;
        if (scan_result->rssi > -80) {
            LOG_INF("scanned: %d, %s, %d, %d", scan_result->rssi,
                    scan_result->ssid, scan_result->band, scan_result->channel);
        }
        break;
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
                    AF_INET,
                    &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
                    buf, sizeof(buf)));
        LOG_INF("Subnet: %s",
                net_addr_ntop(AF_INET,
                              &iface->config.ip.ipv4->unicast[i].netmask, buf,
                              sizeof(buf)));
        LOG_INF("Router: %s", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw,
                                            buf, sizeof(buf)));
        LOG_INF("IP: %s",
                net_addr_ntop(
                    AF_INET,
                    &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
                    buf, sizeof(buf)));
    }

    has_ip_address = true;
}

static int data_handler(struct http_client_ctx *client,
                        enum http_data_status status,
                        const struct http_request_ctx *request_ctx,
                        struct http_response_ctx *response_ctx,
                        void *user_data) {
    int err;
    size_t resp_len = 0;
    response_ctx->final_chunk = (status == HTTP_SERVER_DATA_FINAL);
    response_ctx->body = NULL;
    response_ctx->body_len = 0;

    LOG_INF("http request %s received", client->content_type);
    if (status == HTTP_SERVER_DATA_ABORTED) {
        LOG_DBG("Transaction aborted.");
        return 0;
    }

    if (strncmp(client->content_type, "application/json", HTTP_SERVER_MAX_CONTENT_TYPE_LEN) == 0) {
        if (telegram_len == 0) {
            response_ctx->status = HTTP_200_OK;
            strcpy(resp_buffer, "{}");
        } else {
            // avoid data changing over course of encoding
            uint8_t data[DSMR_P1_TELEGRAM_MAX_SIZE] = {0};
            memcpy(data, last_telegram, sizeof(data));
            struct dsmr_p1_telegram telegram = dsmr_p1_parse_telegram(data, telegram_len);
            err = json_obj_encode_buf(json_telegram_descr,
                                    ARRAY_SIZE(json_telegram_descr), &telegram,
                                    resp_buffer, sizeof(resp_buffer));
            if (err < 0) {
                LOG_ERR("could not encode json: %d", err);
                response_ctx->status = HTTP_500_INTERNAL_SERVER_ERROR;
                return -ENOMEM;
            }
            resp_len = strlen(resp_buffer);
        }
    } else if (strncmp(client->content_type, "", HTTP_SERVER_MAX_CONTENT_TYPE_LEN) == 0 || strncmp(client->content_type, "text/plain", HTTP_SERVER_MAX_CONTENT_TYPE_LEN) == 0) {
        memcpy(resp_buffer, last_telegram, telegram_len);
        resp_len = telegram_len;
    } else {
        LOG_ERR("unsupported media type: %s", client->content_type);
        response_ctx->status = HTTP_415_UNSUPPORTED_MEDIA_TYPE;
        return 0;
    }

    response_ctx->status = HTTP_200_OK;
    response_ctx->body = resp_buffer;
    response_ctx->body_len = resp_len;
    return 0;
}

static void p1_telegram_received_cb(const uint8_t *data, size_t len,
                                    void *user_data) {
    ARG_UNUSED(user_data);
    LOG_INF("p1 telegram received");
    memcpy(last_telegram, data, MIN(sizeof(last_telegram), len));
    telegram_len = len;
}

static void heartbeat_toggle_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&event, EVENT_FLAG_HEARTBEAT);
}

static void wifi_reconnect_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&event, EVENT_FLAG_CONNECT_WIFI);
}