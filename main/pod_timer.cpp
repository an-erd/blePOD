#include "esp_timer.h"
#include "esp_log.h"

#include "pod_main.h"
#include "pod_timer.h"
#include "pod_ledc.h"

static const char* TAG = "POD_TIMER";
EventGroupHandle_t pod_timer_evg;

static void IRAM_ATTR timer_callback(void* arg);

typedef enum {
	TIMER_PERIODIC = 0,
	MAX_TIMER_NUMBER,
} timer_pod_id;

static esp_timer_handle_t timer_handles[MAX_TIMER_NUMBER] = { 0 };
static uint8_t			  timer_num    [MAX_TIMER_NUMBER]
	= { TIMER_PERIODIC,};


void pod_timer_initialize()
{
    const esp_timer_create_args_t periodic_timer_on_args = {
        &timer_callback,
         (void*) &timer_num[TIMER_PERIODIC],
         ESP_TIMER_TASK,
        "periodic_tick"
    };

	ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_on_args, &timer_handles[TIMER_PERIODIC]));
}

void pod_timer_start_tick()
{
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handles[TIMER_PERIODIC], TICK_TIMER_US));

	ESP_LOGD(TAG, "pod_timer_start_metronome(), time since boot: %lld us", esp_timer_get_time());
}

void pod_timer_stop_tick()
{
    ESP_ERROR_CHECK(esp_timer_stop(timer_handles[TIMER_PERIODIC]));

	ESP_LOGV(TAG, "pod_timer_stop_metronome(), time since boot: %lld us", esp_timer_get_time());
}

static void IRAM_ATTR timer_callback(void* arg)
{
	uint8_t     timer_nr                = *((uint8_t *) arg);
	EventBits_t uxBits                  = 0;
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;	// xHigherPriorityTaskWoken must be initialised to pdFALSE.
	BaseType_t xResult;

    int64_t time_since_boot = esp_timer_get_time();
    ESP_LOGD(TAG, "timer_tick_callback, time since boot: %lld us", time_since_boot);

	switch(timer_nr){
		case TIMER_PERIODIC:  uxBits = POD_TIMER_TICK_ON_BIT;	                    break;
		default: ESP_LOGW(TAG, "timer_tick_callback, unhandled timer %u", timer_nr);    break;
	}

	ESP_LOGD(TAG, "timer_tick_callback, handled %u", timer_nr);

	xResult = xEventGroupSetBitsFromISR(pod_timer_evg, uxBits, &xHigherPriorityTaskWoken);

    // message posted successfully?
    if( xResult == pdPASS ){
        portYIELD_FROM_ISR();
    }
}

void pod_timer_task(void *pvParameters)
{
    EventBits_t     uxBits;

    ESP_LOGI(TAG, "pod_timer_task: started");

    for (;;)
    {
        uxBits = xEventGroupWaitBits(pod_timer_evg,
					POD_TIMER_TICK_ON_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

        if(uxBits & POD_TIMER_TICK_ON_BIT){
            ESP_LOGD(TAG, "pod_timer_task: POD_TIMER_TICK_ON_BIT");
			xEventGroupClearBits(pod_timer_evg, POD_TIMER_TICK_ON_BIT);
        }
	}
}
