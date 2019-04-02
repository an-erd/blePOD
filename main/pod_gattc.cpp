#include <string.h>
#include <stdbool.h>
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_gattc_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "pod_main.h"

static const char* TAG = "POD_ADV_RECEIVER";

#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00)>>8) + (((x)&0xFF)<<8))

const uint8_t uuid_zeros[ESP_UUID_LEN_32] = {0x00, 0x00, 0x00, 0x00};

typedef struct {
    uint8_t flags[3];
    uint8_t length;
    uint8_t type;
    uint16_t company_id;
    uint16_t beacon_type;
}__attribute__((packed)) esp_ble_mybeacon_head_t;

typedef struct {
    uint8_t proximity_uuid[4];
    uint16_t major;
    uint16_t minor;
    int8_t measured_power;
}__attribute__((packed)) esp_ble_mybeacon_vendor_t;

typedef struct {
    uint16_t temp;
    uint16_t humidity;
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint16_t battery;
}__attribute__((packed)) esp_ble_mybeacon_payload_t;

typedef struct {
    esp_ble_mybeacon_head_t     mybeacon_head;
    esp_ble_mybeacon_vendor_t   mybeacon_vendor;
    esp_ble_mybeacon_payload_t  mybeacon_payload;
}__attribute__((packed)) esp_ble_mybeacon_t;

esp_ble_mybeacon_head_t mybeacon_common_head = {
    .flags = {0x02, 0x01, 0x04},
    .length = 0x1A,
    .type = 0xFF,
    .company_id = 0x0059,   // ENDIAN_CHANGE_U16 ?
    .beacon_type = 0x1502   // ENDIAN_CHANGE_U16 ?
};

esp_ble_mybeacon_vendor_t vendor_config = {
    .proximity_uuid = {0x01, 0x12, 0x23, 0x34},
    .major = 0x0102,
    .minor = 0x0304,
    .measured_power = (int8_t)0xC5
};

bool esp_ble_is_mybeacon_packet (uint8_t *adv_data, uint8_t adv_data_len){
    bool result = false;

    if ((adv_data != NULL) && (adv_data_len == 0x1E)){
        if (!memcmp(adv_data, (uint8_t*)&mybeacon_common_head, sizeof(mybeacon_common_head))){
            result = true;
        }
    }

    return result;
}

esp_err_t esp_ble_config_mybeacon_data (esp_ble_mybeacon_vendor_t *vendor_config, esp_ble_mybeacon_t *ibeacon_adv_data){
    if ((vendor_config == NULL) || (ibeacon_adv_data == NULL) || (!memcmp(vendor_config->proximity_uuid, uuid_zeros, sizeof(uuid_zeros)))){
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&ibeacon_adv_data->mybeacon_head, &mybeacon_common_head, sizeof(esp_ble_mybeacon_head_t));
    memcpy(&ibeacon_adv_data->mybeacon_vendor, vendor_config, sizeof(esp_ble_mybeacon_vendor_t));

    return ESP_OK;
}

///Declare static functions
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,   // BLE_SCAN_TYPE_ACTIVE, BLE_SCAN_TYPE_PASSIVE
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

