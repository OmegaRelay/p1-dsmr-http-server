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
#include <zephyr/sys/ring_buffer.h>

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

static void uart_irq_cb(const struct device *uart, void *user_data);
static void thread_entry(void *p1, void *p2, void *p3);
static int log_translate(platform_log_level_t log_level);


/******************************************************************************
 * Local Variables
 *****************************************************************************/

static struct k_thread dsmr_p1_rx_thread;
K_THREAD_STACK_DEFINE(dsmr_p1_rx_stack, DSMR_P1_TELEGRAM_MAX_SIZE * 2);

static data_received_callback_t telegram_received_cb;

static uint8_t rx_buf[DSMR_P1_TELEGRAM_MAX_SIZE];
static int rx_offset = 0;
RING_BUF_DECLARE(rx_ring_buf, DSMR_P1_TELEGRAM_MAX_SIZE *2);
K_SEM_DEFINE(data_ready_sem, 0, 1)


/******************************************************************************
 * Public Function Implementation
 *****************************************************************************/

int platform_init(data_received_callback_t cb) {
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
    }
	uart_irq_rx_disable(p1_uart_dev);
    ret = uart_irq_callback_user_data_set(p1_uart_dev, uart_irq_cb, NULL);
    if (ret < 0) {
        return ret;
    }

    telegram_received_cb = cb;
    k_thread_create(&dsmr_p1_rx_thread, dsmr_p1_rx_stack, K_THREAD_STACK_SIZEOF(dsmr_p1_rx_stack), &thread_entry, NULL, NULL, NULL, 1, 0, K_NO_WAIT);
    return 0;
}

int platform_write_data_req(bool high) {
    int ret = 0;

	high ? uart_irq_rx_enable(p1_uart_dev) : uart_irq_rx_disable(p1_uart_dev);
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
        return -EINVAL;
    }

    va_start(param_list, format);
    log_generic(level, format, param_list);
    va_end(param_list);
#else
    ARG_UNUSED(log_level);
    ARG_UNUSED(format);
#endif

    return 0;
}

SYS_INIT(dsmr_p1_init, POST_KERNEL, 90);


/******************************************************************************
 * Local Function Implementation
 *****************************************************************************/

static void uart_irq_cb(const struct device *uart_dev, void *user_data) {
 	if (!uart_irq_update(uart_dev)) {
		LOG_DBG("Unable to process interrupts");
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		LOG_DBG("No RX data");
		return;
	}
    
    int ret = uart_fifo_read(uart_dev, rx_buf + rx_offset, 1);
    if (ret < 0) {
        LOG_ERR("Failed to read UART FIFO (%d)", ret);
        rx_offset = 0;
        return;
    };
    if (rx_buf[0] != '/') {
        rx_offset = 0;
        return;
    }

    rx_offset += ret;
    if (rx_offset < DSMR_P1_TRAILER_LEN) {
        return; 
    }
    if (rx_offset >= sizeof(rx_buf)) {
        rx_offset = 0;
    }
    if (rx_buf[rx_offset - DSMR_P1_TRAILER_LEN] == '!') {
        ring_buf_put(&rx_ring_buf, rx_buf, rx_offset);
        k_sem_give(&data_ready_sem);
        rx_offset = 0;
    }
}

static void thread_entry(void *p1, void *p2, void *p3) {
    int ret;
    uint8_t buf[DSMR_P1_TELEGRAM_MAX_SIZE];
    LOG_INF("started");

    for (;;) {
        memset(buf, 0, sizeof(buf));
        ret = k_sem_take(&data_ready_sem, K_FOREVER);
        if (ret != 0) {
            continue;
        }
        ret = ring_buf_get(&rx_ring_buf, buf, sizeof(buf));
        if (ret < 0) {
            LOG_ERR("could not get data from ring buf (%d)", ret);
            continue;
        }
        LOG_HEXDUMP_INF(buf, ret, "telegram: ");
        telegram_received_cb(buf, ret);
    }
}

/* Convert dsmr_p1 log level to zephyr log level. */
static int log_translate(platform_log_level_t log_level) {
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
