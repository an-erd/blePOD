#include "esp_log.h"

#include "pod_main.h"

static const char* TAG = "POD_UPDATER";

void pod_check_and_update_display()
{
    // TODO
    // if(pod_runvalues_get_update_display_available(&running_values)){
    //     xEventGroupSetBits(dispod_display_evg, DISPOD_DISPLAY_UPDATE_BIT);
    // }
}

void pod_update_task(void *pvParameters)
{
    ESP_LOGI(TAG, "pod_update_task: started");
    queue_element_t new_queue_element;
	char strftime_buf[64];

    for(;;) {
        if(xQueueReceive(values_queue, (void * )&new_queue_element, (portTickType)portMAX_DELAY)) {
            uint8_t q_wait = uxQueueMessagesWaiting(values_queue);
            // dispod_screen_status_update_queue(&pod_screen_status, q_wait, pdFALSE, pdTRUE, pdFALSE);

            switch(new_queue_element.id) {
            case ID_BLEADV:
	            ESP_LOGD(TAG, "received from queue: ID_BLEADV: ");
                // C %3u", new_queue_element.data.rsc.cadance);
                // dispod_runvalues_update_RSCValues(&running_values, new_queue_element.data.rsc.cadance);
                // dispod_check_and_update_display();
                // dispod_archiver_add_RSCValues(new_queue_element.data.rsc.cadance);
                // dispod_touch_timer();
                break;
			default:
                ESP_LOGI(TAG, "unknown event id");
                break;
            }
        }
    }
}
