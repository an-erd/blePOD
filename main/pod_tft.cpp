#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "esp_err.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "Free_Fonts.h"
#include "pod_main.h"

static const char* TAG = "POD_TFT";

// layout measures for data screen
#define XPAD		    10
#define YPAD		    3
#define XCEN            160
#define BOX_FRAME	    2
#define BOX_SIZE        18
#define BOX_X           10
#define TEXT_X          38
#define STATUS_SPRITE_WIDTH     320
#define STATUS_SPRITE_HEIGHT    22
#define TEXT_HEIGHT_STATUS      22

#define X_BUTTON_A	    65          // display button x position (for center of button)
#define X_BUTTON_B	    160
#define X_BUTTON_C	    255

// layout measures for data screen
#define TEXT_HEIGHT_RUNNING             42
#define TEXT_HEIGHT_DATA                42
#define DATA_SPRITE_WIDTH               320
#define DATA_SPRITE_HEIGHT              42

#define VAL_X_DATA_TR                   (XPAD+75+XPAD+60)   // top right orientation

EventGroupHandle_t pod_display_evg;

// sprites for blocks in status screen and data screen
typedef enum {
    SCREEN_BLOCK_STATUS_WIFI = 0,
    SCREEN_BLOCK_STATUS_NTP,
    SCREEN_BLOCK_STATUS_SD,
    SCREEN_BLOCK_STATUS_BLE,
    SCREEN_OVERARCHING_STATUS,
    SCREEN_OVERARCHING_STATS,
    SCREEN_OVERARCHING_BUTTON,
    SCREEN_DATA_ADV1,
    SCREEN_DATA_ADV2,
    SCREEN_DATA_ADV3,
    SCREEN_NUM_SPRITES,             // number of sprites necessary
} screen_block_t;

TFT_eSprite* spr[SCREEN_NUM_SPRITES] = { 0 };

