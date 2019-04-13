// blePOD - receive beacon BLE adv and display on M5Stack-Fire

#include "esp_types.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/portmacro.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "esp32-hal-ledc.h"
#include "soc/timer_group_struct.h"
#include "soc/timer_group_reg.h"

#include <M5Stack.h>
#include "pod_main.h"

static const char* TAG = "POD";

// Event group
EventGroupHandle_t pod_evg;

// Event loop
ESP_EVENT_DEFINE_BASE(WORKFLOW_EVENTS);
ESP_EVENT_DEFINE_BASE(ACTIVITY_EVENTS);
esp_event_loop_handle_t pod_loop_handle;

// Storing values from BLE data device struct and queue
// runningValuesStruct_t   data_values;
QueueHandle_t values_queue;

// Storing screen information
pod_screen_status_t pod_screen_status;

// time to wait in
const TickType_t xTicksToWait = 100 / portTICK_PERIOD_MS;

// temp return value from xEventGroupWaitBits, ... functions
EventBits_t uxBits;

#define M5STACK_FIRE_NEO_NUM_LEDS 10
#define M5STACK_FIRE_NEO_DATA_PIN 15

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> pixels(M5STACK_FIRE_NEO_NUM_LEDS, M5STACK_FIRE_NEO_DATA_PIN);
RgbColor NEOPIXEL_white(colorSaturation);
RgbColor NEOPIXEL_black(0);

static void pod_initialize()
{
    if(CONFIG_USE_WIFI)
        xEventGroupSetBits(pod_evg, POD_WIFI_ACTIVATED_BIT);
    if(CONFIG_USE_SNTP)
        xEventGroupSetBits(pod_evg, POD_NTP_ACTIVATED_BIT);
    if(CONFIG_USE_MQTT)
        xEventGroupSetBits(pod_evg, POD_MQTT_ACTIVATED_BIT);
    if(CONFIG_USE_SD)
        xEventGroupSetBits(pod_evg, POD_SD_ACTIVATED_BIT);
}

static void initialize_spiffs()
{
    esp_err_t ret;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false
    };

    ESP_LOGD(TAG, "SPIFFS: calling esp_vfs_spiffs_register()");
    ret = esp_vfs_spiffs_register(&conf);
    ESP_LOGD(TAG, "SPIFFS: esp_vfs_spiffs_register() returned");

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "SPIFFS: Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS: Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "SPIFFS: Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS: Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SPIFFS: Partition size: total: %d, used: %d", total, used);
    }
}

static void initialize_nvs()
{
    esp_err_t ret;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
}

static void s_leave_data_screen()
{
    xEventGroupClearBits(pod_evg, POD_DATA_SCREEN_BIT);
    xEventGroupClearBits(pod_evg, POD_DATA_LIGHT_ACT_BIT);
    pixels.ClearTo(NEOPIXEL_black);
    pixels.Show();
    pod_screen_change(&pod_screen_status, SCREEN_STATUS);
    pod_screen_status_update_statustext(&pod_screen_status, false, "");
    pod_screen_status_update_button(&pod_screen_status, BUTTON_A, false, "");
    pod_screen_status_update_button(&pod_screen_status, BUTTON_B, false, "");
    pod_screen_status_update_button(&pod_screen_status, BUTTON_C, false, "");
    xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
    ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_STARTUP_COMPLETE_EVT, NULL, 0, portMAX_DELAY));
}

