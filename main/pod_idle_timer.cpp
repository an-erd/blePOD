#include "pod_main.h"
#include "pod_idle_timer.h"

static const char* TAG = "POD_IDLE_TIMER";

static uint32_t     s_count     = 0;
static uint32_t     s_duration  = 0;
static bool         s_running   = false;

void pod_idle_timer_set(uint32_t duration_ms)
{
    s_duration = duration_ms;
    pod_touch_timer();
    s_running = true;

    ESP_LOGD(TAG, "pod_idle_timer_set(): %u", s_duration);
}

void pod_idle_timer_stop()
{
    s_running = false;
    ESP_LOGD(TAG, "pod_idle_timer_stop()");
}

void pod_touch_timer()
{
    s_count = millis() + s_duration;
    ESP_LOGD(TAG, "pod_touch_timer(): millis = %lu, s_dur = %u, s_count = %u", millis(), s_duration, s_count );
}

bool pod_is_idle_timer_expired()
{
    bool expired;

    if(!s_running)
        return false;

    expired = millis() >= s_count;
    if(expired)
        ESP_LOGD(TAG, "pod_idle_timer_expired(): expired");

    return expired;
}
