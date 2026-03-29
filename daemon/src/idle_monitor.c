#include "idle_monitor.h"
#include <time.h>

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void f87d_idle_init(f87d_idle_monitor_t *mon)
{
    mon->last_activity_us = now_us();
    mon->timeout_us = F87D_IDLE_TIMEOUT_US;
    mon->enabled = true;
}

void f87d_idle_touch(f87d_idle_monitor_t *mon)
{
    mon->last_activity_us = now_us();
}

void f87d_idle_set_enabled(f87d_idle_monitor_t *mon, bool enabled)
{
    mon->enabled = enabled;
    if (enabled)
        mon->last_activity_us = now_us();
}

bool f87d_idle_check(f87d_idle_monitor_t *mon)
{
    if (!mon->enabled)
        return false;
    return (now_us() - mon->last_activity_us) >= mon->timeout_us;
}