// initialize all display structs
void pod_screen_status_initialize(pod_screen_status_t *params)
{
    char buffer[64];
    ESP_LOGD(TAG, "pod_screen_status_initialize()");

    // initialize pod_screen_status_t struct
    params->current_screen = SCREEN_NONE;
    params->screen_to_show = SCREEN_STATUS;
    pod_screen_status_update_wifi        (params, WIFI_NOT_CONNECTED, "n/a");
    pod_screen_status_update_ntp         (params, NTP_TIME_NOT_SET);             // TODO where to deactivate?
    snprintf(buffer, 64, BLE_NAME_FORMAT, "-");
   	pod_screen_status_update_ble         (params, BLE_NOT_CONNECTED, buffer);      // TODO where to deactivate?
    pod_screen_status_update_sd          (params, SD_NOT_AVAILABLE);                // TODO where to deactivate?
    pod_screen_status_update_button      (params, BUTTON_A, false, "");
    pod_screen_status_update_button      (params, BUTTON_B, false, "");
    pod_screen_status_update_button      (params, BUTTON_C, false, "");
    pod_screen_status_update_statustext  (params, false, "");

    // queue
    params->q_status.max_len            = 0;
    params->q_status.messages_received  = 0;
    params->q_status.messages_failed    = 0;
    params->show_q_status               = false;

    // voloume
    params->volume                      = 1;

    // create sprites for the building blocks of the status/data screens
    for (int i = 0; i < SCREEN_NUM_SPRITES; i++){
        spr[i] = new TFT_eSprite(&M5.Lcd);
    }
    spr[SCREEN_BLOCK_STATUS_WIFI]->createSprite(STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
    spr[SCREEN_BLOCK_STATUS_NTP]->createSprite (STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
    spr[SCREEN_BLOCK_STATUS_SD]->createSprite  (STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
    spr[SCREEN_BLOCK_STATUS_BLE]->createSprite (STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);

    spr[SCREEN_OVERARCHING_STATUS]->createSprite(STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
    spr[SCREEN_OVERARCHING_STATS]->createSprite (STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
    spr[SCREEN_OVERARCHING_BUTTON]->createSprite(STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);

    spr[SCREEN_DATA_ADV1]->createSprite(STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
    spr[SCREEN_DATA_ADV2]->createSprite(STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
    spr[SCREEN_DATA_ADV3]->createSprite(STATUS_SPRITE_WIDTH, STATUS_SPRITE_HEIGHT);
}

// function to change screen
void pod_screen_change(pod_screen_status_t *params, display_screen_t new_screen)
{
    params->screen_to_show = new_screen;
}

// functions to update status screen data
void pod_screen_status_update_wifi(pod_screen_status_t *params, display_wifi_status_t new_status, const char* new_ssid)
{
    params->wifi_status = new_status;
	memcpy(params->wifi_ssid, new_ssid, strlen(new_ssid) + 1);
}

void pod_screen_status_update_ntp(pod_screen_status_t *params, display_ntp_status_t new_status)
{
    params->ntp_status = new_status;
}

void pod_screen_status_update_ble(pod_screen_status_t *params, display_ble_status_t new_status, const char* new_name)
{
	params->ble_status = new_status;
    if(new_name)
        memcpy(params->ble_name, new_name, strlen(new_name) + 1);
}

void pod_screen_status_update_sd(pod_screen_status_t *params, display_sd_status_t new_status)
{
		params->sd_status = new_status;
}

void pod_screen_status_update_button(pod_screen_status_t *params, uint8_t change_button, bool new_status, const char* new_button_text)
{
    params->show_button[change_button] = new_status;
    if(new_button_text)
        memcpy(params->button_text[change_button], new_button_text, strlen(new_button_text) + 1);
}

void pod_screen_status_update_statustext(pod_screen_status_t *params, bool new_show_text, char* new_status_text)
{
    params->show_status_text = new_show_text;
    if(new_status_text)
    	memcpy(params->status_text, new_status_text, strlen(new_status_text) + 1);
    else
        memcpy(params->status_text, "", strlen("")+1);
}

// the status block consists of
//  a) a colored status box
//      a1) white frame     -> draw only once
//      a2) colored inside  -> update by status_color
//  b) a status text
static void s_draw_status_block(TFT_eSprite *spr, uint16_t status_color, char* status_text, bool first_sprite_draw)
{
    if(first_sprite_draw){
        spr->setFreeFont(FF17);
        spr->setTextColor(TFT_WHITE, TFT_BLACK);
        spr->setTextDatum(TL_DATUM);
        spr->fillSprite(TFT_BLACK);

        // draw frame
        spr->drawRect(BOX_X, 0, BOX_SIZE, BOX_SIZE, TFT_WHITE);
    }

    // draw inlay
	spr->fillRect(BOX_X + BOX_FRAME, 0 + BOX_FRAME, BOX_SIZE - 2 * BOX_FRAME, BOX_SIZE - 2 * BOX_FRAME, status_color);

    // clear and draw text
    spr->fillRect(TEXT_X, 0, 320 - TEXT_X, STATUS_SPRITE_HEIGHT, TFT_BLACK);
    // ESP_LOGI(TAG, "s_draw_status_block(), text %s", status_text);
	spr->drawString(status_text, TEXT_X, 1, GFXFF);
}

static void s_draw_status_text(TFT_eSprite *spr, bool show_status_text, char* status_text, bool first_sprite_draw)
{
    if(first_sprite_draw){
        spr->setFreeFont(FF17);
        spr->setTextColor(TFT_WHITE, TFT_BLACK);
        spr->setTextDatum(TC_DATUM);
        spr->fillSprite(TFT_BLACK);
    }
    // clear and draw text
    spr->fillSprite(TFT_BLACK);
    if(show_status_text)
    	spr->drawString(status_text, XCEN, 0, GFXFF);
}

static void s_draw_button_label(TFT_eSprite *spr,
    bool show_A, char* text_A, bool show_B, char* text_B, bool show_C, char* text_C, bool first_sprite_draw)
{
    if(first_sprite_draw){
        spr->setFreeFont(FF17);
        spr->setTextColor(TFT_WHITE, TFT_BLACK);
        spr->setTextDatum(TC_DATUM);
    }
    spr->fillSprite(TFT_BLACK);
	if (show_A) spr->drawString(text_A, X_BUTTON_A, 0, GFXFF);
	if (show_B) spr->drawString(text_B, X_BUTTON_B, 0, GFXFF);
	if (show_C) spr->drawString(text_C, X_BUTTON_C, 0, GFXFF);
}

static void pod_screen_status_update_display(pod_screen_status_t *params, bool complete)
{
	uint16_t ypos, xpos2;
    uint16_t tmp_color;
    char     buffer[64];

    //  FF17 &FreeSans9pt7b
    //  FF18 &FreeSans12pt7b
    //  FF19 &FreeSans18pt7b
    //  FF20 &FreeSans24pt7b

    if(complete) {
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setFreeFont(FF17);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

    	// Title
        M5.Lcd.setTextDatum(TC_DATUM);
        M5.Lcd.drawString("Starting blePOD...", XCEN, YPAD, GFXFF);
    }

	// 1) WiFi
	switch (params->wifi_status) {
	case WIFI_DEACTIVATED:      tmp_color = TFT_LIGHTGREY;  break;
	case WIFI_NOT_CONNECTED:    tmp_color = TFT_RED;        break;
    case WIFI_SCANNING:         tmp_color = TFT_CYAN;       break;
	case WIFI_CONNECTING:       tmp_color = TFT_YELLOW;     break;
    case WIFI_CONNECTED:        tmp_color = TFT_GREEN;      break;
	default:                    tmp_color = TFT_PURPLE;     break;
	}

    if( (params->wifi_status == WIFI_CONNECTING) || (params->wifi_status == WIFI_CONNECTED) ){
        snprintf(buffer, 64, WIFI_NAME_FORMAT, params->wifi_ssid);
    } else if(params->wifi_status == WIFI_SCANNING){
        snprintf(buffer, 64, WIFI_NAME_FORMAT, "scanning");
    } else {
        snprintf(buffer, 64, WIFI_NAME_FORMAT, "-");
    }
    s_draw_status_block(spr[SCREEN_BLOCK_STATUS_WIFI], tmp_color, buffer, complete);

	// 2) NTP
	switch (params->ntp_status) {
    case NTP_DEACTIVATED:       tmp_color = TFT_LIGHTGREY;  break;
    case NTP_TIME_NOT_SET:      tmp_color = TFT_RED;        break;
    case NTP_UPDATING:          tmp_color = TFT_YELLOW;     break;
    case NTP_UPDATED:           tmp_color = TFT_GREEN;      break;
	default:                    tmp_color = TFT_PURPLE;     break;
	}
    s_draw_status_block(spr[SCREEN_BLOCK_STATUS_NTP], tmp_color, "Time set", complete);

	// 3) SD Card storage
	switch (params->sd_status) {
    case SD_DEACTIVATED:        tmp_color = TFT_LIGHTGREY;  break;
    case SD_NOT_AVAILABLE:      tmp_color = TFT_RED;        break;
    case SD_AVAILABLE:          tmp_color = TFT_GREEN;      break;
	default:                    tmp_color = TFT_PURPLE;     break;
	}
    s_draw_status_block(spr[SCREEN_BLOCK_STATUS_SD], tmp_color, "SD Card", complete);

	// 4) BLE devices
	switch (params->ble_status) {
    case BLE_DEACTIVATED:       tmp_color = TFT_LIGHTGREY;  break;
    case BLE_NOT_CONNECTED:     tmp_color = TFT_RED;        break;
    case BLE_SEARCHING:         tmp_color = TFT_CYAN;       break;
    case BLE_CONNECTING:        tmp_color = TFT_YELLOW;     break;
    case BLE_CONNECTED:         tmp_color = TFT_GREEN;      break;
	default:                    tmp_color = TFT_PURPLE;     break;
	}
    s_draw_status_block(spr[SCREEN_BLOCK_STATUS_BLE], tmp_color, params->ble_name, complete);

	// 5) Status text line
    s_draw_status_text(spr[SCREEN_OVERARCHING_STATUS], params->show_status_text, params->status_text, complete);

	// 6) Button label
    s_draw_button_label(spr[SCREEN_OVERARCHING_BUTTON],
        params->show_button[BUTTON_A], params->button_text[BUTTON_A],
        params->show_button[BUTTON_B], params->button_text[BUTTON_B],
        params->show_button[BUTTON_C], params->button_text[BUTTON_C], complete);

    // push all sprites
    ypos = YPAD + TEXT_HEIGHT_STATUS + 2 * YPAD;
    spr[SCREEN_BLOCK_STATUS_WIFI]->pushSprite(0, ypos);

	ypos += STATUS_SPRITE_HEIGHT + YPAD;
    spr[SCREEN_BLOCK_STATUS_NTP]->pushSprite(0, ypos);

    ypos += STATUS_SPRITE_HEIGHT + YPAD;
    spr[SCREEN_BLOCK_STATUS_SD]->pushSprite(0, ypos);

    ypos += STATUS_SPRITE_HEIGHT + YPAD;
    spr[SCREEN_BLOCK_STATUS_BLE]->pushSprite(0, ypos);

    ypos += STATUS_SPRITE_HEIGHT + 2 * YPAD;
    spr[SCREEN_OVERARCHING_STATUS]->pushSprite(0, ypos);

    ypos = 240 - STATUS_SPRITE_HEIGHT;
    spr[SCREEN_OVERARCHING_BUTTON]->pushSprite(0, ypos);
}

static void s_draw_adv_overview(TFT_eSprite *spr,  uint8_t idx, bool first_sprite_draw)
{
    char buffer[128];
   	char strftime_buf[64];

    if(first_sprite_draw){
        spr->setFreeFont(FF17);
        spr->setTextColor(TFT_WHITE, TFT_BLACK);
        spr->setTextDatum(TL_DATUM);
        spr->fillSprite(TFT_BLACK);

        // draw field title
        // spr->drawString(name, XPAD, 0, GFXFF);
    }

    spr->fillSprite(TFT_BLACK);
    snprintf(buffer, 128, ADV_DATA_FORMAT,  // ADV_DATA_FORMAT  "%s: %3d %+5.1f %3d %4d %+4.1f %+4.1f %+4.1f"
        ble_beacon_data[idx].name,
        ble_adv_data[idx].measured_power,
        ble_adv_data[idx].temp,
        ble_adv_data[idx].humidity,
        ble_adv_data[idx].battery,
        ble_adv_data[idx].x/16384.,
        ble_adv_data[idx].y/16384.,
        ble_adv_data[idx].z/16384.);
    spr->drawString(buffer, 0, 0, GFXFF);

    ESP_LOGD(TAG, "(0x%04x%04x) rssi %3d | temp %5.1f | hum %3d | x %+4.1f | y %+4.1f | z %+4.1f | batt %4d",
        ble_beacon_data[idx].major, ble_beacon_data[idx].minor,
        ble_adv_data[idx].measured_power,
        ble_adv_data[idx].temp,
        ble_adv_data[idx].humidity,
        ble_adv_data[idx].x/16384.,
        ble_adv_data[idx].y/16384.,
        ble_adv_data[idx].z/16384.,
        ble_adv_data[idx].battery);

    ESP_LOGD(TAG, "raw: x %+6d | y %+6d | z %+6d",
        ble_adv_data[idx].x,
        ble_adv_data[idx].y,
        ble_adv_data[idx].z);

    strftime(strftime_buf, sizeof(strftime_buf), "%c", &ble_adv_data[idx].timeinfo);
    ESP_LOGD(TAG, "received from queue to display: time %s, data %s", strftime_buf, buffer);
}

void pod_screen_data_update_display(pod_screen_status_t *params, bool complete) {

    uint16_t ypos;
    char buffer[64];

    if(complete) {
        M5.Lcd.fillScreen(TFT_BLACK);
        M5.Lcd.setFreeFont(FF19);
        M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    // 1-3) beacon data, 3 lines
    s_draw_adv_overview(spr[SCREEN_DATA_ADV1], 0, true);
    s_draw_adv_overview(spr[SCREEN_DATA_ADV2], 1, true);
    s_draw_adv_overview(spr[SCREEN_DATA_ADV3], 2, true);

    // 4) Statistics text line
    snprintf(buffer, 64, STATS_QUEUE_FORMAT,
        params->q_status.max_len, params->q_status.messages_send, params->q_status.messages_received, params->q_status.messages_failed);
    s_draw_status_text(spr[SCREEN_OVERARCHING_STATS], params->show_q_status, buffer, complete);

    // 5) Status text line
    s_draw_status_text(spr[SCREEN_OVERARCHING_STATUS], params->show_status_text, params->status_text, complete);

	// 6) Button label
    s_draw_button_label(spr[SCREEN_OVERARCHING_BUTTON],
        params->show_button[BUTTON_A], params->button_text[BUTTON_A],
        params->show_button[BUTTON_B], params->button_text[BUTTON_B],
        params->show_button[BUTTON_C], params->button_text[BUTTON_C], complete);

    // push all sprites
    ypos = YPAD;
    spr[SCREEN_DATA_ADV1]->pushSprite(0, ypos);
	ypos += STATUS_SPRITE_HEIGHT + YPAD;

    spr[SCREEN_DATA_ADV2]->pushSprite(0, ypos);
    ypos += STATUS_SPRITE_HEIGHT + YPAD;

    spr[SCREEN_DATA_ADV3]->pushSprite(0, ypos);
    ypos += STATUS_SPRITE_HEIGHT + YPAD;

    spr[SCREEN_OVERARCHING_STATS]->pushSprite(0, ypos);
    ypos += STATUS_SPRITE_HEIGHT + YPAD + YPAD;

    spr[SCREEN_OVERARCHING_STATUS]->pushSprite(0, ypos);

    ypos = 240 - STATUS_SPRITE_HEIGHT;
    spr[SCREEN_OVERARCHING_BUTTON]->pushSprite(0, ypos);
}

void pod_screen_status_update_queue(pod_screen_status_t *params, uint8_t cur_len, bool inc_send, bool inc_received, bool inc_failed)
{
    if(cur_len >  params->q_status.max_len)
        params->q_status.max_len = cur_len;
    if(inc_send)
        params->q_status.messages_send++;
    if(inc_received)
        params->q_status.messages_received++;
    if(inc_failed)
        params->q_status.messages_failed++;

    ESP_LOGV(TAG, "queue status: max_len=%u received=%u failed=%u",
        params->q_status.max_len, params->q_status.messages_received, params->q_status.messages_failed);
}

void pod_screen_status_update_queue_status(pod_screen_status_t *params, bool new_show_status)
{
    params->show_q_status = new_show_status;
}


void pod_screen_task(void *pvParameters)
{
    bool complete = false;
    pod_screen_status_t* params = (pod_screen_status_t*)pvParameters;

    ESP_LOGI(TAG, "pod_screen_task: started");

    for (;;)
    {
        while(!(xEventGroupWaitBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT,
                pdTRUE, pdFALSE, portMAX_DELAY) & POD_DISPLAY_UPDATE_BIT));

        ESP_LOGD(TAG, "pod_screen_task: update display, screen_to_show %u", (uint8_t) params->screen_to_show);
        if(params->current_screen != params->screen_to_show){
                ESP_LOGD(TAG, "pod_screen_task: switching from '%u' to '%u'", params->current_screen, params->screen_to_show);
        }

        switch(params->screen_to_show){
        case SCREEN_SPLASH:
            ESP_LOGW(TAG, "pod_screen_task: SCREEN_SPLASH - not available yet");
            break;
        case SCREEN_STATUS:
            ESP_LOGD(TAG, "pod_screen_task: SCREEN_STATUS");
            if(params->current_screen != params->screen_to_show){
                params->current_screen = params->screen_to_show;
                complete = true;
            }
            pod_screen_status_update_display(params, complete);
            break;
        case SCREEN_DATA:
            ESP_LOGD(TAG, "pod_screen_task: SCREEN_DATA");
            if(params->current_screen != params->screen_to_show){
                params->current_screen = params->screen_to_show;
                complete = true;
            }
            pod_screen_data_update_display(params, complete);
            break;
        case SCREEN_CONFIG:
            ESP_LOGW(TAG, "pod_screen_task: SCREEN_CONFIG - not available yet");
            break;
        case SCREEN_OTA:
            ESP_LOGW(TAG, "pod_screen_task: SCREEN_OTA - not available yet");
            // pod_screen_ota_update_display();
            break;
        case SCREEN_SCREENSAVER:
            ESP_LOGW(TAG, "pod_screen_task: SCREEN_SCREENSAVER - not available yet");
            break;
        case SCREEN_POWEROFF:
            ESP_LOGW(TAG, "pod_screen_task: SCREEN_POWEROFF - not available yet");
            break;
        case SCREEN_POWERON:
            ESP_LOGW(TAG, "pod_screen_task: SCREEN_POWERON - not available yet");
            break;
        default:
            ESP_LOGW(TAG, "pod_screen_task: unhandled: %d", params->screen_to_show);
            break;
        }

        complete = false;
    }
}