void pod_m5stack_task(void *pvParameters){
    ESP_LOGI(TAG, "pod_m5stack_task: started");

    for(;;){
        M5.update();
        pod_m5_buttons_test();
        if(pod_is_idle_timer_expired()){
            pod_idle_timer_stop();
            ESP_LOGI(TAG, "pod_m5stack_task: idle timer expired");

            if((xEventGroupWaitBits(pod_evg, POD_DATA_SCREEN_BIT,
                pdFALSE, pdFALSE, 0) & POD_DATA_SCREEN_BIT) ){
                ESP_LOGI(TAG, "pod_m5stack_task: idle timer expired - leave running screen");
        //         s_leave_running_screen();
			    ESP_LOGD(TAG, "pod_m5stack_task: POD_SD_WRITE_COMPLETED_BUFFER_EVT | POD_SD_WRITE_ALL_BUFFER_EVT");
			    xEventGroupSetBits(pod_sd_evg, POD_SD_WRITE_COMPLETED_BUFFER_EVT | POD_SD_WRITE_ALL_BUFFER_EVT);
            } else {
                ESP_LOGI(TAG, "dispod_m5stack_task: idle timer expired - power off");
                // esp_bluedroid_disable();
                // esp_wifi_stop();
                M5.powerOFF();
            }
        }

        // feed watchdog
        TIMERG0.wdt_wprotect=TIMG_WDT_WKEY_VALUE;
        TIMERG0.wdt_feed=1;
        TIMERG0.wdt_wprotect=0;
    }
}

static void ota_task(void *arg)
{
    ESP_LOGI(TAG, "ota_task(): test mutex");

    xEventGroupSetBits(pod_evg, POD_OTA_RUNNING_BIT);
    iot_ota_start(CONFIG_OTA_SERVER_IP, CONFIG_OTA_SERVER_PORT, CONFIG_OTA_FILE_NAME, 60000/portTICK_RATE_MS);
    xEventGroupClearBits(pod_evg, POD_OTA_RUNNING_BIT);

    vTaskDelete(NULL);      // delete current task
}

static void s_try_ota_update()
{
    ESP_LOGI(TAG, "s_do_ota(): free heap size before ota: %d", esp_get_free_heap_size());
    // ESP_ERROR_CHECK(
        xTaskCreate(ota_task, "ota_task", 1024 * 8, NULL, 5, NULL);
        // );

    // wait for the task to start
    while( !(xEventGroupWaitBits(pod_evg, POD_OTA_RUNNING_BIT,
        pdFALSE, pdFALSE, portMAX_DELAY) & POD_OTA_RUNNING_BIT) );

    while( (iot_ota_get_ratio() < 100) && (xEventGroupWaitBits(pod_evg, POD_OTA_RUNNING_BIT, pdFALSE, pdFALSE, 0) & POD_OTA_RUNNING_BIT) ){
        ESP_LOGI(TAG, "OTA progress: %d %%", iot_ota_get_ratio());
        vTaskDelay(500 / portTICK_RATE_MS);
    }

    ESP_LOGI(TAG, "OTA while-loop done: complete %d %%", iot_ota_get_ratio());
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_LOGI(TAG, "free heap size after ota: %d", esp_get_free_heap_size());
    // esp_restart();
}

