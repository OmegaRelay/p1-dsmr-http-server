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

struct tarrif {
    float tarrif_1; // kWh
    float tarrif_2; // kWh
};

struct p1_telegram {
    uint8_t version;
    uint32_t timestamp;
    char *equipment_id;
    uint32_t device_type;
    struct tarrif elec_to_client;
    struct tarrif elec_by_client;
    uint32_t tarrif_indicator;
    float power_delivered;
    float power_received;
    uint32_t nr_power_failures;
};


int dsmr_p1_init(void);

int dsmr_p1_enable(void);

int dsmr_p1_disable(void);

#endif // _DSMR_P1_INCLUDE_DSMR_P1_H__