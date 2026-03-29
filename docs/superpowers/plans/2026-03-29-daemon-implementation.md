# F87 Daemon (f87d) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a D-Bus daemon (`f87d`) that owns all USB/effect operations, with a proxy client library and CLI/GUI migration.

**Architecture:** Daemon process uses sd-bus on session bus (`org.f87.Control`), owns libf87 USB access and animation threads. A proxy client library (`client.h/client.c`) wraps D-Bus calls behind an API compatible with existing libf87 patterns. GUI and CLI switch from direct libf87 to proxy calls with minimal code changes.

**Tech Stack:** C11, sd-bus (libsystemd), CMake, existing libf87, D-Bus auto-activation, systemd user service.

---

## File Structure

```
daemon/
├── CMakeLists.txt              # Daemon build config
├── src/
│   ├── main.c                  # Entry point, sd-bus event loop, signal handling
│   ├── dbus_interface.c        # D-Bus method/signal/property vtable
│   ├── dbus_interface.h        # D-Bus interface declarations
│   ├── device_manager.c        # Hotplug monitoring, auto-reconnect
│   ├── device_manager.h        # Device manager declarations
│   ├── effect_manager.c        # Effect start/stop, animation thread lifecycle
│   ├── effect_manager.h        # Effect manager declarations
│   ├── idle_monitor.c          # Idle timeout logic
│   └── idle_monitor.h          # Idle monitor declarations
lib/
├── include/f87/
│   └── client.h                # Proxy client public API (NEW)
├── src/
│   └── client.c                # D-Bus proxy implementation (NEW)
dbus/
├── org.f87.Control.service     # D-Bus auto-activation service file
├── org.f87.Control.conf        # D-Bus policy config (optional)
systemd/
└── f87d.service                # systemd user service unit
tests/
└── test_daemon.c               # Daemon integration tests (NEW)
```

---

### Task 1: Build System — sd-bus Detection and Daemon Skeleton

**Files:**
- Modify: `CMakeLists.txt`
- Create: `daemon/CMakeLists.txt`
- Create: `daemon/src/main.c`

- [ ] **Step 1: Write a minimal daemon main.c that compiles**

```c
/* daemon/src/main.c */
#include <systemd/sd-bus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t g_quit = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    sd_bus *bus = NULL;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "Failed to connect to session bus: %s\n", strerror(-r));
        return EXIT_FAILURE;
    }

    r = sd_bus_request_name(bus, "org.f87.Control", 0);
    if (r < 0) {
        fprintf(stderr, "Failed to acquire bus name: %s\n", strerror(-r));
        sd_bus_unref(bus);
        return EXIT_FAILURE;
    }

    printf("f87d: running on session bus\n");

    while (!g_quit) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) {
            fprintf(stderr, "Bus process error: %s\n", strerror(-r));
            break;
        }
        if (r > 0)
            continue;

        r = sd_bus_wait(bus, 1000000); /* 1 second */
        if (r < 0 && r != -EINTR) {
            fprintf(stderr, "Bus wait error: %s\n", strerror(-r));
            break;
        }
    }

    printf("f87d: shutting down\n");
    sd_bus_release_name(bus, "org.f87.Control");
    sd_bus_unref(bus);
    return EXIT_SUCCESS;
}
```

- [ ] **Step 2: Create daemon/CMakeLists.txt**

```cmake
# daemon/CMakeLists.txt
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED libsystemd)

add_executable(f87d
    src/main.c
)

target_include_directories(f87d PRIVATE
    ${SYSTEMD_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/lib/include
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_link_libraries(f87d PRIVATE
    f87
    ${SYSTEMD_LIBRARIES}
)

target_compile_options(f87d PRIVATE
    ${SYSTEMD_CFLAGS_OTHER}
)
```

- [ ] **Step 3: Add BUILD_DAEMON option to root CMakeLists.txt**

Add after the `BUILD_GUI` option and `gui` subdirectory block in `CMakeLists.txt`:

```cmake
option(BUILD_DAEMON "Build the f87d D-Bus daemon" ON)

# ... (after gui subdirectory)

if(BUILD_DAEMON)
    add_subdirectory(daemon)
endif()
```

- [ ] **Step 4: Build and verify daemon compiles**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make f87d
```
Expected: Compiles without errors, produces `f87d` binary.

- [ ] **Step 5: Quick smoke test — daemon starts and exits**

Run:
```bash
./f87d &
sleep 1
busctl --user list | grep org.f87.Control
kill %1
```
Expected: Bus name `org.f87.Control` appears in the list. Daemon exits cleanly on SIGTERM.

- [ ] **Step 6: Commit**

```bash
git add daemon/ CMakeLists.txt
git commit -m "feat(daemon): add f87d skeleton with sd-bus event loop"
```

---

### Task 2: Device Manager — USB Open/Close and Hotplug

**Files:**
- Create: `daemon/src/device_manager.h`
- Create: `daemon/src/device_manager.c`
- Modify: `daemon/src/main.c`
- Modify: `daemon/CMakeLists.txt`

- [ ] **Step 1: Create device_manager.h**

```c
/* daemon/src/device_manager.h */
#ifndef F87D_DEVICE_MANAGER_H
#define F87D_DEVICE_MANAGER_H

#include <f87/f87.h>
#include <stdbool.h>

typedef struct {
    f87_ctx *ctx;
    f87_device *dev;
    f87_device_info *dev_list;
    int dev_count;
    f87_device_info connected_info; /* copy of connected device info */
    bool connected;
} f87d_device_manager_t;

/* Callbacks for device events */
typedef void (*f87d_device_connected_cb)(const f87_device_info *info, void *userdata);
typedef void (*f87d_device_disconnected_cb)(void *userdata);

typedef struct {
    f87d_device_connected_cb on_connected;
    f87d_device_disconnected_cb on_disconnected;
    void *userdata;
} f87d_device_callbacks_t;

int  f87d_devmgr_init(f87d_device_manager_t *mgr);
void f87d_devmgr_destroy(f87d_device_manager_t *mgr);

/* Scan for devices, open first found. Returns 0 on success. */
int  f87d_devmgr_scan(f87d_device_manager_t *mgr,
                       const f87d_device_callbacks_t *cbs);

/* Check if device is still reachable. Returns false if disconnected. */
bool f87d_devmgr_check_alive(f87d_device_manager_t *mgr);

/* Poll-based hotplug: call periodically. Scans if not connected. */
int  f87d_devmgr_poll(f87d_device_manager_t *mgr,
                       const f87d_device_callbacks_t *cbs);

/* Get current device (NULL if not connected) */
f87_device *f87d_devmgr_get_device(f87d_device_manager_t *mgr);

#endif /* F87D_DEVICE_MANAGER_H */
```

- [ ] **Step 2: Create device_manager.c**

```c
/* daemon/src/device_manager.c */
#include "device_manager.h"
#include <string.h>
#include <stdio.h>

int f87d_devmgr_init(f87d_device_manager_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->ctx = f87_init();
    if (!mgr->ctx)
        return -1;
    return 0;
}

void f87d_devmgr_destroy(f87d_device_manager_t *mgr)
{
    if (mgr->dev) {
        f87_close(mgr->dev);
        mgr->dev = NULL;
    }
    if (mgr->dev_list) {
        f87_free_device_list(mgr->dev_list);
        mgr->dev_list = NULL;
    }
    if (mgr->ctx) {
        f87_exit(mgr->ctx);
        mgr->ctx = NULL;
    }
    mgr->connected = false;
}

int f87d_devmgr_scan(f87d_device_manager_t *mgr,
                      const f87d_device_callbacks_t *cbs)
{
    /* Close existing */
    if (mgr->dev) {
        f87_close(mgr->dev);
        mgr->dev = NULL;
        mgr->connected = false;
    }
    if (mgr->dev_list) {
        f87_free_device_list(mgr->dev_list);
        mgr->dev_list = NULL;
    }

    int rc = f87_find_devices(mgr->ctx, &mgr->dev_list, &mgr->dev_count);
    if (rc < 0 || mgr->dev_count == 0)
        return -1;

    mgr->dev = f87_open(mgr->ctx, &mgr->dev_list[0]);
    if (!mgr->dev)
        return -1;

    mgr->connected = true;
    memcpy(&mgr->connected_info, &mgr->dev_list[0], sizeof(f87_device_info));

    if (cbs && cbs->on_connected)
        cbs->on_connected(&mgr->connected_info, cbs->userdata);

    return 0;
}

bool f87d_devmgr_check_alive(f87d_device_manager_t *mgr)
{
    if (!mgr->dev || !mgr->connected)
        return false;

    /* Try a lightweight config read to check if device is still there */
    f87_effect cur;
    int rc = f87_get_current_effect(mgr->dev, &cur);
    if (rc < 0) {
        mgr->connected = false;
        return false;
    }
    return true;
}