uint8_t *adv_data = NULL;
uint8_t adv_data_len = 0;

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;
    uint32_t duration = 0;

    switch (event){
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT");
            break;
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT");
            //the unit of the duration is second, 0 means scan permanently
            duration = 0;
            esp_ble_gap_start_scanning(duration);
            break;
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
            ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_START_COMPLETE_EVT");
            //scan start complete event to indicate scan start successfully or failed
            if ((err = param->scan_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(err));
            }
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT");
            //adv start complete event to indicate adv start successfully or failed
            if ((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Adv start failed: %s", esp_err_to_name(err));
            }
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            ESP_LOGD(TAG, "ESP_GAP_BLE_SCAN_RESULT_EVT");
            queue_element_t new_queue_element;
            BaseType_t xStatus;

            esp_ble_gap_cb_param_t *scan_result = (esp_ble_gap_cb_param_t *)param;
            switch (scan_result->scan_rst.search_evt) {
                case ESP_GAP_SEARCH_INQ_RES_EVT:
                    ESP_LOGD(TAG, "ESP_GAP_SEARCH_INQ_RES_EVT");
                    // adv_data = esp_ble_resolve_adv_data(scan_result->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_data_len);
                    // ESP_LOGI(TAG, "searched Device Name Len %d", adv_data_len);
                    // ESP_LOG_BUFFER_HEXDUMP(TAG, adv_data, adv_data_len, ESP_LOG_INFO);
                    // ESP_LOG_BUFFER_HEXDUMP(TAG, scan_result->scan_rst.ble_adv, 31, ESP_LOG_INFO);

                    if(esp_ble_is_mybeacon_packet (scan_result->scan_rst.ble_adv, scan_result->scan_rst.adv_data_len)){
                        ESP_LOGD(TAG, "mybeacon found");
                        // ESP_LOG_BUFFER_HEXDUMP(TAG, scan_result->scan_rst.ble_adv, 31, ESP_LOG_INFO);
                        esp_ble_mybeacon_t *mybeacon_data = (esp_ble_mybeacon_t*)(scan_result->scan_rst.ble_adv);
                        ESP_LOGI(TAG, "(0x%04x%04x) rssi %3d | temp %5.1f | hum %3d | x %+6d | y %+6d | z %+6d | batt %4d",
                            ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_vendor.major),
                            ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_vendor.minor),
                            scan_result->scan_rst.rssi,
                            ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.temp)/10.,
                            ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.humidity),
                            (int16_t)ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.x),
                            (int16_t)ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.y),
                            (int16_t)ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.z),
                            ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.battery) );

                        // dispod_runvalues_update_RSCValues(&running_values, instantaneousCadence);    // TODO
                        new_queue_element.id = ID_BLEADV;
                        new_queue_element.data.ble_adv.major        = ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_vendor.major);
                        new_queue_element.data.ble_adv.minor        = ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_vendor.minor);
                        new_queue_element.data.ble_adv.measured_power = scan_result->scan_rst.rssi;
                        new_queue_element.data.ble_adv.temp         = ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.temp)/10.;
                        new_queue_element.data.ble_adv.humidity     = ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_vendor.minor);
                        new_queue_element.data.ble_adv.x            = (int16_t)ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.x);
                        new_queue_element.data.ble_adv.y            = (int16_t)ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.y);
                        new_queue_element.data.ble_adv.z            = (int16_t)ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.z);
                        new_queue_element.data.ble_adv.battery      = ENDIAN_CHANGE_U16(mybeacon_data->mybeacon_payload.battery);

                        bool q_send_fail = pdFALSE;
                        uint8_t q_wait = 0;
                        xStatus = xQueueSendToBack(values_queue, &new_queue_element, xTicksToWait);
                        if(xStatus != pdTRUE ){
                            ESP_LOGW(TAG, "ESP_GATTC_NOTIFY_EVT: NOTIFY_HANDLE_RSC: cannot send to queue");
                            q_send_fail = pdTRUE;
                        }
                        q_wait = uxQueueMessagesWaiting(values_queue);
                        pod_screen_status_update_queue(&pod_screen_status, q_wait, pdTRUE, pdFALSE, q_send_fail);
                    } else {
                        // ESP_LOGI(TAG, "mybeacon not found");
                    }
                    break;
                default:
                    ESP_LOGI(TAG, "ESP_GAP_BLE_SCAN_RESULT_EVT - default");
                    break;
            }
            break;
        }
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            if ((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS){
                ESP_LOGE(TAG, "Scan stop failed: %s", esp_err_to_name(err));
            }
            else {
                ESP_LOGI(TAG, "Stop scan successfully");
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if ((err = param->adv_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS){
                ESP_LOGE(TAG, "Adv stop failed: %s", esp_err_to_name(err));
            }
            else {
                ESP_LOGI(TAG, "Stop adv successfully");
            }
            break;
        default:
            ESP_LOGI(TAG, "esp_gap_cb - default (%u)", event);
            break;
    }
}

void pod_ble_initialize()
{
    esp_err_t ret;

    ret = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
	ESP_ERROR_CHECK(ret);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "%s initialize controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "%s enable controller failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "%s init bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "%s enable bluetooth failed: %s", __func__, esp_err_to_name(ret));
        return;
    }
}

void pod_ble_app_register()
{
    esp_err_t ret;

    //register the callback function to the gap module
    ret = esp_ble_gap_register_callback(esp_gap_cb);
    if (ret != ESP_OK){
        ESP_LOGE(TAG, "%s gap register failed, error code = %x, %s",
        __func__, ret, esp_err_to_name(ret));
        return;
    }
}

void pod_ble_start_scanning()
{
    esp_ble_gap_set_scan_params(&ble_scan_params);
}
