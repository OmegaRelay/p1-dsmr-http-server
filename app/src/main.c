#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>


LOG_MODULE_REGISTER(main, CONFIG_LOG_MAX_LEVEL);

#define UART_BUF_SIZE 1024

const struct device *p1_uart = DEVICE_DT_GET(DT_ALIAS(uart_p1));
const struct gpio_dt_spec data_req_gpio = GPIO_DT_SPEC_GET(DT_NODELABEL(data_req), gpios);

static uint8_t rx_buf[UART_BUF_SIZE] = {0};

int main(void) {
    int ret;
    LOG_INF("intialising application");

    ret = gpio_pin_configure_dt(&data_req_gpio, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("could not configure data request gpio: %d", ret);
        return ret;
    }

    LOG_INF("application started");

    ret = gpio_pin_set_dt(&data_req_gpio, 1);
    if (ret < 0) {
        LOG_ERR("could not set data request: %d", ret);
        return ret;
    }

    int offset = 0;
    for (;;) {
        if (offset >= sizeof(rx_buf)){
            LOG_HEXDUMP_WRN(rx_buf, offset, "buffer overflow");
            offset = 0;
        }
        ret = uart_poll_in(p1_uart, &rx_buf[offset]);        
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

    return 0;
}