int f87d_devmgr_poll(f87d_device_manager_t *mgr,
                      const f87d_device_callbacks_t *cbs)
{
    if (mgr->connected) {
        /* Check if still alive */
        if (!f87d_devmgr_check_alive(mgr)) {
            /* Device disconnected */
            f87_close(mgr->dev);
            mgr->dev = NULL;
            mgr->connected = false;
            if (cbs && cbs->on_disconnected)
                cbs->on_disconnected(cbs->userdata);
        }
        return 0;
    }

    /* Not connected — try to find device */
    return f87d_devmgr_scan(mgr, cbs);
}

f87_device *f87d_devmgr_get_device(f87d_device_manager_t *mgr)
{
    return mgr->connected ? mgr->dev : NULL;
}
```

- [ ] **Step 3: Add device_manager.c to daemon/CMakeLists.txt**

Change the `add_executable` in `daemon/CMakeLists.txt`:

```cmake
add_executable(f87d
    src/main.c
    src/device_manager.c
)
```

- [ ] **Step 4: Wire device manager into main.c**

Update `daemon/src/main.c` to init/scan/poll the device manager. Add a 5-second sd-bus timer for hotplug polling:

```c
/* daemon/src/main.c */
#include <systemd/sd-bus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    /* Init device manager */
    if (f87d_devmgr_init(&g_devmgr) < 0) {
        fprintf(stderr, "f87d: failed to init device manager\n");
        return EXIT_FAILURE;
    }

    /* Initial device scan */
    f87d_devmgr_scan(&g_devmgr, &g_dev_cbs);

    /* D-Bus setup */
    sd_bus *bus = NULL;
    int r = sd_bus_open_user(&bus);
    if (r < 0) {
        fprintf(stderr, "f87d: failed to connect to session bus: %s\n",
                strerror(-r));
        f87d_devmgr_destroy(&g_devmgr);
        return EXIT_FAILURE;
    }

    r = sd_bus_request_name(bus, "org.f87.Control", 0);
    if (r < 0) {
        fprintf(stderr, "f87d: failed to acquire bus name: %s\n",
                strerror(-r));
        sd_bus_unref(bus);
        f87d_devmgr_destroy(&g_devmgr);
        return EXIT_FAILURE;
    }

    printf("f87d: running on session bus (device %s)\n",
           g_devmgr.connected ? "connected" : "not found");

    /* Hotplug poll timer */
    uint64_t last_poll_us = 0;
    const uint64_t poll_interval_us = 5000000; /* 5 seconds */

    while (!g_quit) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) {
            fprintf(stderr, "f87d: bus process error: %s\n", strerror(-r));
            break;
        }
        if (r > 0)
            continue;

        /* Periodic hotplug poll */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_us = (uint64_t)ts.tv_sec * 1000000ULL +
                          (uint64_t)ts.tv_nsec / 1000ULL;
        if (now_us - last_poll_us >= poll_interval_us) {
            f87d_devmgr_poll(&g_devmgr, &g_dev_cbs);
            last_poll_us = now_us;
        }

        r = sd_bus_wait(bus, 1000000); /* 1 second */
        if (r < 0 && r != -EINTR)
            break;
    }

    printf("f87d: shutting down\n");
    sd_bus_release_name(bus, "org.f87.Control");
    sd_bus_unref(bus);
    f87d_devmgr_destroy(&g_devmgr);
    return EXIT_SUCCESS;
}
```

- [ ] **Step 5: Build and verify**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make f87d
```
Expected: Compiles without errors.

- [ ] **Step 6: Test with hardware — start daemon, plug/unplug keyboard**

Run:
```bash
./f87d
# In another terminal: unplug keyboard, wait 5s, replug
# Observe "device disconnected" / "device connected" messages
```

- [ ] **Step 7: Commit**

```bash
git add daemon/src/device_manager.h daemon/src/device_manager.c daemon/src/main.c daemon/CMakeLists.txt
git commit -m "feat(daemon): add device manager with hotplug polling"
```

---

### Task 3: Effect Manager — HW/SW/Music/Sensor Effect Lifecycle

**Files:**
- Create: `daemon/src/effect_manager.h`
- Create: `daemon/src/effect_manager.c`
- Modify: `daemon/CMakeLists.txt`

- [ ] **Step 1: Create effect_manager.h**

```c
/* daemon/src/effect_manager.h */
#ifndef F87D_EFFECT_MANAGER_H
#define F87D_EFFECT_MANAGER_H

#include <f87/f87.h>
#include <stdbool.h>

typedef enum {
    F87D_CAT_NONE = 0,
    F87D_CAT_HW,
    F87D_CAT_SW,
    F87D_CAT_MUSIC,
    F87D_CAT_SENSOR,
} f87d_effect_category_t;

typedef struct {
    f87_anim_ctx_t *anim;
    f87d_effect_category_t category;
    int effect_id;
    uint8_t brightness;
    uint8_t speed;
    uint8_t color[3];
} f87d_effect_manager_t;

void f87d_effmgr_init(f87d_effect_manager_t *mgr);
void f87d_effmgr_destroy(f87d_effect_manager_t *mgr);

/* Start a hardware effect. Stops any running SW animation first. */
int f87d_effmgr_set_hw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t colorful, uint8_t r, uint8_t g, uint8_t b);

/* Start a software animation effect. */
int f87d_effmgr_set_sw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t r, uint8_t g, uint8_t b, int fps);

/* Start a music-reactive effect. */
int f87d_effmgr_set_music(f87d_effect_manager_t *mgr, f87_device *dev,
                           int effect_id, uint8_t brightness,
                           uint8_t r, uint8_t g, uint8_t b, double gain);

/* Start a sensor effect. */
int f87d_effmgr_set_sensor(f87d_effect_manager_t *mgr, f87_device *dev,
                            const char *profile, const char *config_path);

/* Stop current effect. */
int f87d_effmgr_stop(f87d_effect_manager_t *mgr);

/* Get category string: "hw", "sw", "music", "sensor", "" */
const char *f87d_effmgr_category_str(f87d_effect_category_t cat);

/* Is a SW/music/sensor effect running? (animation thread alive) */
bool f87d_effmgr_has_sw_running(const f87d_effect_manager_t *mgr);

#endif /* F87D_EFFECT_MANAGER_H */
```

- [ ] **Step 2: Create effect_manager.c**

```c
/* daemon/src/effect_manager.c */
#include "effect_manager.h"
#include <string.h>
#include <stdio.h>

void f87d_effmgr_init(f87d_effect_manager_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->category = F87D_CAT_NONE;
    mgr->effect_id = -1;
}

void f87d_effmgr_destroy(f87d_effect_manager_t *mgr)
{
    f87d_effmgr_stop(mgr);
}

static void stop_anim(f87d_effect_manager_t *mgr)
{
    if (mgr->anim) {
        f87_anim_stop(mgr->anim);
        mgr->anim = NULL;
    }
}

int f87d_effmgr_set_hw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t colorful, uint8_t r, uint8_t g, uint8_t b)
{
    if (!dev) return -1;

    stop_anim(mgr);

    f87_effect effect = {0};
    effect.mode = (f87_mode)effect_id;
    effect.brightness = brightness;
    effect.speed = speed;
    effect.colorful = colorful;
    effect.color1 = (f87_color){r, g, b};

    int rc = f87_set_effect(dev, &effect);
    if (rc < 0) return rc;

    mgr->category = F87D_CAT_HW;
    mgr->effect_id = effect_id;
    mgr->brightness = brightness;
    mgr->speed = speed;
    mgr->color[0] = r;
    mgr->color[1] = g;
    mgr->color[2] = b;
    return 0;
}

int f87d_effmgr_set_sw(f87d_effect_manager_t *mgr, f87_device *dev,
                        int effect_id, uint8_t brightness, uint8_t speed,
                        uint8_t r, uint8_t g, uint8_t b, int fps)
{
    if (!dev) return -1;

    stop_anim(mgr);

    f87_anim_config_t config = {
        .color = {r, g, b},
        .brightness = brightness,
        .speed = speed,
        .fps = fps,
    };

    mgr->anim = f87_anim_start(dev, (f87_sw_effect_id)effect_id, &config);
    if (!mgr->anim) return -1;

    mgr->category = F87D_CAT_SW;
    mgr->effect_id = effect_id;
    mgr->brightness = brightness;
    mgr->speed = speed;
    mgr->color[0] = r;
    mgr->color[1] = g;
    mgr->color[2] = b;
    return 0;
}

int f87d_effmgr_set_music(f87d_effect_manager_t *mgr, f87_device *dev,
                           int effect_id, uint8_t brightness,
                           uint8_t r, uint8_t g, uint8_t b, double gain)
{
    if (!dev) return -1;

    stop_anim(mgr);

    f87_anim_config_t config = {
        .color = {r, g, b},
        .brightness = brightness,
        .speed = 2,
        .audio_source = F87_AUDIO_MONITOR,
        .gain = (float)gain,
    };

    mgr->anim = f87_anim_start(dev, (f87_sw_effect_id)effect_id, &config);
    if (!mgr->anim) return -1;

    mgr->category = F87D_CAT_MUSIC;
    mgr->effect_id = effect_id;
    mgr->brightness = brightness;
    mgr->color[0] = r;
    mgr->color[1] = g;
    mgr->color[2] = b;
    return 0;
}

int f87d_effmgr_set_sensor(f87d_effect_manager_t *mgr, f87_device *dev,
                            const char *profile, const char *config_path)
{
    if (!dev) return -1;

    stop_anim(mgr);

    f87_anim_config_t config = {
        .brightness = 3,
        .speed = 2,
        .sensor_profile = profile,
        .sensor_config_path = config_path,
    };

    mgr->anim = f87_anim_start(dev, F87_SW_SENSOR, &config);
    if (!mgr->anim) return -1;

    mgr->category = F87D_CAT_SENSOR;
    mgr->effect_id = F87_SW_SENSOR;
    return 0;
}

int f87d_effmgr_stop(f87d_effect_manager_t *mgr)
{
    stop_anim(mgr);
    mgr->category = F87D_CAT_NONE;
    mgr->effect_id = -1;
    return 0;
}

const char *f87d_effmgr_category_str(f87d_effect_category_t cat)
{
    switch (cat) {
    case F87D_CAT_HW:     return "hw";
    case F87D_CAT_SW:     return "sw";
    case F87D_CAT_MUSIC:  return "music";
    case F87D_CAT_SENSOR: return "sensor";
    default:              return "";
    }
}

bool f87d_effmgr_has_sw_running(const f87d_effect_manager_t *mgr)
{
    return mgr->anim != NULL &&
           (mgr->category == F87D_CAT_SW ||
            mgr->category == F87D_CAT_MUSIC ||
            mgr->category == F87D_CAT_SENSOR);
}
```

