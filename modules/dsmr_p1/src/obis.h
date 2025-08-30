/**
 * @file obis.h
 * @author Theis <theismejnertsen@gmail.com>
 * @date 09-03-2025
 * 
 * @brief DSMR P1 standard OBIS codes
 * 
 */

#ifndef _DSMR_P1_SRC_OBIS_H__
#define _DSMR_P1_SRC_OBIS_H__

struct obis_code {
    int medium;
    int channel;
    int physical;
    int quantity;
    int type;
};

#define DSMR_P1_COSEM_DELIM "\r\n"

#define DSMR_P1_OBIS_MEDIUM_ABSTRACT 0
#define DSMR_P1_OBIS_MEDIUM_ELEC 1
#define DSMR_P1_OBIS_MEDIUM_HEAT 6
#define DSMR_P1_OBIS_MEDIUM_GAS 7
#define DSMR_P1_OBIS_MEDIUM_WATER 8

#define DSMR_P1_OBIS_VOLTAGE_PL1 32
#define DSMR_P1_OBIS_VOLTAGE_PL2 52
#define DSMR_P1_OBIS_VOLTAGE_PL3 72

#define DSMR_P1_OBIS_CURRENT_PL1 31
#define DSMR_P1_OBIS_CURRENT_PL2 51
#define DSMR_P1_OBIS_CURRENT_PL3 71
               
#define DSMR_P1_OBIS_REF_STR_VERSION "1-3:0.2.8"
const struct obis_code obis_version = {
    .medium = DSMR_P1_OBIS_MEDIUM_ELEC,
    .channel = 3,
    .physical = 0,
    .quantity = 2,
    .type = 8,
};

#define DSMR_P1_OBIS_REF_STR_DATE_TIME "0-0:1.0.0"
const struct obis_code obis_date_time = {
    .medium = DSMR_P1_OBIS_MEDIUM_ABSTRACT,
    .channel = 0,
    .physical = 1,
    .quantity = 0,
    .type = 0,
};
#define DSMR_P1_OBIS_REF_STR_EQUIPMENT_ID   "0-0:96.1.1"
const struct obis_code obis_equipment_id = {
    .medium = DSMR_P1_OBIS_MEDIUM_ABSTRACT,
    .channel = 0,
    .physical = 96,
    .quantity = 1,
    .type = 1,
};

#define DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_TO_CLIENT_T1   "1-0:1.8.1"
#define DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_TO_CLIENT_T2   "1-0:1.8.2"
#define DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_BY_CLIENT_T1   "1-0:2.8.1"
#define DSMR_P1_OBIS_REF_STR_POWER_DELIVERED_BY_CLIENT_T2   "1-0:2.8.2"
#define DSMR_P1_OBIS_REF_STR_POWER_ELEC_DELIVERED           "1-0:1.7.0"
#define DSMR_P1_OBIS_REF_STR_POWER_ELEC_RECEIVED            "1-0:2.7.0"

#define DSMR_P1_OBIS_REF_STR_POWER_TARRIF_INDICATOR         "0-0:96.14.0"
#define DSMR_P1_OBIS_REF_STR_POWER_FAILURE_NR               "0-0:96.7.21"
#define DSMR_P1_OBIS_REF_STR_POWER_FAILURE_NR_LONG          "0-0:96.7.9"
#define DSMR_P1_OBIS_REF_STR_POWER_FAILURE_EVENT_LOG        "1-0:99.97.0"

#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL1           "1-0:32.7.0"
#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL1_NR_SAGS   "1-0:32.32.0"
#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL1_NR_SWELLS "1-0:32.36.0"

#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL2           "1-0:52.7.0"
#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL2_NR_SAGS   "1-0:52.32.0"
#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL2_NR_SWELLS "1-0:52.36.0"

#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL3           "1-0:72.7.0"
#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL3_NR_SAGS   "1-0:72.32.0"
#define DSMR_P1_OBIS_REF_STR_POWER_VOLTAGE_PL3_NR_SWELLS "1-0:72.36.0"

#define DSMR_P1_OBIS_REF_STR_POWER_CURRENT_PL1           "1-0:31.7.0"
#define DSMR_P1_OBIS_REF_STR_POWER_CURRENT_PL2           "1-0:51.7.0"
#define DSMR_P1_OBIS_REF_STR_POWER_CURRENT_PL3           "1-0:71.7.0"

#endif // _DSMR_P1_SRC_OBIS_H__