static void run_on_event(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    esp_err_t ret;
    param_t pod_param;

    switch(id){
        case POD_STARTUP_EVT:
            ESP_LOGV(TAG, "POD_STARTUP_EVT");

            // Initialize the M5Stack (without speaker) object and the M5Stack NeoPixels
            M5.begin(true, true, true); // LCD, SD, Serial
            M5.setWakeupButton(BUTTON_A_PIN);
            pod_init_beep(SPEAKER_PIN, 1000);
            xEventGroupClearBits(pod_evg, POD_DATA_LIGHT_ACT_BIT);
            pixels.Begin();
            pixels.Show();

            pod_initialize();
            pod_screen_status_initialize(&pod_screen_status);

            ret = iot_param_load(PARAM_NAMESPACE, PARAM_KEY, &pod_param);
            if(ret == ESP_OK){
                pod_screen_status.volume = pod_param.volume;
                ESP_LOGI(TAG, "POD_STARTUP_EVT: read param ok, read volume %u", pod_param.volume);
            } else {
                // volume is already initialized, so it's fine
                ESP_LOGE(TAG, "POD_STARTUP_EVT: read param failed, ret = %d", ret);
            }
            xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);

            // pod_runvalues_initialize(&running_values);
            pod_archiver_initialize();
            pod_idle_timer_stop();

            ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_BASIC_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            break;
        case POD_BASIC_INIT_DONE_EVT:
            ESP_LOGV(TAG, "POD_BASIC_INIT_DONE_EVT");
            pod_wifi_network_init();
            // show splash and some info here
            ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_SPLASH_AND_NETWORK_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            break;
        case POD_SPLASH_AND_NETWORK_INIT_DONE_EVT:
            ESP_LOGV(TAG, "POD_SPLASH_AND_NETWORK_INIT_DONE_EVT");
            uxBits = xEventGroupWaitBits(pod_evg, POD_WIFI_ACTIVATED_BIT, pdFALSE, pdFALSE, 0);
            if(uxBits & POD_WIFI_ACTIVATED_BIT){
                // WiFi activated -> connect to WiFi
                ESP_LOGV(TAG, "connect to WiFi");
                pod_wifi_network_up();
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_WIFI_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            } else {
                // WiFi not activated (and thus no NTP), jump to SD mount
                ESP_LOGV(TAG, "no WiFi configured (thus no NTP), mount SD next");
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_NTP_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            }
            break;
        case POD_WIFI_INIT_DONE_EVT:
            ESP_LOGV(TAG, "POD_WIFI_INIT_DONE_EVT");
            break;
        case POD_WIFI_GOT_IP_EVT:
            ESP_LOGV(TAG, "POD_WIFI_GOT_IP_EVT");
            xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
            uxBits = xEventGroupWaitBits(pod_evg, POD_WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, 0);
            if(uxBits & POD_WIFI_CONNECTED_BIT){
                ESP_LOGV(TAG, "WiFi connected, update NTP");
                pod_sntp_check_time();
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_NTP_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            } else {
                // WiFi configured, but not connected, jump to SD mount
                ESP_LOGV(TAG, "no WiFi connection thus no NTP, do SD and connect to BLE next");
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_NTP_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            }
            break;
        case POD_NTP_INIT_DONE_EVT:
            ESP_LOGV(TAG, "POD_NTP_INIT_DONE_EVT");
            xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
            uxBits = xEventGroupWaitBits(pod_evg, POD_WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, 0);
            if(uxBits & POD_WIFI_CONNECTED_BIT){
                ESP_LOGV(TAG, "WiFi connected, connect MQTT");
                pod_mqtt_init();
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_MQTT_DONE_EVT, NULL, 0, portMAX_DELAY));
            } else {
                // WiFi configured, but not connected, jump to SD mount
                ESP_LOGV(TAG, "no WiFi connection thus no NTP/MQTT, do SD and connect to BLE next");
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_MQTT_DONE_EVT, NULL, 0, portMAX_DELAY));
            }
            break;
        case POD_MQTT_DONE_EVT:
            ESP_LOGV(TAG, "POD_MQTT_DONE_EVT");
            uxBits = xEventGroupWaitBits(pod_evg, POD_SD_ACTIVATED_BIT, pdFALSE, pdFALSE, 0);
            ESP_LOGD(TAG, "uxBits: POD_SD_ACTIVATED_BIT = %u, uxBits = %u", POD_SD_ACTIVATED_BIT, uxBits);
            if( ( uxBits & POD_SD_ACTIVATED_BIT) == POD_SD_ACTIVATED_BIT){
                ESP_LOGD(TAG, "POD_NTP_INIT_DONE_EVT: pod_sd_evg DISPOD_SD_PROBE_EVT");
                xEventGroupSetBits(pod_sd_evg, POD_SD_PROBE_EVT);
            } else {
                ESP_LOGD(TAG, "POD_NTP_INIT_DONE_EVT: skip POD_SD_PROBE_EVT");
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_SD_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
            }
            break;
        case POD_SD_INIT_DONE_EVT:
            ESP_LOGV(TAG, "POD_SD_INIT_DONE_EVT");
            uxBits = xEventGroupWaitBits(pod_evg, POD_BLE_RETRY_BIT, pdTRUE, pdFALSE, 0);
            ESP_LOGD(TAG, "uxBits: POD_BLE_RETRY_BIT = %u, uxBits = %u", POD_BLE_RETRY_BIT, uxBits);
            if(!(uxBits & POD_BLE_RETRY_BIT)){
                ESP_LOGD(TAG, "> !POD_BLE_RETRY_BIT");
                pod_ble_initialize();       //TODO
                pod_ble_app_register();
                pod_ble_start_scanning();
            } else {
                ESP_LOGD(TAG, "> POD_BLE_RETRY_BIT");
                // pod_ble_start_scanning();    // TODO check whether it works with additional call to start_scanning.
            }
            break;
        case POD_BLE_DEVICE_DONE_EVT:
            ESP_LOGV(TAG, "POD_BLE_DEVICE_DONE_EVT");
            ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_STARTUP_COMPLETE_EVT, NULL, 0, portMAX_DELAY));
            break;
        case POD_STARTUP_COMPLETE_EVT:{
            bool retryWifi = false;
            bool retryBLE = false;
            bool cont = false;
            ESP_LOGV(TAG, "POD_STARTUP_COMPLETE_EVT");
            // at this point we've either
            // - no Wifi configured: no WiFi, no NTP, maybe BLE
            // - WiFi configured: maybe WiFi -> maybe updated NTP, maybe BLE

            // no WiFi but retry set -> jump to WiFi again (POD_DISPLAY_INIT_DONE_EVT)
            if(!(xEventGroupWaitBits(pod_evg, POD_WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, 0) & POD_WIFI_CONNECTED_BIT)){
                // option: no WiFi, retry WiFi -> Button A
                pod_screen_status_update_button(&pod_screen_status, BUTTON_A, true, "WiFi");
                xEventGroupSetBits(pod_evg, POD_BTN_A_RETRY_WIFI_BIT);
                retryWifi = true;
            }
            if(!(xEventGroupWaitBits(pod_evg, POD_BLE_CONNECTED_BIT, pdFALSE, pdFALSE, 0) & POD_BLE_CONNECTED_BIT)){
                // option: no BLE, retry BLE -> Button B
                pod_screen_status_update_button(&pod_screen_status, BUTTON_B, true, "BLE");
                xEventGroupSetBits(pod_evg, POD_BTN_B_RETRY_BLE_BIT);
                retryBLE = true;
            }
            if((xEventGroupWaitBits(pod_evg, POD_BLE_CONNECTED_BIT, pdFALSE, pdFALSE, 0) & POD_BLE_CONNECTED_BIT)){
                // option: BLE avail, go to running screen -> Button C
                pod_screen_status_update_button(&pod_screen_status, BUTTON_C, true, "Cont.");
                xEventGroupSetBits(pod_evg, POD_BTN_C_CNT_BIT);
                cont = true;
            }

            ESP_LOGD(TAG, "POD_STARTUP_COMPLETE_EVT: retryWifi %d, retryBLE %d, cont %d", retryWifi, retryBLE, cont);

            if(retryWifi && retryBLE && (!cont)){
                pod_screen_status_update_statustext(&pod_screen_status, true, "Retry WiFi or BLE?");
            } else if(retryWifi && (!retryBLE) && cont){
                pod_screen_status_update_statustext(&pod_screen_status, true, "Retry WiFi or continue?");
            } else if(retryBLE && (!retryWifi) && (!cont)){
                pod_screen_status_update_statustext(&pod_screen_status, true, "Retry BLE?");
            } else if((!retryWifi) && (!retryBLE) && cont){
                pod_screen_status_update_statustext(&pod_screen_status, true, "Continue?");
            }
            xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
            }
            pod_idle_timer_set(CONFIG_IDLE_TIME_STATUS_SCREEN * 1000);
            break;
        case POD_RETRY_WIFI_EVT:
            ESP_LOGV(TAG, "POD_RETRY_WIFI_EVT");
            pod_idle_timer_stop();
            break;
        case POD_RETRY_BLE_EVT:
            ESP_LOGV(TAG, "POD_RETRY_BLE_EVT");
            pod_idle_timer_stop();
            break;
        case POD_GO_TO_DATA_SCREEN_EVT:
            ESP_LOGV(TAG, "POD_GO_TO_DATA_SCREEN_EVT");
            xEventGroupSetBits(pod_evg, POD_DATA_SCREEN_BIT);
            pod_screen_change(&pod_screen_status, SCREEN_DATA);
            pod_screen_status_update_button(&pod_screen_status, BUTTON_A, true, "A");
            pod_screen_status_update_button(&pod_screen_status, BUTTON_B, true, "B");
            pod_screen_status_update_button(&pod_screen_status, BUTTON_C, true, "Back");
            xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
            pod_idle_timer_set(CONFIG_IDLE_TIME_DATA_SCREEN * 1000);
            break;
        case POD_BLE_DISCONNECT_EVT:
            ESP_LOGV(TAG, "POD_BLE_DISCONNECT_EVT");
            if(xEventGroupWaitBits(pod_evg, POD_DATA_SCREEN_BIT, pdFALSE, pdFALSE, 0) & POD_DATA_SCREEN_BIT){
                ESP_LOGV(TAG, "TODO POD_BLE_DISCONNECT_EVT -> POD_DATA_SCREEN_BIT");
                s_leave_data_screen();
    			ESP_LOGD(TAG, "Archiver: POD_SD_WRITE_COMPLETED_BUFFER_EVT | POD_SD_WRITE_ALL_BUFFER_EVT");
    			xEventGroupSetBits(pod_sd_evg, POD_SD_WRITE_COMPLETED_BUFFER_EVT | POD_SD_WRITE_ALL_BUFFER_EVT);
            } else {
                ESP_LOGV(TAG, "POD_BLE_DISCONNECT_EVT -> not POD_DATA_SCREEN_BIT -> POD_STARTUP_COMPLETE_EVT");
                ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_STARTUP_COMPLETE_EVT, NULL, 0, portMAX_DELAY));
            }
            break;
        //
        case POD_BUTTON_TAP_EVT: {
            button_unit_t button_unit = *(button_unit_t*) event_data;
            ESP_LOGV(TAG, "POD_BUTTON_TAP_EVT, button id %d", button_unit.btn_id);

            pod_touch_timer();

            // come here from POD_STARTUP_COMPLETE_EVT
            if((xEventGroupWaitBits(pod_evg, POD_BTN_A_RETRY_WIFI_BIT | POD_BTN_B_RETRY_BLE_BIT | POD_BTN_C_CNT_BIT, pdFALSE, pdFALSE, 0)
                    & (POD_BTN_A_RETRY_WIFI_BIT | POD_BTN_B_RETRY_BLE_BIT | POD_BTN_C_CNT_BIT))){
                switch(button_unit.btn_id){
                    case BUTTON_A:
                        // Retry WiFI
                        if((xEventGroupWaitBits(pod_evg, POD_BTN_A_RETRY_WIFI_BIT, pdFALSE, pdFALSE, 0) & POD_BTN_A_RETRY_WIFI_BIT)){
                            xEventGroupClearBits(pod_evg, POD_BTN_A_RETRY_WIFI_BIT | POD_BTN_B_RETRY_BLE_BIT | POD_BTN_C_CNT_BIT);
                            xEventGroupSetBits(pod_evg, POD_WIFI_RETRY_BIT);
                            pod_screen_status_update_statustext(&pod_screen_status, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_A, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_B, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_C, false, "");
                            ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_SPLASH_AND_NETWORK_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
                        }
                        break;
                    case BUTTON_B:
                        // Retry WiFI
                        if((xEventGroupWaitBits (pod_evg, POD_BTN_B_RETRY_BLE_BIT, pdFALSE, pdFALSE, 0) & POD_BTN_B_RETRY_BLE_BIT)){
                            xEventGroupClearBits(pod_evg, POD_BTN_A_RETRY_WIFI_BIT | POD_BTN_B_RETRY_BLE_BIT | POD_BTN_C_CNT_BIT);
                            xEventGroupSetBits  (pod_evg, POD_BLE_RETRY_BIT);
                            pod_screen_status_update_statustext(&pod_screen_status, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_A, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_B, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_C, false, "");
                            ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_SD_INIT_DONE_EVT, NULL, 0, portMAX_DELAY));
                        }
                        break;
                    case BUTTON_C:
                        // Cont.
                        if((xEventGroupWaitBits (pod_evg, POD_BTN_C_CNT_BIT, pdFALSE, pdFALSE, 0) & POD_BTN_C_CNT_BIT)){
                            xEventGroupClearBits(pod_evg, POD_BTN_A_RETRY_WIFI_BIT | POD_BTN_B_RETRY_BLE_BIT | POD_BTN_C_CNT_BIT);
                            pod_screen_status_update_statustext(&pod_screen_status, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_A, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_B, false, "");
                            pod_screen_status_update_button    (&pod_screen_status, BUTTON_C, false, "");
                            ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_GO_TO_DATA_SCREEN_EVT, NULL, 0, portMAX_DELAY));
                            // open new data value file
                            pod_archiver_set_new_file();

        					// Test data generation, when entering running screen
        					// xEventGroupSetBits(dispod_sd_evg, DISPOD_SD_GENERATE_TESTDATA_EVT);
                        }
                        break;
                    default:
                        ESP_LOGW(TAG, "unhandled button");
                        break;
                }
            }

            // showing data screen
            if((xEventGroupWaitBits(pod_evg, POD_DATA_SCREEN_BIT, pdFALSE, pdFALSE, 0) & POD_DATA_SCREEN_BIT)){
                switch(button_unit.btn_id){
                    case BUTTON_A:
                    break;
                    case BUTTON_B:
                    break;
                    case BUTTON_C:
                        s_leave_data_screen();
			        	// ESP_LOGD(TAG, "Archiver: POD_SD_WRITE_COMPLETED_BUFFER_EVT | POD_SD_WRITE_ALL_BUFFER_EVT");
			        	// xEventGroupSetBits(pod_sd_evg, POD_SD_WRITE_COMPLETED_BUFFER_EVT | POD_SD_WRITE_ALL_BUFFER_EVT);
                    break;
                    default:
                        ESP_LOGW(TAG, "unhandled button");
                    break;
                }
            }
            }
            break;
        case POD_BUTTON_2SEC_RELEASE_EVT:{
            button_unit_t button_unit = *(button_unit_t*) event_data;
            ESP_LOGV(TAG, "POD_BUTTON_2SEC_PRESS_EVT, button id %d", button_unit.btn_id);

            pod_touch_timer();

            // showing status screen with WiFi available -> allow for OTA
            if((xEventGroupWaitBits(pod_evg, POD_BTN_B_RETRY_BLE_BIT | POD_BTN_C_CNT_BIT, pdFALSE, pdFALSE, 0)
                    & ( POD_BTN_B_RETRY_BLE_BIT | POD_BTN_C_CNT_BIT))){
                switch(button_unit.btn_id){
                case BUTTON_B:
                    // start/try OTA
                    s_try_ota_update();
                    break;
                }
            }
            // showing data screen
            if((xEventGroupWaitBits(pod_evg, POD_DATA_SCREEN_BIT, pdFALSE, pdFALSE, 0) & POD_DATA_SCREEN_BIT)){
                char buffer[64];
                switch(button_unit.btn_id){
                case BUTTON_A:
                    break;
                case BUTTON_B:
                    // Toggle show queue status
                    pod_screen_status_update_queue_status(&pod_screen_status, !pod_screen_status.show_q_status);
                    break;
                }
            }
            }
            break;
        case POD_BUTTON_5SEC_RELEASE_EVT:{
            button_unit_t button_unit = *(button_unit_t*) event_data;
            ESP_LOGV(TAG, "POD_BUTTON_5SEC_PRESS_EVT, button id %d", button_unit.btn_id);
            // unused yet
            pod_touch_timer();
            }
            break;
        default:
            ESP_LOGW(TAG, "unhandled event base/id %s:%d", base, id);
            break;
    }
}

