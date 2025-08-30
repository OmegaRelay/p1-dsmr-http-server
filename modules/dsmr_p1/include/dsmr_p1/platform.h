
#ifndef _DSMR_P1_INCLUDE_DSMR_PLATFORM_H__
#define _DSMR_P1_INCLUDE_DSMR_PLATFORM_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum platform_log_level {
    PLATFORM_LOG_NONE,
    PLATFORM_LOG_DEBUG,
    PLATFORM_LOG_INFO,
    PLATFORM_LOG_WARNING,
    PLATFORM_LOG_ERROR,
    PLATFORM_LOG_FATAL,
} platform_log_level_t;

typedef void (*data_received_callback_t)(uint8_t *data, size_t len);

int platform_init(data_received_callback_t cb);

int platform_write_data_req(bool high);

int platform_log(platform_log_level_t log_level, const char *aFormat, ...);

#endif // _DSMR_P1_INCLUDE_DSMR_PLATFORM_H__