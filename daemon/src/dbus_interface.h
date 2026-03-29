#ifndef F87D_DBUS_INTERFACE_H
#define F87D_DBUS_INTERFACE_H

#include <systemd/sd-bus.h>
#include "device_manager.h"
#include "effect_manager.h"
#include "idle_monitor.h"
#include "profile_manager.h"

typedef struct {
    sd_bus *bus;
    f87d_device_manager_t *devmgr;
    f87d_effect_manager_t *effmgr;
    f87d_idle_monitor_t *idle;
} f87d_dbus_ctx_t;

int f87d_dbus_register(sd_bus *bus, f87d_dbus_ctx_t *ctx);

int f87d_dbus_emit_device_connected(f87d_dbus_ctx_t *ctx,
                                     const char *product,
                                     uint16_t vid, uint16_t pid);
int f87d_dbus_emit_device_disconnected(f87d_dbus_ctx_t *ctx);
int f87d_dbus_emit_effect_changed(f87d_dbus_ctx_t *ctx,
                                   int effect_id, const char *category);

#endif
