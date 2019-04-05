#include "esp_log.h"

#include "pod_main.h"

static const char* TAG = "POD_UPDATER";

void pod_check_and_update_display()
{
    if(pod_values_get_update_display_available()){
        xEventGroupSetBits(pod_display_evg, POD_DISPLAY_UPDATE_BIT);
    }
}

void pod_update_task(void *pvParameters)
{
    ESP_LOGI(TAG, "pod_update_task: started");
    queue_element_t new_queue_element;
	char strftime_buf[64];

    for(;;) {
        if(xQueueReceive(values_queue, (void * )&new_queue_element, (portTickType)portMAX_DELAY)) {
            uint8_t q_wait = uxQueueMessagesWaiting(values_queue);
            pod_screen_status_update_queue(&pod_screen_status, q_wait, pdFALSE, pdTRUE, pdFALSE);

            switch(new_queue_element.id) {
            case ID_BLEADV:
	            ESP_LOGD(TAG, "received from queue: ID_BLEADV");
                pod_values_update_queue_element(&new_queue_element);
                pod_check_and_update_display();
                // pod_archiver_add_adv_value(new_queue_element);
                pod_touch_timer();
                break;
			default:
                ESP_LOGI(TAG, "unknown event id");
                break;
            }
        }
    }
}
