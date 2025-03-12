#include <dsmr_p1/dsmr_p1.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>


LOG_MODULE_REGISTER(main, CONFIG_LOG_MAX_LEVEL);

const struct gpio_dt_spec led_gpio = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static void p1_telegram_received_cb(struct dsmr_p1_telegram telegram, void *user_data) {
    ARG_UNUSED(user_data);
    ARG_UNUSED(telegram);
    LOG_INF("p1 telegram received");
}

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

    dsmr_p1_set_callback(&p1_telegram_received_cb, NULL);
    dsmr_p1_enable();

    LOG_INF("application started");
    for (;;) {
        gpio_pin_toggle_dt(&led_gpio);
        k_sleep(K_MSEC(300));
    }

    return 0;
}