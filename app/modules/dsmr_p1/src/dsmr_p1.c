
/******************************************************************************
 * Includes
 *****************************************************************************/

#include "dsmr_p1/dsmr_p1.h"
#include "dsmr_p1/platform.h"
#include "obis.h"

#include <stdlib.h>


/******************************************************************************
 * Constants
 *****************************************************************************/


/******************************************************************************
 * Local Function Interface
 *****************************************************************************/

static uint16_t calc_p1_telegram_crc(const uint8_t *src, size_t len);
static struct p1_telegram parse_p1_telegram(uint8_t *telegram, uint16_t telegram_len);


/******************************************************************************
 * Local Variables
 *****************************************************************************/

static void telegram_received_cb(uint8_t *telegram, size_t len);


/******************************************************************************
 * Public Function Implementation
 *****************************************************************************/

int dsmr_p1_init(void) {
    return platform_init(telegram_received_cb);
}

int dsmr_p1_enable(void) {
    return platform_write_data_req(true);
}

int dsmr_p1_disable(void) {
    return platform_write_data_req(false);
}


/******************************************************************************
 * Local Function Implementation
 *****************************************************************************/

static void telegram_received_cb(uint8_t *telegram, size_t len) {
    uint16_t rx_crc = 0;
    char rx_crc_str[4];
    strncpy(&rx_crc_str, &telegram[len - sizeof(rx_crc_str)], sizeof(rx_crc_str));
    rx_crc = atoi(rx_crc_str);
    len -= sizeof(rx_crc_str);

    uint16_t calc_crc = calc_p1_telegram_crc(&telegram, len); 
    if (calc_crc != rx_crc) {
        platform_log(PLATFORM_LOG_ERROR, "received bad crc");
    }

    parse_p1_telegram(telegram, len);
}

/**
 * @brief Calculates 16 bit CRC using polynomial 0x8005 without XOR in or XOR out
 * 
 * @param src
 * @param len 
 * @return uint16_t 
 */
static uint16_t calc_p1_telegram_crc(const uint8_t *src, size_t len) {
    uint16_t crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)src[i] << 8;
        for (unsigned k = 0; k < 8; k++)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x8005 : crc << 1;
    }
    return crc;
}

static struct p1_telegram parse_p1_telegram(uint8_t *telegram, uint16_t telegram_len) {

}