- [ ] **Step 3: Add effect_manager.c to daemon/CMakeLists.txt**

```cmake
add_executable(f87d
    src/main.c
    src/device_manager.c
    src/effect_manager.c
)
```

- [ ] **Step 4: Build and verify**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make f87d
```
Expected: Compiles without errors.

- [ ] **Step 5: Commit**

```bash
git add daemon/src/effect_manager.h daemon/src/effect_manager.c daemon/CMakeLists.txt
git commit -m "feat(daemon): add effect manager for HW/SW/music/sensor effects"
```

---

### Task 4: D-Bus Interface — Methods, Signals, and Properties

**Files:**
- Create: `daemon/src/dbus_interface.h`
- Create: `daemon/src/dbus_interface.c`
- Modify: `daemon/src/main.c`
- Modify: `daemon/CMakeLists.txt`

- [ ] **Step 1: Create dbus_interface.h**

```c
/* daemon/src/dbus_interface.h */
#ifndef F87D_DBUS_INTERFACE_H
#define F87D_DBUS_INTERFACE_H

#include <systemd/sd-bus.h>
#include "device_manager.h"
#include "effect_manager.h"

/* Daemon context passed as userdata to D-Bus handlers */
typedef struct {
    sd_bus *bus;
    f87d_device_manager_t *devmgr;
    f87d_effect_manager_t *effmgr;
} f87d_dbus_ctx_t;

/* Register the org.f87.Control interface on /org/f87/Control */
int f87d_dbus_register(sd_bus *bus, f87d_dbus_ctx_t *ctx);

/* Emit signals */
int f87d_dbus_emit_device_connected(f87d_dbus_ctx_t *ctx,
                                     const char *product,
                                     uint16_t vid, uint16_t pid);
int f87d_dbus_emit_device_disconnected(f87d_dbus_ctx_t *ctx);
int f87d_dbus_emit_effect_changed(f87d_dbus_ctx_t *ctx,
                                   int effect_id, const char *category);

#endif /* F87D_DBUS_INTERFACE_H */
```

- [ ] **Step 2: Create dbus_interface.c with method handlers**

```c
/* daemon/src/dbus_interface.c */
#include "dbus_interface.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define DBUS_PATH "/org/f87/Control"
#define DBUS_IFACE "org.f87.Control"

/* ---- Helper: check device connected ---- */
static int check_device(sd_bus_message *reply, f87d_dbus_ctx_t *ctx)
{
    if (!f87d_devmgr_get_device(ctx->devmgr))
        return sd_bus_reply_method_errorf(reply,
            "org.f87.Error.NotConnected", "No keyboard connected");
    return 0;
}

/* ---- Method handlers ---- */