extern "C" void app_main()
{
    ESP_LOGI(TAG, "app_main() entered");

	// adjust logging
	esp_log_level_set("phy_init",       ESP_LOG_INFO);
	esp_log_level_set("nvs",            ESP_LOG_INFO);
	esp_log_level_set("tcpip_adapter",  ESP_LOG_INFO);
	esp_log_level_set("BTDM_INIT",      ESP_LOG_INFO);
    // esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_TCP", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_SSL", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    // initialize NVS
    initialize_nvs();

    // Initialize SPIFFS file system
    initialize_spiffs();

    // disPOD overall initialization
    ESP_LOGI(TAG, "initialize pod");

    // event groups
    pod_evg         = xEventGroupCreate();
    pod_display_evg = xEventGroupCreate();
    pod_sd_evg      = xEventGroupCreate();
    pod_timer_evg   = xEventGroupCreate();

    // pod values initialize
    pod_values_initialize();

    // create BLE adv queue to get BLE notification decoded and put into this queue
    values_queue = xQueueCreate( 10, sizeof( queue_element_t ) );

    esp_event_loop_args_t pod_loop_args = {
        .queue_size = 5,
        .task_name = "loop_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY
    };

    // Create the pod event loop
    ESP_ERROR_CHECK(esp_event_loop_create(&pod_loop_args, &pod_loop_handle));

    // Register the handler for task iteration event.
    ESP_ERROR_CHECK(esp_event_handler_register_with(pod_loop_handle, ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, run_on_event, NULL));

    // // run the display task with the same priority as the current process
    ESP_LOGI(TAG, "Starting pod_screen_task()");
    xTaskCreate(pod_screen_task, "pod_screen_task", 4096, &pod_screen_status, uxTaskPriorityGet(NULL), NULL);

    // // run the updater task with the same priority as the current process
    ESP_LOGI(TAG, "Starting pod_update_task()");
    xTaskCreate(pod_update_task, "pod_update_task", 4096, NULL, uxTaskPriorityGet(NULL), NULL);

    // // run the archiver task with the same priority as the current process
    ESP_LOGI(TAG, "Starting pod_archiver_task()");
    xTaskCreate(pod_archiver_task, "pod_archiver_task", 4096, NULL, uxTaskPriorityGet(NULL), NULL);

    // // run the M5STack task
    ESP_LOGI(TAG, "Starting pod_m5stack_task()");
    xTaskCreate(pod_m5stack_task, "pod_m5stack_task", 4096, NULL, uxTaskPriorityGet(NULL), NULL);

    // // run the timer task and the timer
    ESP_LOGI(TAG, "Starting pod_timer_task()");
    xTaskCreate(pod_timer_task, "pod_timer_task", 4096, NULL, uxTaskPriorityGet(NULL), NULL);
	pod_timer_initialize();

    // push a startup event in the loop
    ESP_ERROR_CHECK(esp_event_post_to(pod_loop_handle, WORKFLOW_EVENTS, POD_STARTUP_EVT, NULL, 0, portMAX_DELAY));
}
