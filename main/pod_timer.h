#ifndef __POD_TIMER_H__
#define __POD_TIMER_H__

#define	TICK_TIMER_US				333333

// POD timer event group
#define POD_TIMER_TICK_ON_BIT          (BIT0)
extern EventGroupHandle_t pod_timer_evg;

void pod_timer_initialize();
void pod_timer_start_tick();
void pod_timer_stop_tick();

void pod_timer_task(void *pvParameters);

#endif // __POD_TIMER_H__
