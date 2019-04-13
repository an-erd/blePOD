#ifndef __POD_MQTT_H__
#define __POD_MQTT_H__

#include "pod_main.h"

void pod_mqtt_init();
void pod_mqtt_send_queue_element(queue_element_t *_queue_element);

#endif // __POD_MQTT_H__