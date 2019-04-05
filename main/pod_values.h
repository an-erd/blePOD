#ifndef __POD_VALUES_H__
#define __POD_VALUES_H__

#include "esp_event.h"

typedef struct  {
    uint8_t     proximity_uuid[4];
    uint16_t    major;
    uint16_t    minor;
    char        name[8];
} ble_beacon_data_t;

typedef struct  {
    struct tm	timeinfo;
    int8_t      measured_power;
    float       temp;
    uint16_t    humidity;
    uint16_t    x;
    uint16_t    y;
    uint16_t    z;
    uint16_t    battery;
} ble_adv_data_t;

extern ble_beacon_data_t ble_beacon_data[];
extern ble_adv_data_t    ble_adv_data[];


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
        float       temp;
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

uint8_t beacon_maj_min_to_idx(uint16_t maj, uint16_t min);
void pod_values_initialize();
bool pod_values_get_update_display_available();
void pod_values_update_queue_element(queue_element_t *_queue_element);

#endif // __POD_VALUES_H__