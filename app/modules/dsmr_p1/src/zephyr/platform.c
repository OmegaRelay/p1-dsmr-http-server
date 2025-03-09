/**
 * @file platform.c
 * @author Theis <theismejnertsen@gmail.com>
 * @date 09-03-2025
 * 
 * @brief Zephyr platform integration for the DSMR P1 library
 * 
 */

#include <dsmr_p1/dsmr_p1.h>
#include <dsmr_p1/platform.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
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
 * Local Function Prototypes
 *****************************************************************************/

static void thread_entry(void *p1, void *p2, void *p3);
static int log_translate(platform_log_level_t log_level);


/******************************************************************************
 * Local Variables
 *****************************************************************************/

static uint8_t rx_buf[CONFIG_DSMR_P1_BUF_SIZE] = {0};

static struct k_thread dsmr_p1_rx_thread;
K_THREAD_STACK_DEFINE(dsmr_p1_rx_stack, CONFIG_DSMR_P1_BUF_SIZE * 2);

static telegram_received_callback_t telegram_received_cb;


/******************************************************************************
 * Public Function Implementation
 *****************************************************************************/

int platform_init(telegram_received_callback_t cb) {
    int ret;
    LOG_INF("intialising");
    if (cb == NULL) {
        return -EINVAL;
    }

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

    telegram_received_cb = cb;
    k_thread_create(&dsmr_p1_rx_thread, &dsmr_p1_rx_stack, K_THREAD_STACK_SIZEOF(dsmr_p1_rx_stack), &thread_entry, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
    return 0;
}

int platform_write_data_req(bool high) {
    int ret = 0;
    if (gpio_is_ready_dt(&data_req_gpio)) {
        ret = gpio_pin_set_dt(&data_req_gpio, high ? 1 : 0);
        if (ret < 0) {
            LOG_ERR("could not set data request pin: %d", ret);
            return ret;
        }
    } else {
        ret = -ENOTSUP;
    }
    return ret;
}

int platform_log(platform_log_level_t log_level, const char *format, ...) {

#if defined(CONFIG_LOG)
	int level = log_translate(log_level);
	va_list param_list;

	if (level < 0) {
		return;
	}

	va_start(param_list, format);
	log_generic(level, format, param_list);
	va_end(param_list);
#else
	ARG_UNUSED(log_level);
	ARG_UNUSED(format);
#endif

}

SYS_INIT(dsmr_p1_init, POST_KERNEL, 90);


/******************************************************************************
 * Local Function Implementation
 *****************************************************************************/

static void thread_entry(void *p1, void *p2, void *p3) {
    int ret;
    bool is_crc = true;
    int crc_count;
    uint16_t crc = 0;
    LOG_INF("started");

    int offset = 0;
    for (;;) {
        if (offset >= sizeof(rx_buf)){
            LOG_HEXDUMP_WRN(rx_buf, offset, "buffer overflow");
            goto reset;
        }
        ret = uart_poll_in(p1_uart_dev, &rx_buf[offset]);        
        if (ret != 0) {
            continue;
        }

        if (!is_crc) {
            if (rx_buf[0] != '/') {
                goto reset;
            }
            if (rx_buf[offset] == '!') {
                is_crc = true;
                crc_count = 0;
                crc = 0;
            }
        } else {
            crc_count++;
            if (crc_count == 4) {
                telegram_received_cb(rx_buf, offset+1);
                goto reset;
            }
        }
        offset++;
        continue;
        
reset:
        offset = 0;
        is_crc = false;
        crc = 0;

    }
}

/* Convert dsmr_p1 log level to zephyr log level. */
static int log_translate(platform_log_level_t log_level)
{
	switch (log_level) {
	case PLATFORM_LOG_NONE:
	case PLATFORM_LOG_DEBUG:
		return LOG_LEVEL_DBG;
	case PLATFORM_LOG_INFO:
		return LOG_LEVEL_INF;
	case PLATFORM_LOG_WARNING:
		return LOG_LEVEL_WRN;
	case PLATFORM_LOG_ERROR:
		return LOG_LEVEL_ERR;
	default:
		break;
	}

	return -1;
}