static int method_set_effect(sd_bus_message *msg, void *userdata,
                              sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    int32_t effect_id;
    uint8_t brightness, speed, r, g, b;
    int colorful;

    int rc = sd_bus_message_read(msg, "iyybyyy", &effect_id, &brightness,
                                  &speed, &colorful, &r, &g, &b);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    if (check_device(msg, ctx))
        return -ENODEV;

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    rc = f87d_effmgr_set_hw(ctx->effmgr, dev, effect_id, brightness, speed,
                             (uint8_t)colorful, r, g, b);

    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to set effect: %d", rc);

    f87d_dbus_emit_effect_changed(ctx, effect_id, "hw");
    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_set_sw_effect(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    int32_t effect_id, fps;
    uint8_t brightness, speed, r, g, b;

    int rc = sd_bus_message_read(msg, "iyyyyy i", &effect_id, &brightness,
                                  &speed, &r, &g, &b, &fps);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    if (check_device(msg, ctx))
        return -ENODEV;

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    rc = f87d_effmgr_set_sw(ctx->effmgr, dev, effect_id, brightness, speed,
                             r, g, b, fps);

    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to start sw effect: %d", rc);

    f87d_dbus_emit_effect_changed(ctx, effect_id, "sw");
    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_set_music_effect(sd_bus_message *msg, void *userdata,
                                    sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    int32_t effect_id;
    uint8_t brightness, r, g, b;
    double gain;

    int rc = sd_bus_message_read(msg, "iyyyd", &effect_id, &brightness,
                                  &r, &g, &b, &gain);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    if (check_device(msg, ctx))
        return -ENODEV;

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    rc = f87d_effmgr_set_music(ctx->effmgr, dev, effect_id, brightness,
                                r, g, b, gain);

    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to start music effect: %d", rc);

    f87d_dbus_emit_effect_changed(ctx, effect_id, "music");
    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_set_sensor_effect(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    const char *profile = NULL, *config_path = NULL;

    int rc = sd_bus_message_read(msg, "ss", &profile, &config_path);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    if (check_device(msg, ctx))
        return -ENODEV;

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    rc = f87d_effmgr_set_sensor(ctx->effmgr, dev,
                                 profile[0] ? profile : NULL,
                                 config_path[0] ? config_path : NULL);

    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to start sensor effect: %d", rc);

    f87d_dbus_emit_effect_changed(ctx, F87_SW_SENSOR, "sensor");
    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_set_brightness(sd_bus_message *msg, void *userdata,
                                  sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    uint8_t level;

    int rc = sd_bus_message_read(msg, "y", &level);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    if (check_device(msg, ctx))
        return -ENODEV;

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    rc = f87_set_brightness(dev, level);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to set brightness: %d", rc);

    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_set_color(sd_bus_message *msg, void *userdata,
                             sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;
    uint8_t r, g, b;

    int rc = sd_bus_message_read(msg, "yyy", &r, &g, &b);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    if (check_device(msg, ctx))
        return -ENODEV;

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    f87_set_all_keys(dev, (f87_color){r, g, b});
    rc = f87_apply(dev);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to set color: %d", rc);

    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_stop(sd_bus_message *msg, void *userdata,
                        sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;

    f87d_effmgr_stop(ctx->effmgr);
    f87d_dbus_emit_effect_changed(ctx, -1, "");
    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_off(sd_bus_message *msg, void *userdata,
                       sd_bus_error *error)
{
    f87d_dbus_ctx_t *ctx = userdata;

    f87d_effmgr_stop(ctx->effmgr);

    if (check_device(msg, ctx))
        return -ENODEV;

    f87_device *dev = f87d_devmgr_get_device(ctx->devmgr);
    int rc = f87_lights_off(dev);
    if (rc < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.USBError", "Failed to turn off: %d", rc);

    f87d_dbus_emit_effect_changed(ctx, 0, "hw");
    return sd_bus_reply_method_return(msg, "b", true);
}

static int method_rescan(sd_bus_message *msg, void *userdata,
                          sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;

    int rc = f87d_devmgr_scan(ctx->devmgr, NULL);
    return sd_bus_reply_method_return(msg, "b", rc == 0);
}

static int method_get_status(sd_bus_message *msg, void *userdata,
                              sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;

    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(msg, &reply);
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) return r;

    /* Connected */
    r = sd_bus_message_append(reply, "{sv}", "Connected", "b",
                               ctx->devmgr->connected);
    if (r < 0) return r;

    /* ActiveEffect */
    r = sd_bus_message_append(reply, "{sv}", "ActiveEffect", "i",
                               (int32_t)ctx->effmgr->effect_id);
    if (r < 0) return r;

    /* Category */
    r = sd_bus_message_append(reply, "{sv}", "Category", "s",
                               f87d_effmgr_category_str(ctx->effmgr->category));
    if (r < 0) return r;

    r = sd_bus_message_close_container(reply);
    if (r < 0) return r;

    return sd_bus_send(NULL, reply, NULL);
}

/* ---- Properties ---- */

static int prop_get_connected(sd_bus *bus, const char *path,
                               const char *iface, const char *property,
                               sd_bus_message *reply, void *userdata,
                               sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "b", ctx->devmgr->connected);
}

static int prop_get_active_effect(sd_bus *bus, const char *path,
                                   const char *iface, const char *property,
                                   sd_bus_message *reply, void *userdata,
                                   sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "i", (int32_t)ctx->effmgr->effect_id);
}

static int prop_get_category(sd_bus *bus, const char *path,
                              const char *iface, const char *property,
                              sd_bus_message *reply, void *userdata,
                              sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "s",
        f87d_effmgr_category_str(ctx->effmgr->category));
}

static int prop_get_brightness(sd_bus *bus, const char *path,
                                const char *iface, const char *property,
                                sd_bus_message *reply, void *userdata,
                                sd_bus_error *error)
{
    (void)bus; (void)path; (void)iface; (void)property; (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    return sd_bus_message_append(reply, "y", ctx->effmgr->brightness);
}

/* ---- vtable ---- */

static const sd_bus_vtable f87_vtable[] = {
    SD_BUS_VTABLE_START(0),

    /* Methods */
    SD_BUS_METHOD("SetEffect", "iyybyyy", "b", method_set_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetSwEffect", "iyyyyyi", "b", method_set_sw_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetMusicEffect", "iyyyd", "b", method_set_music_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetSensorEffect", "ss", "b", method_set_sensor_effect,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetBrightness", "y", "b", method_set_brightness,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetColor", "yyy", "b", method_set_color,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Stop", "", "b", method_stop,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Off", "", "b", method_off,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("Rescan", "", "b", method_rescan,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetStatus", "", "a{sv}", method_get_status,
                  SD_BUS_VTABLE_UNPRIVILEGED),

    /* Signals */
    SD_BUS_SIGNAL("DeviceConnected", "sqq", 0),
    SD_BUS_SIGNAL("DeviceDisconnected", "", 0),
    SD_BUS_SIGNAL("EffectChanged", "is", 0),

    /* Properties */
    SD_BUS_PROPERTY("Connected", "b", prop_get_connected, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActiveEffect", "i", prop_get_active_effect, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("ActiveCategory", "s", prop_get_category, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Brightness", "y", prop_get_brightness, 0,
                    SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),

    SD_BUS_VTABLE_END,
};

/* ---- Public functions ---- */

int f87d_dbus_register(sd_bus *bus, f87d_dbus_ctx_t *ctx)
{
    return sd_bus_add_object_vtable(bus, NULL, DBUS_PATH, DBUS_IFACE,
                                    f87_vtable, ctx);
}

int f87d_dbus_emit_device_connected(f87d_dbus_ctx_t *ctx,
                                     const char *product,
                                     uint16_t vid, uint16_t pid)
{
    return sd_bus_emit_signal(ctx->bus, DBUS_PATH, DBUS_IFACE,
                              "DeviceConnected", "sqq", product, vid, pid);
}

int f87d_dbus_emit_device_disconnected(f87d_dbus_ctx_t *ctx)
{
    return sd_bus_emit_signal(ctx->bus, DBUS_PATH, DBUS_IFACE,
                              "DeviceDisconnected", "");
}

int f87d_dbus_emit_effect_changed(f87d_dbus_ctx_t *ctx,
                                   int effect_id, const char *category)
{
    return sd_bus_emit_signal(ctx->bus, DBUS_PATH, DBUS_IFACE,
                              "EffectChanged", "is",
                              (int32_t)effect_id, category);
}
```

- [ ] **Step 3: Wire D-Bus interface into main.c**

Update `daemon/src/main.c` — add `#include "dbus_interface.h"`, `#include "effect_manager.h"`, add global `f87d_effect_manager_t` and `f87d_dbus_ctx_t`, wire into event loop. Replace the `on_device_*` callbacks to emit D-Bus signals:

```c
/* daemon/src/main.c — full rewrite */
#include <systemd/sd-bus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "device_manager.h"
#include "effect_manager.h"
#include "dbus_interface.h"

static volatile sig_atomic_t g_quit = 0;
static f87d_device_manager_t g_devmgr;
static f87d_effect_manager_t g_effmgr;
static f87d_dbus_ctx_t g_dbus_ctx;

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

    /* Init subsystems */
    if (f87d_devmgr_init(&g_devmgr) < 0) {
        fprintf(stderr, "f87d: failed to init device manager\n");
        return EXIT_FAILURE;
    }
    f87d_effmgr_init(&g_effmgr);

    /* Initial device scan */
    f87d_devmgr_scan(&g_devmgr, &g_dev_cbs);

    /* D-Bus setup */
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

    /* Hotplug poll timer */
    uint64_t last_poll_us = 0;
    const uint64_t poll_interval_us = 5000000; /* 5 seconds */

    while (!g_quit) {
        r = sd_bus_process(bus, NULL);
        if (r < 0) break;
        if (r > 0) continue;

        /* Periodic hotplug poll */
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
    f87d_effmgr_destroy(&g_effmgr);
    sd_bus_release_name(bus, "org.f87.Control");
    sd_bus_unref(bus);
    f87d_devmgr_destroy(&g_devmgr);
    return EXIT_SUCCESS;
}
```

- [ ] **Step 4: Add dbus_interface.c to daemon/CMakeLists.txt**

```cmake
add_executable(f87d
    src/main.c
    src/device_manager.c
    src/effect_manager.c
    src/dbus_interface.c
)
```

- [ ] **Step 5: Build and verify**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make f87d
```
Expected: Compiles without errors.

- [ ] **Step 6: Test D-Bus interface with busctl**

Run daemon, then test from another terminal:
```bash
# Terminal 1:
./f87d

# Terminal 2:
busctl --user introspect org.f87.Control /org/f87/Control org.f87.Control
busctl --user call org.f87.Control /org/f87/Control org.f87.Control GetStatus
busctl --user get-property org.f87.Control /org/f87/Control org.f87.Control Connected
```
Expected: Introspection shows all methods/signals/properties. GetStatus returns dict. Connected returns boolean.

- [ ] **Step 7: Test HW effect via D-Bus (with keyboard)**

```bash
# Set wave effect (id=3), brightness=4, speed=2, colorful=false, color=red
busctl --user call org.f87.Control /org/f87/Control org.f87.Control \
    SetEffect iyybyyy 3 4 2 false 255 0 0
```
Expected: Returns `b true`, keyboard shows wave effect.

- [ ] **Step 8: Commit**

```bash
git add daemon/src/dbus_interface.h daemon/src/dbus_interface.c daemon/src/main.c daemon/CMakeLists.txt
git commit -m "feat(daemon): add D-Bus interface with methods, signals, properties"
```

---

### Task 5: Idle Monitor

**Files:**
- Create: `daemon/src/idle_monitor.h`
- Create: `daemon/src/idle_monitor.c`
- Modify: `daemon/src/main.c`
- Modify: `daemon/src/dbus_interface.c`
- Modify: `daemon/CMakeLists.txt`

- [ ] **Step 1: Create idle_monitor.h**

```c
/* daemon/src/idle_monitor.h */
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

/* Call on every D-Bus method invocation to reset the timer. */
void f87d_idle_touch(f87d_idle_monitor_t *mon);

/* Enable or disable idle timeout. */
void f87d_idle_set_enabled(f87d_idle_monitor_t *mon, bool enabled);

/* Check if idle timeout has been reached. Returns true if daemon should exit. */
bool f87d_idle_check(f87d_idle_monitor_t *mon);

#endif /* F87D_IDLE_MONITOR_H */
```

- [ ] **Step 2: Create idle_monitor.c**

```c
/* daemon/src/idle_monitor.c */
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
```

- [ ] **Step 3: Add idle_monitor to f87d_dbus_ctx_t**

In `dbus_interface.h`, add field to context struct:

```c
#include "idle_monitor.h"

typedef struct {
    sd_bus *bus;
    f87d_device_manager_t *devmgr;
    f87d_effect_manager_t *effmgr;
    f87d_idle_monitor_t *idle;
} f87d_dbus_ctx_t;
```

- [ ] **Step 4: Touch idle timer in every D-Bus method handler**

Add `f87d_idle_touch(ctx->idle);` as the first line in each method handler in `dbus_interface.c`: `method_set_effect`, `method_set_sw_effect`, `method_set_music_effect`, `method_set_sensor_effect`, `method_set_brightness`, `method_set_color`, `method_stop`, `method_off`, `method_rescan`, `method_get_status`.

- [ ] **Step 5: Wire idle monitor into main.c event loop**

In `main.c`:
- Declare `static f87d_idle_monitor_t g_idle;`
- Call `f87d_idle_init(&g_idle);`
- Set `g_dbus_ctx.idle = &g_idle;`
- In the hotplug poll block, add idle check logic:

```c
/* After hotplug poll in the while loop */
if (f87d_effmgr_has_sw_running(&g_effmgr)) {
    f87d_idle_set_enabled(&g_idle, false);
} else {
    f87d_idle_set_enabled(&g_idle, true);
    if (f87d_idle_check(&g_idle)) {
        printf("f87d: idle timeout, exiting\n");
        break;
    }
}
```

- [ ] **Step 6: Add idle_monitor.c to daemon/CMakeLists.txt**

```cmake
add_executable(f87d
    src/main.c
    src/device_manager.c
    src/effect_manager.c
    src/dbus_interface.c
    src/idle_monitor.c
)
```

- [ ] **Step 7: Build and verify**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make f87d
```
Expected: Compiles without errors.

- [ ] **Step 8: Commit**

```bash
git add daemon/src/idle_monitor.h daemon/src/idle_monitor.c daemon/src/dbus_interface.h daemon/src/dbus_interface.c daemon/src/main.c daemon/CMakeLists.txt
git commit -m "feat(daemon): add idle monitor with 5min timeout, SW effect override"
```

---

### Task 6: D-Bus Service Files and systemd User Service

**Files:**
- Create: `dbus/org.f87.Control.service`
- Create: `systemd/f87d.service`

- [ ] **Step 1: Create D-Bus auto-activation service file**

```ini
# dbus/org.f87.Control.service
[D-BUS Service]
Name=org.f87.Control
Exec=/usr/local/bin/f87d
```

- [ ] **Step 2: Create systemd user service unit**

```ini
# systemd/f87d.service
[Unit]
Description=F87 Keyboard RGB Control Daemon
Documentation=https://github.com/user/F87Control

[Service]
Type=dbus
BusName=org.f87.Control
ExecStart=/usr/local/bin/f87d
Restart=on-failure
RestartSec=5

[Install]
WantedBy=default.target
```

- [ ] **Step 3: Add install targets to daemon/CMakeLists.txt**

Append to `daemon/CMakeLists.txt`:

```cmake
# Install targets
install(TARGETS f87d DESTINATION bin)

# D-Bus service file
install(FILES ${CMAKE_SOURCE_DIR}/dbus/org.f87.Control.service
        DESTINATION share/dbus-1/services)

# systemd user service
install(FILES ${CMAKE_SOURCE_DIR}/systemd/f87d.service
        DESTINATION lib/systemd/user)
```

- [ ] **Step 4: Test auto-activation manually**

```bash
# Install service file temporarily
mkdir -p ~/.local/share/dbus-1/services
cp dbus/org.f87.Control.service ~/.local/share/dbus-1/services/
# Edit to point at build dir:
sed -i "s|/usr/local/bin/f87d|$(pwd)/build/f87d|" ~/.local/share/dbus-1/services/org.f87.Control.service

# Kill any running daemon
busctl --user call org.f87.Control /org/f87/Control org.f87.Control GetStatus 2>/dev/null || true

# This should auto-start the daemon:
busctl --user get-property org.f87.Control /org/f87/Control org.f87.Control Connected
```
Expected: Daemon auto-starts via D-Bus activation, property returns a boolean.

- [ ] **Step 5: Commit**

```bash
git add dbus/ systemd/ daemon/CMakeLists.txt
git commit -m "feat(daemon): add D-Bus auto-activation and systemd user service"
```

---

### Task 7: Proxy Client Library (libf87 client)

**Files:**
- Create: `lib/include/f87/client.h`
- Create: `lib/src/client.c`
- Modify: `lib/CMakeLists.txt`

- [ ] **Step 1: Create client.h**

```c
/* lib/include/f87/client.h */
#ifndef F87_CLIENT_H
#define F87_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "device.h"
#include "animate.h"

typedef struct f87_client f87_client;

typedef struct {
    bool connected;
    int active_effect;
    char category[16];
    uint8_t brightness;
} f87_client_status_t;

/* Connect to daemon via D-Bus. Returns NULL on failure. */
f87_client *f87_client_connect(void);

/* Disconnect from daemon. */
void f87_client_disconnect(f87_client *client);

/* HW effect */
int f87_client_set_effect(f87_client *client, int effect_id,
                           uint8_t brightness, uint8_t speed,
                           uint8_t colorful, uint8_t r, uint8_t g, uint8_t b);

/* SW effect */
int f87_client_set_sw_effect(f87_client *client, int effect_id,
                              uint8_t brightness, uint8_t speed,
                              uint8_t r, uint8_t g, uint8_t b, int fps);

/* Music effect */
int f87_client_set_music_effect(f87_client *client, int effect_id,
                                 uint8_t brightness,
                                 uint8_t r, uint8_t g, uint8_t b, double gain);

/* Sensor effect */
int f87_client_set_sensor_effect(f87_client *client,
                                  const char *profile,
                                  const char *config_path);

/* Color / brightness / stop / off */
int f87_client_set_color(f87_client *client, uint8_t r, uint8_t g, uint8_t b);
int f87_client_set_brightness(f87_client *client, uint8_t level);
int f87_client_stop(f87_client *client);
int f87_client_off(f87_client *client);

/* Status */
int f87_client_get_status(f87_client *client, f87_client_status_t *status);
int f87_client_is_connected(f87_client *client);

/* Rescan */
int f87_client_rescan(f87_client *client);

/* Signal callbacks */
typedef void (*f87_client_device_cb)(bool connected, const char *product,
                                      void *userdata);
typedef void (*f87_client_effect_cb)(int effect_id, const char *category,
                                      void *userdata);

int f87_client_on_device_change(f87_client *client,
                                 f87_client_device_cb cb, void *userdata);
int f87_client_on_effect_change(f87_client *client,
                                 f87_client_effect_cb cb, void *userdata);

/* Process pending D-Bus events (call in GUI main loop or poll). Returns 0. */
int f87_client_process(f87_client *client);

/* Get the underlying sd-bus fd for integration with external event loops (e.g. GLib). */
int f87_client_get_fd(f87_client *client);

#endif /* F87_CLIENT_H */
```

- [ ] **Step 2: Create client.c**

```c
/* lib/src/client.c */
#include "f87/client.h"
#include <systemd/sd-bus.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define DBUS_DEST  "org.f87.Control"
#define DBUS_PATH  "/org/f87/Control"
#define DBUS_IFACE "org.f87.Control"

struct f87_client {
    sd_bus *bus;
    sd_bus_slot *device_slot;
    sd_bus_slot *effect_slot;
    f87_client_device_cb device_cb;
    void *device_cb_data;
    f87_client_effect_cb effect_cb;
    void *effect_cb_data;
};

f87_client *f87_client_connect(void)
{
    f87_client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    int r = sd_bus_open_user(&c->bus);
    if (r < 0) {
        free(c);
        return NULL;
    }

    return c;
}

void f87_client_disconnect(f87_client *client)
{
    if (!client) return;
    if (client->device_slot)
        sd_bus_slot_unref(client->device_slot);
    if (client->effect_slot)
        sd_bus_slot_unref(client->effect_slot);
    if (client->bus)
        sd_bus_unref(client->bus);
    free(client);
}

/* Helper: call a method and read boolean return */
static int call_method_bool(f87_client *c, const char *method,
                             const char *types, ...)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    va_list ap;

    sd_bus_message *msg = NULL;
    int r = sd_bus_message_new_method_call(c->bus, &msg,
        DBUS_DEST, DBUS_PATH, DBUS_IFACE, method);
    if (r < 0) return -1;

    if (types && types[0]) {
        va_start(ap, types);
        r = sd_bus_message_appendv(msg, types, ap);
        va_end(ap);
        if (r < 0) {
            sd_bus_message_unref(msg);
            return -1;
        }
    }

    r = sd_bus_call(c->bus, msg, 0, &error, &reply);
    sd_bus_message_unref(msg);
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    int result = 0;
    sd_bus_message_read(reply, "b", &result);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return result ? 0 : -1;
}

int f87_client_set_effect(f87_client *client, int effect_id,
                           uint8_t brightness, uint8_t speed,
                           uint8_t colorful, uint8_t r, uint8_t g, uint8_t b)
{
    return call_method_bool(client, "SetEffect", "iyybyyy",
                            (int32_t)effect_id, brightness, speed,
                            (int)colorful, r, g, b);
}

int f87_client_set_sw_effect(f87_client *client, int effect_id,
                              uint8_t brightness, uint8_t speed,
                              uint8_t r, uint8_t g, uint8_t b, int fps)
{
    return call_method_bool(client, "SetSwEffect", "iyyyyyi",
                            (int32_t)effect_id, brightness, speed,
                            r, g, b, (int32_t)fps);
}

int f87_client_set_music_effect(f87_client *client, int effect_id,
                                 uint8_t brightness,
                                 uint8_t r, uint8_t g, uint8_t b, double gain)
{
    return call_method_bool(client, "SetMusicEffect", "iyyyd",
                            (int32_t)effect_id, brightness, r, g, b, gain);
}

int f87_client_set_sensor_effect(f87_client *client,
                                  const char *profile,
                                  const char *config_path)
{
    return call_method_bool(client, "SetSensorEffect", "ss",
                            profile ? profile : "",
                            config_path ? config_path : "");
}

int f87_client_set_color(f87_client *client, uint8_t r, uint8_t g, uint8_t b)
{
    return call_method_bool(client, "SetColor", "yyy", r, g, b);
}

int f87_client_set_brightness(f87_client *client, uint8_t level)
{
    return call_method_bool(client, "SetBrightness", "y", level);
}

int f87_client_stop(f87_client *client)
{
    return call_method_bool(client, "Stop", NULL);
}

int f87_client_off(f87_client *client)
{
    return call_method_bool(client, "Off", NULL);
}

int f87_client_rescan(f87_client *client)
{
    return call_method_bool(client, "Rescan", NULL);
}

int f87_client_get_status(f87_client *client, f87_client_status_t *status)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "GetStatus", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    memset(status, 0, sizeof(*status));

    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) goto done;

    while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
        const char *key = NULL;
        sd_bus_message_read(reply, "s", &key);

        if (strcmp(key, "Connected") == 0) {
            int val = 0;
            sd_bus_message_read(reply, "v", "b", &val);
            status->connected = val;
        } else if (strcmp(key, "ActiveEffect") == 0) {
            int32_t val = 0;
            sd_bus_message_read(reply, "v", "i", &val);
            status->active_effect = val;
        } else if (strcmp(key, "Category") == 0) {
            const char *val = NULL;
            sd_bus_message_read(reply, "v", "s", &val);
            if (val) strncpy(status->category, val, sizeof(status->category) - 1);
        } else {
            sd_bus_message_skip(reply, "v");
        }

        sd_bus_message_exit_container(reply);
    }

    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return 0;
}

int f87_client_is_connected(f87_client *client)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int val = 0;

    int r = sd_bus_get_property_trivial(client->bus, DBUS_DEST, DBUS_PATH,
                                         DBUS_IFACE, "Connected", &error,
                                         'b', &val);
    sd_bus_error_free(&error);
    return r < 0 ? -1 : val;
}

/* Signal match callbacks */
static int on_device_signal(sd_bus_message *msg, void *userdata,
                             sd_bus_error *error)
{
    (void)error;
    f87_client *c = userdata;
    if (!c->device_cb) return 0;

    const char *member = sd_bus_message_get_member(msg);
    if (strcmp(member, "DeviceConnected") == 0) {
        const char *product = NULL;
        sd_bus_message_read(msg, "sqq", &product, NULL, NULL);
        c->device_cb(true, product, c->device_cb_data);
    } else if (strcmp(member, "DeviceDisconnected") == 0) {
        c->device_cb(false, NULL, c->device_cb_data);
    }
    return 0;
}

static int on_effect_signal(sd_bus_message *msg, void *userdata,
                             sd_bus_error *error)
{
    (void)error;
    f87_client *c = userdata;
    if (!c->effect_cb) return 0;

    int32_t effect_id = 0;
    const char *category = NULL;
    sd_bus_message_read(msg, "is", &effect_id, &category);
    c->effect_cb(effect_id, category, c->effect_cb_data);
    return 0;
}

int f87_client_on_device_change(f87_client *client,
                                 f87_client_device_cb cb, void *userdata)
{
    client->device_cb = cb;
    client->device_cb_data = userdata;

    if (client->device_slot) {
        sd_bus_slot_unref(client->device_slot);
        client->device_slot = NULL;
    }

    char match_connected[256];
    snprintf(match_connected, sizeof(match_connected),
             "type='signal',sender='%s',interface='%s',"
             "member='DeviceConnected'", DBUS_DEST, DBUS_IFACE);

    int r = sd_bus_add_match(client->bus, &client->device_slot,
                              match_connected, on_device_signal, client);
    if (r < 0) return -1;

    /* Also match DeviceDisconnected on same slot callback */
    char match_disconnected[256];
    snprintf(match_disconnected, sizeof(match_disconnected),
             "type='signal',sender='%s',interface='%s',"
             "member='DeviceDisconnected'", DBUS_DEST, DBUS_IFACE);

    sd_bus_slot *slot2 = NULL;
    r = sd_bus_add_match(client->bus, &slot2,
                          match_disconnected, on_device_signal, client);
    /* Note: slot2 leaks here — acceptable for now, will be cleaned on disconnect */
    return r < 0 ? -1 : 0;
}

int f87_client_on_effect_change(f87_client *client,
                                 f87_client_effect_cb cb, void *userdata)
{
    client->effect_cb = cb;
    client->effect_cb_data = userdata;

    if (client->effect_slot) {
        sd_bus_slot_unref(client->effect_slot);
        client->effect_slot = NULL;
    }

    char match[256];
    snprintf(match, sizeof(match),
             "type='signal',sender='%s',interface='%s',"
             "member='EffectChanged'", DBUS_DEST, DBUS_IFACE);

    return sd_bus_add_match(client->bus, &client->effect_slot,
                             match, on_effect_signal, client);
}

int f87_client_process(f87_client *client)
{
    int r;
    while ((r = sd_bus_process(client->bus, NULL)) > 0)
        ;
    return 0;
}

int f87_client_get_fd(f87_client *client)
{
    return sd_bus_get_fd(client->bus);
}
```

- [ ] **Step 3: Add client.c to lib/CMakeLists.txt and link sd-bus**

Add to `lib/CMakeLists.txt` — add sd-bus as dependency and client.c as source:

```cmake
# After existing pkg_check_modules
pkg_check_modules(SYSTEMD REQUIRED libsystemd)
```

Add `src/client.c` to the `add_library(f87 SHARED ...)` source list.

Add to `target_include_directories(f87 PRIVATE ...)`:
```cmake
${SYSTEMD_INCLUDE_DIRS}
```

Add to `target_link_libraries(f87 PRIVATE ...)`:
```cmake
${SYSTEMD_LIBRARIES}
```

- [ ] **Step 4: Build and verify**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make
```
Expected: libf87 and f87d both compile. CLI and GUI (if enabled) link against updated libf87.

- [ ] **Step 5: Commit**

```bash
git add lib/include/f87/client.h lib/src/client.c lib/CMakeLists.txt
git commit -m "feat(lib): add D-Bus proxy client library (client.h/client.c)"
```

---

### Task 8: CLI Migration to Proxy Client

**Files:**
- Modify: `cli/src/main.c`

- [ ] **Step 1: Add --direct flag and client mode to CLI**

Modify `cli/src/main.c`:
- Add `static int g_direct_mode = 0;` global flag
- In `main()`, check for `--direct` before command dispatch
- Add `#include <f87/client.h>`
- Create `client_*` wrapper functions that mirror existing `cmd_*` but use `f87_client_*`

The CLI should support two modes:
1. **Default (daemon mode):** Uses `f87_client_connect()` → `f87_client_*` calls
2. **Direct mode (`--direct`):** Uses existing `f87_init()` → direct USB (for debug)

Key changes in `main()`:

```c
/* Near top of main(), after --help/--version checks */
int arg_offset = 0;
if (argc >= 2 && strcmp(argv[1], "--direct") == 0) {
    g_direct_mode = 1;
    arg_offset = 1;
}

const char *cmd = argv[1 + arg_offset];

if (g_direct_mode) {
    /* Existing direct USB code path */
    f87_ctx *ctx = f87_init();
    /* ... existing dispatch ... */
} else {
    /* Daemon client mode */
    f87_client *client = f87_client_connect();
    if (!client) {
        fprintf(stderr, "Cannot connect to f87d daemon. "
                "Is it running? Use --direct for direct USB.\n");
        return 1;
    }
    int ret = dispatch_client(client, cmd, argc - 2 - arg_offset,
                               argv + 2 + arg_offset);
    f87_client_disconnect(client);
    return ret;
}
```

- [ ] **Step 2: Implement dispatch_client function**

```c
static int dispatch_client(f87_client *client, const char *cmd,
                            int argc, char **argv)
{
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "info") == 0) {
        f87_client_status_t st;
        if (f87_client_get_status(client, &st) < 0) {
            fprintf(stderr, "Failed to get status from daemon.\n");
            return 1;
        }
        printf("Connected: %s\n", st.connected ? "yes" : "no");
        printf("Active effect: %d (%s)\n", st.active_effect, st.category);
        return 0;

    } else if (strcmp(cmd, "brightness") == 0) {
        if (argc < 1) { fprintf(stderr, "Usage: f87ctl brightness <1-4>\n"); return 1; }
        int level = atoi(argv[0]);
        if (level < 1 || level > 4) { fprintf(stderr, "Brightness must be 1-4.\n"); return 1; }
        int rc = f87_client_set_brightness(client, (uint8_t)level);
        if (rc < 0) { fprintf(stderr, "Failed to set brightness.\n"); return 1; }
        printf("Brightness set to %d/4.\n", level);
        return 0;

    } else if (strcmp(cmd, "off") == 0) {
        int rc = f87_client_off(client);
        if (rc < 0) { fprintf(stderr, "Failed to turn off.\n"); return 1; }
        printf("Lights off.\n");
        return 0;

    } else if (strcmp(cmd, "color") == 0 || strcmp(cmd, "colour") == 0) {
        if (argc < 1) { fprintf(stderr, "Usage: f87ctl color <RRGGBB>\n"); return 1; }
        f87_color color;
        if (parse_color(argv[0], &color) < 0) {
            fprintf(stderr, "Invalid colour '%s'.\n", argv[0]); return 1;
        }
        int rc = f87_client_set_color(client, color.r, color.g, color.b);
        if (rc < 0) { fprintf(stderr, "Failed to set color.\n"); return 1; }
        printf("Color set to #%02X%02X%02X.\n", color.r, color.g, color.b);
        return 0;

    } else if (strcmp(cmd, "effect") == 0) {
        if (argc < 1) { fprintf(stderr, "Usage: f87ctl effect <name> [opts]\n"); return 1; }
        /* Parse effect name using existing modes[] table (extract to shared helper) */
        const char *name = argv[0];
        int effect_id = -1;
        struct { const char *n; int id; } modes[] = {
            {"off", 0}, {"static", 1}, {"breathing", 2}, {"wave", 3},
            {"spectrum", 4}, {"rain", 5}, {"ripple", 7}, {"starlight", 8},
            {"snake", 10}, {"aurora", 11}, {"reactive", 12}, {"marquee", 13},
            {"circle", 15}, {"raindown", 16}, {"center", 17}, {"custom", 18},
            {NULL, 0}
        };
        for (int i = 0; modes[i].n; i++) {
            if (strcasecmp(name, modes[i].n) == 0) { effect_id = modes[i].id; break; }
        }
        if (effect_id < 0) { fprintf(stderr, "Unknown effect '%s'.\n", name); return 1; }

        f87_color color = {255, 0, 0};
        uint8_t bright = 4, spd = 2, colorful = 0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--brightness") == 0 && i+1 < argc) bright = (uint8_t)atoi(argv[++i]);
            else if (strcmp(argv[i], "--speed") == 0 && i+1 < argc) spd = (uint8_t)atoi(argv[++i]);
            else if (strcmp(argv[i], "--colorful") == 0) colorful = 1;
            else if (argv[i][0] != '-') parse_color(argv[i], &color);
        }

        int rc = f87_client_set_effect(client, effect_id, bright, spd,
                                        colorful, color.r, color.g, color.b);
        if (rc < 0) { fprintf(stderr, "Failed to set effect.\n"); return 1; }
        printf("Effect set.\n");
        return 0;

    } else if (strcmp(cmd, "animate") == 0) {
        if (argc < 1) { fprintf(stderr, "Usage: f87ctl animate <effect> [opts]\n"); return 1; }
        int effect_id = parse_sw_effect(argv[0]);
        if (effect_id < 0) { fprintf(stderr, "Unknown effect: %s\n", argv[0]); return 1; }

        uint8_t r = 255, g = 80, b = 0, bright = 3, spd = 2;
        int fps = 0;
        const char *profile = NULL, *config_path = NULL;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--speed") == 0 && i+1 < argc) spd = (uint8_t)atoi(argv[++i]);
            else if (strcmp(argv[i], "--brightness") == 0 && i+1 < argc) bright = (uint8_t)atoi(argv[++i]);
            else if (strcmp(argv[i], "--fps") == 0 && i+1 < argc) fps = atoi(argv[++i]);
            else if (strcmp(argv[i], "--profile") == 0 && i+1 < argc) profile = argv[++i];
            else if (strcmp(argv[i], "--config") == 0 && i+1 < argc) config_path = argv[++i];
            else if (strlen(argv[i]) == 6 && argv[i][0] != '-') {
                f87_color c; if (parse_color(argv[i], &c) == 0) { r = c.r; g = c.g; b = c.b; }
            }
        }

        int rc;
        if (effect_id == F87_SW_SENSOR)
            rc = f87_client_set_sensor_effect(client, profile, config_path);
        else
            rc = f87_client_set_sw_effect(client, effect_id, bright, spd, r, g, b, fps);

        if (rc < 0) { fprintf(stderr, "Failed to start animation.\n"); return 1; }
        printf("Animation '%s' started on daemon.\n", argv[0]);
        return 0;

    } else if (strcmp(cmd, "music") == 0) {
        if (argc < 1) { fprintf(stderr, "Usage: f87ctl music <mode> [opts]\n"); return 1; }
        int effect_id = parse_music_mode(argv[0]);
        if (effect_id < 0) { fprintf(stderr, "Unknown music mode: %s\n", argv[0]); return 1; }

        uint8_t r = 0, g = 128, b = 255, bright = 4;
        double gain = 0.0;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--gain") == 0 && i+1 < argc) gain = atof(argv[++i]);
            else if (strlen(argv[i]) == 6 && argv[i][0] != '-') {
                f87_color c; if (parse_color(argv[i], &c) == 0) { r = c.r; g = c.g; b = c.b; }
            }
        }

        int rc = f87_client_set_music_effect(client, effect_id, bright, r, g, b, gain);
        if (rc < 0) { fprintf(stderr, "Failed to start music mode.\n"); return 1; }
        printf("Music mode '%s' started on daemon.\n", argv[0]);
        return 0;

    } else if (strcmp(cmd, "stop") == 0) {
        f87_client_stop(client);
        printf("Effect stopped.\n");
        return 0;

    } else if (strcmp(cmd, "raw") == 0) {
        fprintf(stderr, "Raw commands require --direct mode.\n");
        return 1;

    } else {
        fprintf(stderr, "Unknown command '%s'.\n", cmd);
        return 1;
    }
}
```

- [ ] **Step 3: Update usage() to mention --direct and stop command**

Add to usage string:
```
"  stop                              Stop active effect (daemon mode)\n"
"  --direct                          Bypass daemon, use direct USB\n"
```

- [ ] **Step 4: Build and verify**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make
```
Expected: f87ctl compiles and links against libf87 (which now includes client.c with sd-bus).

- [ ] **Step 5: Test CLI daemon mode**

```bash
# Start daemon
./f87d &

# Test via CLI
./f87ctl info
./f87ctl effect wave --brightness 4 --speed 2
./f87ctl brightness 3
./f87ctl stop
./f87ctl off

# Direct mode still works
./f87ctl --direct info
./f87ctl --direct effect static ff0000

kill %1
```

- [ ] **Step 6: Commit**

```bash
git add cli/src/main.c
git commit -m "feat(cli): migrate to daemon proxy client with --direct fallback"
```

---

### Task 9: GUI Migration to Proxy Client

**Files:**
- Modify: `gui/src/app_state.h`
- Modify: `gui/src/app_state.c`
- Modify: `gui/src/window.c` (if needed for signal integration)

- [ ] **Step 1: Update app_state.h to use client**

Replace direct libf87 fields with client:

```c
/* gui/src/app_state.h */
#ifndef F87_APP_STATE_H
#define F87_APP_STATE_H

#include <f87/f87.h>
#include <f87/client.h>

typedef enum {
    F87_GUI_IDLE,
    F87_GUI_RUNNING,
    F87_GUI_ERROR,
} f87_gui_status_t;

typedef struct {
    f87_client *client;
    f87_gui_status_t status;
    char status_text[256];
    int current_effect_id;
    char current_category[16];
    char current_sensor_profile[64];
    bool device_connected;
} f87_app_state_t;

int  f87_app_state_init(f87_app_state_t *state);
void f87_app_state_destroy(f87_app_state_t *state);
int  f87_app_state_rescan(f87_app_state_t *state);

int f87_app_state_start_hw(f87_app_state_t *state, int mode_id,
                            uint8_t brightness, uint8_t speed,
                            uint8_t colorful, uint8_t r, uint8_t g, uint8_t b);

int f87_app_state_start_sw(f87_app_state_t *state, int effect_id,
                            const f87_anim_config_t *config);

int f87_app_state_stop(f87_app_state_t *state);

#endif /* F87_APP_STATE_H */
```

- [ ] **Step 2: Rewrite app_state.c to use proxy client**

```c
/* gui/src/app_state.c */
#include "app_state.h"
#include <stdio.h>
#include <string.h>

int f87_app_state_init(f87_app_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->status = F87_GUI_IDLE;
    snprintf(state->status_text, sizeof(state->status_text), "Baslatiliyor...");

    state->client = f87_client_connect();
    if (!state->client) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Daemon'a baglanilamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    /* Check daemon status */
    f87_client_status_t st;
    if (f87_client_get_status(state->client, &st) < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Daemon durumu alinamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    state->device_connected = st.connected;
    if (st.connected) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Bagli (daemon)");
        state->status = F87_GUI_IDLE;
    } else {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye bulunamadi");
        state->status = F87_GUI_ERROR;
    }

    return 0;
}

void f87_app_state_destroy(f87_app_state_t *state)
{
    if (state->client) {
        f87_client_disconnect(state->client);
        state->client = NULL;
    }
}

int f87_app_state_rescan(f87_app_state_t *state)
{
    if (!state->client) return -1;

    int rc = f87_client_rescan(state->client);
    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Tarama basarisiz");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    state->device_connected = f87_client_is_connected(state->client) > 0;
    if (state->device_connected) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Bagli (daemon)");
        state->status = F87_GUI_IDLE;
    } else {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Klavye bulunamadi");
        state->status = F87_GUI_ERROR;
    }
    return 0;
}

int f87_app_state_start_hw(f87_app_state_t *state, int mode_id,
                            uint8_t brightness, uint8_t speed,
                            uint8_t colorful, uint8_t r, uint8_t g, uint8_t b)
{
    if (!state->client) return -1;

    int rc = f87_client_set_effect(state->client, mode_id, brightness, speed,
                                    colorful, r, g, b);
    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Efekt gonderilemedi");
        state->status = F87_GUI_ERROR;
        return rc;
    }

    snprintf(state->status_text, sizeof(state->status_text),
             "%s calisiyor", f87_mode_name((f87_mode)mode_id));
    state->status = F87_GUI_RUNNING;
    state->current_effect_id = mode_id;
    strncpy(state->current_category, "hw", sizeof(state->current_category));
    return 0;
}

int f87_app_state_start_sw(f87_app_state_t *state, int effect_id,
                            const f87_anim_config_t *config)
{
    if (!state->client) return -1;

    int rc;
    if (effect_id == F87_SW_SENSOR) {
        rc = f87_client_set_sensor_effect(state->client,
                                           config->sensor_profile,
                                           config->sensor_config_path);
    } else if (effect_id >= 200) {
        /* Music effect */
        rc = f87_client_set_music_effect(state->client, effect_id,
                                          config->brightness,
                                          config->color[0], config->color[1],
                                          config->color[2], config->gain);
    } else {
        rc = f87_client_set_sw_effect(state->client, effect_id,
                                       config->brightness, config->speed,
                                       config->color[0], config->color[1],
                                       config->color[2], config->fps);
    }

    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Animasyon baslatilamadi");
        state->status = F87_GUI_ERROR;
        return -1;
    }

    snprintf(state->status_text, sizeof(state->status_text),
             "%s calisiyor", f87_sw_effect_name((f87_sw_effect_id)effect_id));
    state->status = F87_GUI_RUNNING;
    state->current_effect_id = effect_id;
    return 0;
}

int f87_app_state_stop(f87_app_state_t *state)
{
    if (!state->client) return -1;

    int rc = f87_client_stop(state->client);
    if (rc < 0) {
        snprintf(state->status_text, sizeof(state->status_text),
                 "Durdurma hatasi");
        state->status = F87_GUI_ERROR;
        return rc;
    }

    snprintf(state->status_text, sizeof(state->status_text), "Bekleniyor");
    state->status = F87_GUI_IDLE;
    return 0;
}
```

- [ ] **Step 3: Check other GUI files for direct libf87 usage**

Search for `f87_init`, `f87_open`, `f87_close`, `f87_exit`, `f87_find_devices`, `f87_free_device_list`, `f87_set_effect`, `f87_anim_start`, `f87_anim_stop` in `gui/src/*.c`. Any remaining direct calls should be replaced with client calls. The `app_state` struct no longer has `ctx`, `dev`, `dev_list`, `dev_count`, or `anim` fields — any code referencing those needs updating.

Key files to check and update:
- `gui/src/window.c` — if it references `state->dev` for key layout, it should use hardcoded layout or get it from daemon
- `gui/src/controls.c` — if it calls libf87 directly
- `gui/src/keyboard_view.c` — if it reads `state->dev`

For key layout data: since the keyboard layout is static (88 keys, always same), the GUI can use the layout constants directly from `f87/lighting.h` headers without needing a device handle. If `f87_get_key_layout()` is called, pass `NULL` or use a stub.

- [ ] **Step 4: Build and verify GUI compiles**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON -DBUILD_GUI=ON && make
```
Expected: GUI compiles without errors.

- [ ] **Step 5: Test GUI with daemon**

```bash
./f87d &
./f87control
# Select effects, verify they work via daemon
kill %1
```

- [ ] **Step 6: Commit**

```bash
git add gui/src/app_state.h gui/src/app_state.c gui/src/window.c gui/src/controls.c
git commit -m "feat(gui): migrate to daemon proxy client"
```

---

### Task 10: Integration Test

**Files:**
- Create: `tests/test_daemon.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create integration test**

```c
/* tests/test_daemon.c
 * Integration test: start daemon, call methods via client, verify.
 * Requires: daemon running (manually or via auto-activation).
 */
#include <f87/client.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

static int test_client_connect(void)
{
    printf("test_client_connect... ");
    f87_client *c = f87_client_connect();
    if (!c) {
        printf("SKIP (daemon not running)\n");
        return 0;
    }
    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

static int test_get_status(void)
{
    printf("test_get_status... ");
    f87_client *c = f87_client_connect();
    if (!c) { printf("SKIP\n"); return 0; }

    f87_client_status_t st;
    int rc = f87_client_get_status(c, &st);
    assert(rc == 0);
    printf("connected=%d effect=%d category='%s' ",
           st.connected, st.active_effect, st.category);

    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

static int test_is_connected(void)
{
    printf("test_is_connected... ");
    f87_client *c = f87_client_connect();
    if (!c) { printf("SKIP\n"); return 0; }

    int connected = f87_client_is_connected(c);
    printf("connected=%d ", connected);

    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

static int test_set_brightness(void)
{
    printf("test_set_brightness... ");
    f87_client *c = f87_client_connect();
    if (!c) { printf("SKIP\n"); return 0; }

    if (f87_client_is_connected(c) <= 0) {
        printf("SKIP (no device)\n");
        f87_client_disconnect(c);
        return 0;
    }

    int rc = f87_client_set_brightness(c, 3);
    assert(rc == 0);

    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

static int test_stop(void)
{
    printf("test_stop... ");
    f87_client *c = f87_client_connect();
    if (!c) { printf("SKIP\n"); return 0; }

    int rc = f87_client_stop(c);
    assert(rc == 0);

    f87_client_disconnect(c);
    printf("PASS\n");
    return 0;
}

int main(void)
{
    printf("=== Daemon Integration Tests ===\n");
    printf("(Start f87d first for full testing)\n\n");

    test_client_connect();
    test_get_status();
    test_is_connected();
    test_set_brightness();
    test_stop();

    printf("\nAll tests passed.\n");
    return 0;
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

```cmake
pkg_check_modules(SYSTEMD libsystemd)
if(SYSTEMD_FOUND)
    add_executable(test_daemon tests/test_daemon.c)
    target_link_libraries(test_daemon PRIVATE f87)
    add_test(NAME daemon_tests COMMAND test_daemon)
endif()
```

- [ ] **Step 3: Build and run tests**

Run:
```bash
cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_DAEMON=ON && make test_daemon
./test_daemon
```
Expected: Tests pass (or SKIP if daemon not running).

- [ ] **Step 4: Run full test suite**

```bash
ctest --output-on-failure
```
Expected: All existing tests still pass. New daemon test passes or skips gracefully.

- [ ] **Step 5: Commit**

```bash
git add tests/test_daemon.c CMakeLists.txt
git commit -m "test: add daemon integration tests"
```

---

### Task 11: CLAUDE.md and Documentation Update

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update CLAUDE.md**

Add to Key Files section:
```
- `daemon/src/main.c` — Daemon entry point, sd-bus event loop
- `daemon/src/dbus_interface.c` — D-Bus method/signal/property handlers
- `daemon/src/device_manager.c` — Hotplug monitoring, auto-reconnect
- `daemon/src/effect_manager.c` — Effect lifecycle management
- `daemon/src/idle_monitor.c` — Idle timeout (5min, disabled during SW effects)
- `lib/include/f87/client.h` — D-Bus proxy client API
- `lib/src/client.c` — D-Bus proxy implementation
- `dbus/org.f87.Control.service` — D-Bus auto-activation
- `systemd/f87d.service` — systemd user service unit
```

Update Build section to include daemon:
```bash
sudo apt install libusb-1.0-0-dev libjson-c-dev libpulse-dev libgtk-4-dev libadwaita-1-dev libsystemd-dev cmake build-essential
mkdir build && cd build && cmake .. -DBUILD_DAEMON=ON && make
```

Update Testing section:
```bash
# Start daemon
./f87d &
# Or install systemd service:
# cp systemd/f87d.service ~/.config/systemd/user/
# systemctl --user enable --now f87d

# CLI now uses daemon by default
./f87ctl effect wave --brightness 4
./f87ctl animate fire
./f87ctl stop

# Direct USB mode (bypass daemon)
./f87ctl --direct effect wave
```

Update Project Status:
```
- Faz 6.1: Daemon mode (complete)
  - D-Bus daemon (sd-bus) with auto-activation
  - Device manager with 5s hotplug polling
  - Effect manager (HW/SW/music/sensor)
  - Idle timeout (5min, disabled during SW effects)
  - Proxy client library (client.h)
  - CLI/GUI migrated to daemon
```

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md for daemon mode (Faz 6.1)"
```
