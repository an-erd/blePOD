#include "esp_log.h"
#include <stdlib.h>
#include "pod_values.h"
#include "pod_main.h"

static const char* TAG = "POD_VALUES";

#define UPDATE_BEAC0     (BIT0)
#define UPDATE_BEAC1     (BIT1)
#define UPDATE_BEAC2     (BIT2)
#define UPDATE_BEAC3     (BIT3)
#define UPDATE_BEAC4     (BIT4)
static EventGroupHandle_t s_values_evg;

ble_beacon_data_t   ble_beacon_data[CONFIG_BLE_DEVICE_COUNT];
ble_adv_data_t      ble_adv_data   [CONFIG_BLE_DEVICE_COUNT];

uint8_t beacon_maj_min_to_idx(uint16_t maj, uint16_t min)
{
    if( (maj == CONFIG_BLE_DEVICE_1_MAJ) && (min == CONFIG_BLE_DEVICE_1_MIN) ) return 0;
    if( (maj == CONFIG_BLE_DEVICE_2_MAJ) && (min == CONFIG_BLE_DEVICE_2_MIN) ) return 1;
    if( (maj == CONFIG_BLE_DEVICE_3_MAJ) && (min == CONFIG_BLE_DEVICE_3_MIN) ) return 2;
    if( (maj == CONFIG_BLE_DEVICE_4_MAJ) && (min == CONFIG_BLE_DEVICE_4_MIN) ) return 3;
    if( (maj == CONFIG_BLE_DEVICE_5_MAJ) && (min == CONFIG_BLE_DEVICE_5_MIN) ) return 4;

    ESP_LOGE(TAG, "beacon_maj_min_to_idx: unknown maj %d min %d", maj, min);

    return 0;
}

void pod_values_initialize()
{
    ble_beacon_data[0] = { {0x01, 0x12, 0x23, 0x34}, CONFIG_BLE_DEVICE_1_MAJ, CONFIG_BLE_DEVICE_1_MIN, };
    ble_beacon_data[1] = { {0x01, 0x12, 0x23, 0x34}, CONFIG_BLE_DEVICE_2_MAJ, CONFIG_BLE_DEVICE_2_MIN, };
    ble_beacon_data[2] = { {0x01, 0x12, 0x23, 0x34}, CONFIG_BLE_DEVICE_3_MAJ, CONFIG_BLE_DEVICE_3_MIN, };
    ble_beacon_data[3] = { {0x01, 0x12, 0x23, 0x34}, CONFIG_BLE_DEVICE_4_MAJ, CONFIG_BLE_DEVICE_4_MIN, };
    ble_beacon_data[4] = { {0x01, 0x12, 0x23, 0x34}, CONFIG_BLE_DEVICE_5_MAJ, CONFIG_BLE_DEVICE_5_MIN, };
    snprintf(ble_beacon_data[0].name, 8, "Beac%d", 1);
    snprintf(ble_beacon_data[1].name, 8, "Beac%d", 2);
    snprintf(ble_beacon_data[2].name, 8, "Beac%d", 3);
    snprintf(ble_beacon_data[3].name, 8, "Beac%d", 4);
    snprintf(ble_beacon_data[4].name, 8, "Beac%d", 5);

    s_values_evg = xEventGroupCreate();
}

bool pod_values_get_update_display_available()
{
    EventBits_t uxBits;

    uxBits = xEventGroupWaitBits(s_values_evg,
        UPDATE_BEAC0 | UPDATE_BEAC1 | UPDATE_BEAC2 | UPDATE_BEAC3 | UPDATE_BEAC4, pdTRUE, pdFALSE, 0);

    return (uxBits & (UPDATE_BEAC0 | UPDATE_BEAC1 | UPDATE_BEAC2 | UPDATE_BEAC3 | UPDATE_BEAC4));
}

void pod_values_update_queue_element(queue_element_t *_queue_element)
{
    uint16_t maj = _queue_element->data.ble_adv.major;
    uint16_t min = _queue_element->data.ble_adv.minor;
    uint8_t  idx = beacon_maj_min_to_idx(maj, min);

    ble_adv_data[idx].timeinfo       = _queue_element->data.ble_adv.timeinfo;
    ble_adv_data[idx].measured_power = _queue_element->data.ble_adv.measured_power;
    ble_adv_data[idx].temp           = _queue_element->data.ble_adv.temp;
    ble_adv_data[idx].humidity       = _queue_element->data.ble_adv.humidity;
    ble_adv_data[idx].x              = _queue_element->data.ble_adv.x;
    ble_adv_data[idx].y              = _queue_element->data.ble_adv.y;
    ble_adv_data[idx].z              = _queue_element->data.ble_adv.z;
    ble_adv_data[idx].battery        = _queue_element->data.ble_adv.battery;

    xEventGroupSetBits(s_values_evg, (EventBits_t) (1 << idx));
}
