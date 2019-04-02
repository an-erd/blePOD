#ifndef __BLEPOD_VALUES_H__
#define __BLEPOD_VALUES_H__

#include "esp_event.h"

// #define VALUES_BUFFERLEN         5
// #define VALUES_TIMELEN		    32

typedef enum {
    ID_BLEADV,             // BLE adv packet
} element_id_t;

// union used to put received data packets into a queue for further work
typedef union {
    struct ble_adv_element {
		// struct tm	timeinfo;
        // uint8_t     proximity_uuid[4];
        uint16_t    major;
        uint16_t    minor;
        int8_t      measured_power;
        uint16_t    temp;
        uint16_t    humidity;
        uint16_t    x;
        uint16_t    y;
        uint16_t    z;
        uint16_t    battery;
    } ble_adv;
} values_element_t;

typedef struct {
    element_id_t         id;
    values_element_t    data;
} queue_element_t;

// void blepod_runvalues_initialize(runningValuesStruct_t *values);
// void blepod_runvalues_calculate_display_values(runningValuesStruct_t *values);
// bool blepod_runvalues_get_update_display_available(runningValuesStruct_t *values);
// void dispod_runvalues_update_RSCValues(runningValuesStruct_t *values,
//     uint8_t new_cad);
// void dispod_runvalues_update_customValues(runningValuesStruct_t *values,
//     uint16_t new_GCT, uint8_t new_str);

#endif // __BLEPOD_VALUES_H__