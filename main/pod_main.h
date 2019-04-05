#ifndef __BLEPOD_MAIN_H__
#define __BLEPOD_MAIN_H__

#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_err.h"
#include "esp_bt_main.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include <M5Stack.h>
#include <NeoPixelBus.h>
#include "pod_values.h"
#include "pod_button.h"
#include "pod_tft.h"
#include "pod_timer.h"
#include "pod_idle_timer.h"
#include "pod_archiver.h"
#include "pod_update.h"
#include "pod_ledc.h"
#include "iot_ota.h"
#include "iot_param.h"
#include "pod_wifi.h"
#include "pod_tft.h"
#include "pod_sntp.h"
#include "pod_gattc.h"

// IOT param
typedef struct {
    uint16_t volume;
} param_t;

#define PARAM_NAMESPACE "blePOD"
#define PARAM_KEY       "struct"


// blePOD event group
#define POD_WIFI_ACTIVATED_BIT              (BIT0)      // WiFi is activated
#define POD_WIFI_SCANNING_BIT               (BIT1)      // WiFi scanning for APs
#define POD_WIFI_CONNECTING_BIT	            (BIT2)      // WiFi connecting to appropriate AP
#define POD_WIFI_CONNECTED_BIT		        (BIT3)      // WiFi got IP
#define POD_WIFI_RETRY_BIT                  (BIT4)      // Try WiFi (and NTP if necessary) again
#define POD_NTP_ACTIVATED_BIT               (BIT5)      // NTP is activated
#define POD_NTP_UPDATING_BIT    	        (BIT6)      // NTP update process running
#define POD_NTP_UPDATED_BIT			        (BIT7)      // NTP time set
#define POD_BLE_ACTIVATED_BIT               (BIT8)      // BLE is activated
#define POD_BLE_SCANNING_BIT     	        (BIT9)      // BLE scanning for devices
#define POD_BLE_CONNECTING_BIT    	        (BIT10)     // BLE connecting to appropriate device
#define POD_BLE_CONNECTED_BIT               (BIT11)     // BLE connected to device
#define POD_BLE_RETRY_BIT                   (BIT12)     // Try BLE again
#define POD_SD_ACTIVATED_BIT                (BIT13)     // SD card is activated
#define POD_SD_AVAILABLE_BIT                (BIT14)     // SD function available
#define POD_DATA_LIGHT_ACT_BIT              (BIT15)     // Data w/light output activated
#define POD_BTN_A_RETRY_WIFI_BIT            (BIT16)     // Retry Wifi in progress
#define POD_BTN_B_RETRY_BLE_BIT             (BIT17)
#define POD_BTN_C_CNT_BIT                   (BIT18)     // Continue with flow to next function
#define POD_DATA_SCREEN_BIT                 (BIT19)     // Diesplay data screen
#define POD_OTA_RUNNING_BIT                 (BIT20)     // OTA is running and ota_task exists
extern EventGroupHandle_t pod_evg;

// POD SD card event group
#define POD_SD_WRITE_COMPLETED_BUFFER_EVT   (BIT0)      // write completed buffers
#define POD_SD_WRITE_ALL_BUFFER_EVT			(BIT1)		// write incompleted buffers, too
#define POD_SD_PROBE_EVT                    (BIT2)      // check availability of card and function -> set Bits for status
#define POD_SD_GENERATE_TESTDATA_EVT        (BIT3)      // fill the buffer array with test data
extern EventGroupHandle_t pod_sd_evg;

