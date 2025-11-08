/**
 * @file main.c
 * @author Theis <theismejnertsen@gmail.com>
 * @date 2025-11-02
 */

/******************************************************************************
 * Includes
 *****************************************************************************/

#include "server.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/toolchain.h>

#include <errno.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net/wifi_credentials.h>
#include <zephyr/net/wifi_mgmt.h>

#include <dsmr_p1/dsmr_p1.h>
#include <zephyr/logging/log.h>

/******************************************************************************
 * Constants
 *****************************************************************************/

#define NET_MGMT_EVENT_WIFI_SET                                                \
    (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |        \
     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)

enum main_event {
    MAIN_EVENT_DSMR_TELEGRAM_RECEIVED = BIT(0),
    MAIN_EVENT_WIFI_RECONNECT = BIT(1),
};

#define LED_ON_TIME K_MSEC(100)

static const struct gpio_dt_spec led_gpio =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/******************************************************************************
 * Private Function Prototypes
 *****************************************************************************/

static void led_disable_timeout_cb(struct k_timer *timer);

static void net_mgmt_event_static_handler_cb(uint64_t mgmt_event,
                                             struct net_if *iface, void *info,
                                             size_t info_length,
                                             void *user_data);
static void handle_wifi_connect_result(struct wifi_status *status);
static void wifi_reconnect_timeout_cb(struct k_timer *timer);
static void autoconnect_wifi(void);

static void telegram_received_cb(const uint8_t *data, size_t len,
                                 void *user_data);

static int resource_handle_data(const struct server_request *req,
                                struct server_response *res);
static void resource_handle_data_on_done(int err);
static int resource_handle_version(const struct server_request *req,
                                   struct server_response *res);

/******************************************************************************
 * Private Variables
 *****************************************************************************/

LOG_MODULE_REGISTER(main, CONFIG_LOG_MAX_LEVEL);

K_EVENT_DEFINE(main_event);

K_TIMER_DEFINE(led_disable_timer, led_disable_timeout_cb, NULL);
K_TIMER_DEFINE(wifi_reconnect_timer, wifi_reconnect_timeout_cb, NULL);

NET_MGMT_REGISTER_EVENT_HANDLER(wifi_net_mgmt_cb, NET_MGMT_EVENT_WIFI_SET,
                                net_mgmt_event_static_handler_cb, NULL);

static size_t last_telegram_len = 0;
static uint8_t last_telegram[DSMR_P1_TELEGRAM_MAX_SIZE] = {0};
uint8_t data_buf[DSMR_P1_TELEGRAM_MAX_SIZE] = {0};
K_MUTEX_DEFINE(telegram_mu);

/******************************************************************************
 * Public Functions
 *****************************************************************************/

int main(void) {
    int ret;
    if (!gpio_is_ready_dt(&led_gpio)) {
        LOG_ERR("led0 is not ready");
        return -EIO;
    }
    ret = gpio_pin_configure_dt(&led_gpio, GPIO_OUTPUT_ACTIVE);
    if (ret < 0) {
        LOG_ERR("could not configure led0: %d", ret);
        return ret;
    }

    autoconnect_wifi();
    server_add_resource("/data", &resource_handle_data);
    server_add_resource("/version", &resource_handle_version);
    server_start();

    ret = dsmr_p1_set_callback(telegram_received_cb, NULL);
    if (ret < 0) {
        LOG_ERR("failed to set dsmr p1 callback: %d", ret);
        return ret;
    }
    ret = dsmr_p1_enable();
    if (ret < 0) {
        LOG_WRN("failed to enable dsmr p1: %d", ret);
    }

    uint32_t events;
    while (true) {
        events = k_event_wait(&main_event, UINT32_MAX, true, K_FOREVER);
        if (events & MAIN_EVENT_DSMR_TELEGRAM_RECEIVED) {
            gpio_pin_set_dt(&led_gpio, 1);
            k_timer_start(&led_disable_timer, LED_ON_TIME, K_FOREVER);
        }
        if (events & MAIN_EVENT_WIFI_RECONNECT) {
            autoconnect_wifi();
        }
    }

    return 0;
}

/******************************************************************************
 * Private Functions
 *****************************************************************************/

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
    }
}

static void wifi_reconnect_timeout_cb(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_event_post(&main_event, MAIN_EVENT_WIFI_RECONNECT);
}

static void autoconnect_wifi(void) {
    int ret;
    if (!wifi_credentials_is_empty()) {
        struct net_if *iface = net_if_get_wifi_sta();
        ret = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
        if (ret) {
            LOG_ERR("could not auto-connect to network. %d", ret);
        }
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
    res->body = last_telegram;
    res->body_len = last_telegram_len;
    res->on_done = resource_handle_data_on_done;
    return 0;
}

static void resource_handle_data_on_done(int err) {
    k_mutex_unlock(&telegram_mu);
}

static int resource_handle_version(const struct server_request *req,
                                   struct server_response *res) {
    return -ENOSYS;
}
