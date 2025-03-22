
/******************************************************************************
 * Includes
 *****************************************************************************/

#include "dsmr_p1/dsmr_p1.h"
#include "dsmr_p1/platform.h"
#include "obis.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/******************************************************************************
 * Constants
 *****************************************************************************/
/******************************************************************************
 * Local Function Interface
 *****************************************************************************/

static void telegram_received_cb(uint8_t *telegram, size_t len);
static uint16_t calc_p1_telegram_crc(const uint8_t *src, size_t len);
static struct dsmr_p1_telegram parse_p1_telegram(uint8_t *telegram, size_t telegram_len);
static void parse_cosem_object(struct dsmr_p1_telegram *telegram, char *cosem, size_t len);
static struct tm parse_cosem_timestamp(char *value);


/******************************************************************************
 * Local Variables
 *****************************************************************************/

static dsmr_p1_telegram_received_callback_t user_cb;
static void *user_data;


/******************************************************************************
 * Public Function Implementation
 *****************************************************************************/

int dsmr_p1_init(void) {
    return platform_init(&telegram_received_cb);
}

int dsmr_p1_enable(void) {
    return platform_write_data_req(true);
}

int dsmr_p1_disable(void) {
    return platform_write_data_req(false);
}

int dsmr_p1_set_callback(dsmr_p1_telegram_received_callback_t a_cb, void *a_user_data) {
    user_cb = a_cb;
    user_data = a_user_data;
    return 0;
}


/******************************************************************************
 * Local Function Implementation
 *****************************************************************************/

static void telegram_received_cb(uint8_t *data, size_t len) {
    if (data[len - DSMR_P1_TRAILER_LEN] != '!') {
        platform_log(PLATFORM_LOG_ERROR, "received bad telegram");
        return;
    }
    platform_log(PLATFORM_LOG_INFO, "telegram received");
    
    uint16_t rx_crc = 0;
    char rx_crc_str[5];
    strncpy(rx_crc_str, (const char *) &data[len - DSMR_P1_TRAILER_LEN + 1], sizeof(rx_crc_str));
    rx_crc_str[4] = 0;
    rx_crc = strtol(rx_crc_str, NULL, 16);

    uint16_t calc_crc = calc_p1_telegram_crc(data, len - DSMR_P1_TRAILER_LEN + 1); 
    if (calc_crc != rx_crc) {
        platform_log(PLATFORM_LOG_ERROR, "received bad crc");
        platform_log(PLATFORM_LOG_DEBUG, "calculated: 0x%04X, received 0x%04X", calc_crc, rx_crc);
        return;
    }
    platform_log(PLATFORM_LOG_DEBUG, "crc ok");

    struct dsmr_p1_telegram telegram = parse_p1_telegram(data, len);
    user_cb(telegram, user_data);
}

/**
 * @brief Calculates 16 bit CRC using polynomial 0x8005 without XOR in or XOR out
 * 
 * @param src
 * @param len 
 * @return uint16_t 
 */
