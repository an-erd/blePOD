#include "esp_log.h"
#include <stdlib.h>
#include "pod_main.h"

static const char* TAG = "POD_MQTT";
static esp_mqtt_client_handle_t s_client;

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id = 0;

    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            pod_screen_status_update_mqtt(&pod_screen_status, MQTT_CONNECTED, (char*) "");
            xEventGroupSetBits(pod_evg, POD_MQTT_CONNECTED_BIT);
            xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            pod_screen_status_update_mqtt(&pod_screen_status, MQTT_NOT_CONNECTED, (char*) "");
            xEventGroupClearBits(pod_evg, POD_MQTT_CONNECTED_BIT);
            xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
    }
    return ESP_OK;
}

void pod_mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .event_handle   = mqtt_event_handler,
        .host           = CONFIG_MQTT_HOST,
        .uri            = CONFIG_MQTT_BROKER_URL,
        .port           = CONFIG_MQTT_PORT,
        .client_id      = "ESP-TEST",
        .username       = CONFIG_MQTT_USERNAME,
        .password       = CONFIG_MQTT_PASSWORD
        // .user_context = (void *)your_context
    };

    // esp_mqtt_client_handle_t
    s_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(s_client);
}

void pod_mqtt_send_queue_element(queue_element_t *_queue_element)
{
    int msg_id = 0;
    char buffer[128];
    // uint16_t maj = _queue_element->data.ble_adv.major;
    // uint16_t min = _queue_element->data.ble_adv.minor;
    // uint8_t  idx = beacon_maj_min_to_idx(maj, min);
    // ADV_DATA_FORMAT  "%s: %3d %+5.1f %3d %4d %+4.1f %+4.1f %+4.1f"
    snprintf(buffer, 128, "%+5.1f", _queue_element->data.ble_adv.temp);
    msg_id = esp_mqtt_client_publish(s_client, "/andreaserd/feeds/temp", buffer, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

    snprintf(buffer, 128, "%d", _queue_element->data.ble_adv.humidity);
    msg_id = esp_mqtt_client_publish(s_client, "/andreaserd/feeds/humidity", buffer, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

    snprintf(buffer, 128, "%d", _queue_element->data.ble_adv.measured_power);
    msg_id = esp_mqtt_client_publish(s_client, "/andreaserd/feeds/rssi", buffer, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

    snprintf(buffer, 128, "%d", _queue_element->data.ble_adv.battery);
    msg_id = esp_mqtt_client_publish(s_client, "/andreaserd/feeds/battery", buffer, 0, 1, 0);
    ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

    // ble_adv_data[idx].timeinfo       = _queue_element->data.ble_adv.timeinfo;
    // ble_adv_data[idx].measured_power = _queue_element->data.ble_adv.measured_power;
    // ble_adv_data[idx].temp           = _queue_element->data.ble_adv.temp;
    // ble_adv_data[idx].humidity       = _queue_element->data.ble_adv.humidity;
    // ble_adv_data[idx].x              = _queue_element->data.ble_adv.x;
    // ble_adv_data[idx].y              = _queue_element->data.ble_adv.y;
    // ble_adv_data[idx].z              = _queue_element->data.ble_adv.z;
    // ble_adv_data[idx].battery        = _queue_element->data.ble_adv.battery;

    // xEventGroupSetBits(s_values_evg, (EventBits_t) (1 << idx));
}