// blePOD Client callback function events
typedef enum {
    // workflow events
    POD_STARTUP_EVT              = 0,       /*!< When the disPOD event loop started, the event comes, last call from app_main() */
    POD_BASIC_INIT_DONE_EVT,                /*!< When the basic init has completed, the event comes */
    POD_SPLASH_AND_NETWORK_INIT_DONE_EVT,   /*!< When the display splash etc. (not coded yet) has completed, the event comes */
    POD_WIFI_INIT_DONE_EVT,                 /*!< When the WiFi init, not yet completed, the event comes */
    POD_WIFI_GOT_IP_EVT,                    /*!< When the WiFi is connected and got an IP, the event comes */
    POD_NTP_INIT_DONE_EVT,                  /*!< When the NTP update (successfull or failed!) completed, the event comes */
    POD_SD_INIT_DONE_EVT,                   /*!< When the SD mount/probe/unmount is completed, the event comes */
    POD_BLE_DEVICE_DONE_EVT,                /*!< When the BLE device initialization is completed, the event comes */
    POD_STARTUP_COMPLETE_EVT,               /*!< When the startup sequence was run through, the event comes */
    POD_RETRY_WIFI_EVT,                     /*!< When an retry of WiFi is requested, the event comes */
    POD_RETRY_BLE_EVT,                      /*!< When an retry of BLE is requested, the event comes */
    POD_GO_TO_DATA_SCREEN_EVT,              /*!< When starting the running screen is requested, the event comes */
    POD_BLE_DISCONNECT_EVT,                 /*!< When BLE disconnects, the event comes */
    // activity events
    POD_BUTTON_TAP_EVT,                     /*!< When a button has been TAP event (=released), the event comes */
    POD_BUTTON_2SEC_RELEASE_EVT,            /*!< When a button has been released after 2s, the event comes */
    POD_BUTTON_5SEC_RELEASE_EVT,            /*!< When a button has been released after 5s, the event comes */
    //
    POD_EVENT_MAX
} blepod_cb_event_t;

ESP_EVENT_DECLARE_BASE(ACTIVITY_EVENTS);
ESP_EVENT_DECLARE_BASE(WORKFLOW_EVENTS);
extern esp_event_loop_handle_t pod_loop_handle;

// queue for data values
extern const TickType_t xTicksToWait;
extern QueueHandle_t values_queue;

// pod screen data
extern pod_screen_status_t pod_screen_status;

// global running values data struct
// extern dataValuesStruct_t data_values;

#define BLE_NAME_FORMAT         "BLE Device (%s)"
#define WIFI_NAME_FORMAT        "WiFi (%s)"
#define STATUS_VOLUME_FORMAT    "Volume: %u"
#define STATS_QUEUE_FORMAT      "Q: max %u, snd %u, rec %u, fail %u"
#define ADV_DATA_FORMAT         "%s: %3d %5.1f %3d %4d"

// SD card
#define sdPIN_NUM_MISO 19
#define sdPIN_NUM_MOSI 23
#define sdPIN_NUM_CLK  18
#define sdPIN_NUM_CS   4

// M5Stack NeoPixels
extern NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> pixels;
#define colorSaturation 128

// Speaker
#define SPEAKER_PIN 25

// UART
#define USE_SERIAL Serial

// OTA error codes and update status
extern const char* otaErrorNames[];

typedef struct {
	bool		    chg_;
	bool		    otaUpdateStarted_;
	bool		    otaUpdateEnd_;
	unsigned int    otaUpdateProgress_;
	bool		    otaUpdateError_;
	int			    otaUpdateErrorNr_;
} otaUpdate_t;

// missing functions
#define __min(a,b) \
    ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a < _b ? _a : _b; })

#define __max(a,b) \
    ({ __typeof__ (a) _a = (a); \
        __typeof__ (b) _b = (b); \
        _a > _b ? _a : _b; })

#define __map(x,in_min,in_max,out_min,out_max)\
    ({ __typeof__ (x) _x = (x); \
        __typeof__ (in_min) _in_min = (in_min); \
        __typeof__ (in_max) _in_max = (in_max); \
        __typeof__ (out_min) _out_min = (out_min); \
        __typeof__ (out_max) _out_max = (out_max); \
        (_x - _in_min) * (_out_max - _out_min) / (_in_max - _in_min) + _out_min; })

#endif // __POD_MAIN_H__
