#ifndef __POD_IDLE_TIMER_H__
#define __POD_IDLE_TIMER_H__

void pod_idle_timer_set(uint32_t duration);
void pod_idle_timer_stop();
bool pod_is_idle_timer_expired();
void pod_touch_timer();

#endif // __POD_IDLE_TIMER_H__