static uint16_t calc_p1_telegram_crc(const uint8_t *src, size_t len) {
    uint16_t crc = 0;
    size_t i, j;

    for (i = 0; i < len; i++) {
        crc ^= *src++;
        for (j = 0; j < 8; j++) {
            if (crc & 0x1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}

static struct dsmr_p1_telegram parse_p1_telegram(uint8_t *telegram, size_t telegram_len) {
    struct dsmr_p1_telegram ret = {0};

    size_t len = 0;

    char *token, *next_token = telegram;
    while (next_token < (char *)(telegram + telegram_len)) {
        char *saveptr;
        token = strtok_r(next_token, DSMR_P1_COSEM_DELIM, &saveptr);
        if (token == NULL) {
            break;
        }
        platform_log(PLATFORM_LOG_INFO, "%s", token);
        size_t len = strlen(token);
        next_token = token + len + 1;
        parse_cosem_object(&ret, token, len);
    }

    return ret;
}

static void parse_cosem_object(struct dsmr_p1_telegram *telegram, char *cosem, size_t len) {
    char *saveptr;
    const char *obis_delim = "()";
    char *obis_code = strtok_r(cosem, obis_delim, &saveptr);
    if (obis_code == NULL || strnlen(obis_code, len) == len) {
        platform_log(PLATFORM_LOG_DEBUG, "not a cosem object");
        return;
    }
    platform_log(PLATFORM_LOG_DEBUG, "cosem object: %s", cosem);
    platform_log(PLATFORM_LOG_DEBUG, "parsing cosem object %p, %s", obis_code, obis_code);
    struct obis_code code;
    char *value = strtok_r(NULL, obis_delim, &saveptr);
    if (value == NULL) {
        return;
    }

    const char *value_delim = "*";
    if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_VERSION) == 0) {
        telegram->version = strtol(value, NULL, 16);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_DATE_TIME) == 0) {
        telegram->timestamp = parse_cosem_timestamp(value);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_TO_CLIENT_T1) == 0) {
        value = strtok(value, value_delim);
        telegram->elec_to_client.tarrif_1 = strtold(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_BY_CLIENT_T1) == 0) {
        value = strtok(value, value_delim);
        telegram->elec_by_client.tarrif_1 = strtold(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_TO_CLIENT_T2) == 0) {
        value = strtok(value, value_delim);
        telegram->elec_to_client.tarrif_2 = strtold(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_BY_CLIENT_T2) == 0) {
        value = strtok(value, value_delim);
        telegram->elec_by_client.tarrif_2 = strtold(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_ELEC_DELIVERED) == 0) {
        value = strtok(value, value_delim);
        telegram->power_delivered = strtold(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_ELEC_RECEIVED) == 0) {
        value = strtok(value, value_delim);
        telegram->power_received = strtold(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_TARRIF_INDICATOR) == 0) {
        value = strtok(value, value_delim);
        telegram->tarrif_indicator = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_FAILURE_NR) == 0) {
        telegram->nr_power_failures = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_FAILURE_NR_LONG) == 0) {
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL1) == 0) {
        value = strtok(value, value_delim);
        telegram->pl1.voltage = strtof(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL2) == 0) {
        value = strtok(value, value_delim);
        telegram->pl2.voltage = strtof(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL3) == 0) {
        value = strtok(value, value_delim);
        telegram->pl3.voltage = strtof(value, NULL);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL1_NR_SAGS) == 0) {
        telegram->pl1.nr_voltage_sags = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL2_NR_SAGS) == 0) {
        telegram->pl2.nr_voltage_sags = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL3_NR_SAGS) == 0) {
        telegram->pl3.nr_voltage_sags = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL1_NR_SWELLS) == 0) {
        telegram->pl1.nr_voltage_swells = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL2_NR_SWELLS) == 0) {
        telegram->pl2.nr_voltage_swells = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL3_NR_SWELLS) == 0) {
        telegram->pl3.nr_voltage_swells = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_CURRENT_PL1) == 0) {
        value = strtok(value, value_delim);
        telegram->pl1.current = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_CURRENT_PL2) == 0) {
        value = strtok(value, value_delim);
        telegram->pl2.current = strtol(value, NULL, 10);
    }
    else if (strcmp(cosem, DSMR_P1_OBIS_REF_STR_POWER_CURRENT_PL3) == 0) {
        value = strtok(value, value_delim);
        telegram->pl3.current = strtol(value, NULL, 10);
    }
}

static struct tm parse_cosem_timestamp(char *value) {
    struct tm ret = {0};
    char sub_value[3] = {0};
    int offset = 0;
    
    memcpy(sub_value, &value[offset], 2);
    ret.tm_year = strtol(sub_value, NULL, 10);
    offset += 2;

    memcpy(sub_value, &value[offset], 2);
    ret.tm_mon = strtol(sub_value, NULL, 10);
    offset += 2;

    memcpy(sub_value, &value[offset], 2);
    ret.tm_mday = strtol(sub_value, NULL, 10);
    offset += 2;

    memcpy(sub_value, &value[offset], 2);
    ret.tm_hour = strtol(sub_value, NULL, 10);
    offset += 2;

    memcpy(sub_value, &value[offset], 2);
    ret.tm_min = strtol(sub_value, NULL, 10);
    offset += 2;

    memcpy(sub_value, &value[offset], 2);
    ret.tm_sec = strtol(sub_value, NULL, 10);
    offset += 2;

    memcpy(sub_value, &value[offset], 1);
    ret.tm_isdst = sub_value[0] == 'S';

    return ret;
}
