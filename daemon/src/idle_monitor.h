#ifndef F87D_IDLE_MONITOR_H
#define F87D_IDLE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#define F87D_IDLE_TIMEOUT_US (5ULL * 60 * 1000000) /* 5 minutes */

typedef struct {
    uint64_t last_activity_us;
    uint64_t timeout_us;
    bool enabled;
} f87d_idle_monitor_t;

void f87d_idle_init(f87d_idle_monitor_t *mon);
void f87d_idle_touch(f87d_idle_monitor_t *mon);
void f87d_idle_set_enabled(f87d_idle_monitor_t *mon, bool enabled);
bool f87d_idle_check(f87d_idle_monitor_t *mon);

#endif
