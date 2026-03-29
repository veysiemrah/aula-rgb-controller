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

static volatile sig_atomic_t g_quit = 0;
static f87d_device_manager_t g_devmgr;

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
}

static void on_device_disconnected(void *userdata)
{
    (void)userdata;
    printf("f87d: device disconnected\n");
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

    if (f87d_devmgr_init(&g_devmgr) < 0) {
        fprintf(stderr, "f87d: failed to init device manager\n");
        return EXIT_FAILURE;
    }

    f87d_devmgr_scan(&g_devmgr, &g_dev_cbs);

    sd_bus *bus = NULL;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "f87d: session bus: %s\n", strerror(-r));
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
        }

        r = sd_bus_wait(bus, 1000000);
        if (r < 0 && r != -EINTR) break;
    }

    printf("f87d: shutting down\n");
    sd_bus_release_name(bus, "org.f87.Control");
    sd_bus_unref(bus);
    f87d_devmgr_destroy(&g_devmgr);
    return EXIT_SUCCESS;
}
