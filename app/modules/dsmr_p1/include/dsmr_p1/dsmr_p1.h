/**
 * @file dsmr_p1.h
 * @author Theis <theismejnertsen@gmail.com>
 * @date 08-03-2025
 * 
 * @brief Interface functions for the dsmr p1 module
 * 
 */

#ifndef _DSMR_P1_INCLUDE_DSMR_P1_H__
#define _DSMR_P1_INCLUDE_DSMR_P1_H__

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define DSMR_P1_TELEGRAM_MAX_SIZE 1024

#define DSMR_P1_TRAILER_LEN 7U // ! CRC16 CR LF (1+4+1+1)

struct tarrif {
    long double tarrif_1; // kWh
    long double tarrif_2; // kWh
};

struct phase {
    float voltage;
    uint32_t nr_voltage_sags;
    uint32_t nr_voltage_swells;
    uint32_t current;
};

struct dsmr_p1_telegram {
    uint8_t version;
    struct tm timestamp;
    char *equipment_id;
    uint32_t device_type;
    struct tarrif elec_to_client;
    struct tarrif elec_by_client;
    uint32_t tarrif_indicator;
    float power_delivered;
    float power_received;
    uint32_t nr_power_failures;
    struct phase pl1;
    struct phase pl2;
    struct phase pl3;
};

typedef void (*dsmr_p1_telegram_received_callback_t)(struct dsmr_p1_telegram telegram, void *user_data);

int dsmr_p1_init(void);

int dsmr_p1_enable(void);

int dsmr_p1_disable(void);

int dsmr_p1_set_callback(dsmr_p1_telegram_received_callback_t cb, void *user_data);

#endif // _DSMR_P1_INCLUDE_DSMR_P1_H__
