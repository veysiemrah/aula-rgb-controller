/*
 * f87d -- D-Bus daemon for Aula F87 keyboard RGB control
 *
 * Owns all USB access and effect execution. Clients (GUI, CLI)
 * communicate via D-Bus session bus (org.f87.Control).
 */

#include <systemd/sd-bus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "device_manager.h"
#include "effect_manager.h"
#include "dbus_interface.h"
#include "idle_monitor.h"
#include "profile_manager.h"
#include "error_history.h"
#include <f87/logger.h>

static volatile sig_atomic_t g_quit = 0;
static f87d_device_manager_t g_devmgr;
static f87d_effect_manager_t g_effmgr;
static f87d_idle_monitor_t g_idle;
static f87d_dbus_ctx_t g_dbus_ctx;
static f87d_error_ring_t g_error_ring;

static void signal_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

static void on_device_connected(const f87_device_info *info, void *userdata)
{
    (void)userdata;
    printf("f87d: device connected — %s (%04X:%04X)\n",
           info->product, info->vendor_id, info->product_id);
    f87d_dbus_emit_device_connected(&g_dbus_ctx, info->product,
                                     info->vendor_id, info->product_id);
}

static void on_device_disconnected(void *userdata)
{
    (void)userdata;
    printf("f87d: device disconnected\n");
    f87d_effmgr_stop(&g_effmgr);
    f87d_dbus_emit_device_disconnected(&g_dbus_ctx);
}

static f87d_device_callbacks_t g_dev_cbs = {
    .on_connected = on_device_connected,
    .on_disconnected = on_device_disconnected,
    .userdata = NULL,
};

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    f87_log_init(F87_LOG_JOURNAL);
    f87d_error_ring_init(&g_error_ring);
    f87_log_set_callback(f87d_error_ring_log_callback, &g_error_ring);

    if (f87d_devmgr_init(&g_devmgr) < 0) {
        fprintf(stderr, "f87d: failed to init device manager\n");
        return EXIT_FAILURE;
    }
    f87d_effmgr_init(&g_effmgr);
    f87d_idle_init(&g_idle);

    f87d_devmgr_scan(&g_devmgr, &g_dev_cbs);

    /* Restore last state */
    f87d_profile_t last_profile;
    if (f87d_profile_load_last(&last_profile) == 0) {
        f87_device *dev = f87d_devmgr_get_device(&g_devmgr);
        if (dev) {
            printf("f87d: restoring last state (%s, effect %d)\n",
                   last_profile.category, last_profile.effect_id);
            if (strcmp(last_profile.category, "hw") == 0 && last_profile.effect_id >= 0) {
                f87d_effmgr_set_hw(&g_effmgr, dev, last_profile.effect_id,
                                    last_profile.brightness, last_profile.speed,
                                    last_profile.colorful,
                                    last_profile.color[0], last_profile.color[1],
                                    last_profile.color[2]);
            } else if (strcmp(last_profile.category, "sw") == 0) {
                f87d_effmgr_set_sw(&g_effmgr, dev, last_profile.effect_id,
                                    last_profile.brightness, last_profile.speed,
                                    last_profile.color[0], last_profile.color[1],
                                    last_profile.color[2], 0);
            } else if (strcmp(last_profile.category, "music") == 0) {
                f87d_effmgr_set_music(&g_effmgr, dev, last_profile.effect_id,
                                       last_profile.brightness,
                                       last_profile.color[0], last_profile.color[1],
                                       last_profile.color[2], last_profile.gain);
            } else if (strcmp(last_profile.category, "sensor") == 0) {
                f87d_effmgr_set_sensor(&g_effmgr, dev,
                    last_profile.sensor_profile[0] ? last_profile.sensor_profile : NULL,
                    last_profile.sensor_config_path[0] ? last_profile.sensor_config_path : NULL);
            }
            if (last_profile.side_light > 0)
                f87d_effmgr_set_side_light(&g_effmgr, dev, last_profile.side_light);
            if (last_profile.battery_light > 0)
                f87d_effmgr_set_battery_light(&g_effmgr, dev, last_profile.battery_light);
        }
    }

    sd_bus *bus = NULL;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "f87d: session bus: %s\n", strerror(-r));
        f87d_devmgr_destroy(&g_devmgr);
        return EXIT_FAILURE;
    }

    g_dbus_ctx = (f87d_dbus_ctx_t){
        .bus = bus,
        .devmgr = &g_devmgr,
        .effmgr = &g_effmgr,
        .idle = &g_idle,
        .error_ring = &g_error_ring,
    };

    r = f87d_dbus_register(bus, &g_dbus_ctx);
    if (r < 0) {
        fprintf(stderr, "f87d: dbus register: %s\n", strerror(-r));
        sd_bus_unref(bus);
        f87d_devmgr_destroy(&g_devmgr);
        return EXIT_FAILURE;
    }

    r = sd_bus_request_name(bus, "org.f87.Control", 0);
    if (r < 0) {
        fprintf(stderr, "f87d: bus name: %s\n", strerror(-r));
        sd_bus_unref(bus);
        f87d_devmgr_destroy(&g_devmgr);
        return EXIT_FAILURE;
    }

    printf("f87d: running (device %s)\n",
           g_devmgr.connected ? "connected" : "not found");

    uint64_t last_poll_us = 0;
    const uint64_t poll_interval_us = 5000000; /* 5 seconds */

    while (!g_quit) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) break;
        if (r > 0) continue;

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_us = (uint64_t)ts.tv_sec * 1000000ULL +
                          (uint64_t)ts.tv_nsec / 1000ULL;
        if (now_us - last_poll_us >= poll_interval_us) {
            f87d_devmgr_poll(&g_devmgr, &g_dev_cbs);
            last_poll_us = now_us;

            /* Idle timeout: disabled while SW effect running */
            if (f87d_effmgr_has_sw_running(&g_effmgr)) {
                f87d_idle_set_enabled(&g_idle, false);
            } else {
                f87d_idle_set_enabled(&g_idle, true);
                if (f87d_idle_check(&g_idle)) {
                    printf("f87d: idle timeout, exiting\n");
                    break;
                }
            }
        }

        r = sd_bus_wait(bus, 1000000);
        if (r < 0 && r != -EINTR) break;
    }

    printf("f87d: shutting down\n");
    f87d_effmgr_destroy(&g_effmgr);
    sd_bus_release_name(bus, "org.f87.Control");
    sd_bus_unref(bus);
    f87d_devmgr_destroy(&g_devmgr);
    f87_log_shutdown();
    f87d_error_ring_destroy(&g_error_ring);
    return EXIT_SUCCESS;
}
