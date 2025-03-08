
/******************************************************************************
 * Includes
 *****************************************************************************/

#include "dsmr_p1.h"

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>


/******************************************************************************
 * Constants
 *****************************************************************************/

#define LOG_LEVEL CONFIG_DMSR_P1_LOG_LEVEL
#define LOG_MODULE_NAME dmsr_p1_serial
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

const struct device *p1_uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_p1_uart));
const struct gpio_dt_spec data_req_gpio = GPIO_DT_SPEC_GET_OR(DT_NODELABEL(zephy_p1_req_gpio), gpios, {0});


/******************************************************************************
 * Local Function Interface
 *****************************************************************************/

static int serial_init(void);
static void thread_entry(void *p1, void *p2, void *p3);

/******************************************************************************
 * Local Variables
 *****************************************************************************/

static uint8_t rx_buf[CONFIG_DSMR_P1_BUF_SIZE] = {0};

static struct k_thread dsmr_p1_rx_thread;
K_THREAD_STACK_DEFINE(dsmr_p1_rx_stack, CONFIG_DSMR_P1_BUF_SIZE * 2);


/******************************************************************************
 * Public Function Implementation
 *****************************************************************************/

int dsmr_p1_init(void) {
    int ret = 0;
    ret = serial_init();
    if (ret < 0) {
        return ret;
    }
    k_thread_create(&dsmr_p1_rx_thread, &dsmr_p1_rx_stack, K_THREAD_STACK_SIZEOF(dsmr_p1_rx_stack), &thread_entry, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
    return ret;
}

int dsmr_p1_enable(void) {
    int ret = 0;
    if (gpio_is_ready_dt(&data_req_gpio)) {
        ret = gpio_pin_set_dt(&data_req_gpio, 1);
        if (ret < 0) {
            LOG_ERR("could not set data request: %d", ret);
            return ret;
        }
    } else {
        ret = -ENOTSUP;
    }
    return ret;
}

int dsmr_p1_disable(void) {
    int ret = 0;
    if (gpio_is_ready_dt(&data_req_gpio)) {
        ret = gpio_pin_set_dt(&data_req_gpio, 0);
        if (ret < 0) {
            LOG_ERR("could not set data request: %d", ret);
            return ret;
        }
    } else {
        ret = -ENOTSUP;
    }
    return ret;
}

SYS_INIT(dsmr_p1_init, POST_KERNEL, 90);


/******************************************************************************
 * Local Function Implementation
 *****************************************************************************/

static int serial_init(void) {
    int ret;
    LOG_INF("intialising");

    if (gpio_is_ready_dt(&data_req_gpio)) {
        ret = gpio_pin_configure_dt(&data_req_gpio, GPIO_OUTPUT_INACTIVE);
        if (ret < 0) {
            LOG_ERR("could not configure data request gpio: %d", ret);
            return ret;
        }


        ret = gpio_pin_set_dt(&data_req_gpio, 1);
        if (ret < 0) {
            LOG_ERR("could not set data request: %d", ret);
            return ret;
        }
    }
}

static void thread_entry(void *p1, void *p2, void *p3) {
    int ret;
    LOG_INF("started");

    int offset = 0;
    for (;;) {
        if (offset >= sizeof(rx_buf)){
            LOG_HEXDUMP_WRN(rx_buf, offset, "buffer overflow");
            offset = 0;
        }
        ret = uart_poll_in(p1_uart_dev, &rx_buf[offset]);        
        if (ret != 0) {
            continue;
        }
        if (rx_buf[0] != '/') {
            offset = 0;
            continue;
        }
        if (rx_buf[offset] == '!') {
            uint16_t message_size = offset + 1;
            LOG_INF("message size: %d", message_size);
            LOG_HEXDUMP_INF(rx_buf, message_size, "message data:");
            offset = 0;
            continue;
        }
        offset++;
    }
}