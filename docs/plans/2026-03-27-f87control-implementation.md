# F87Control Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a Linux application to control Aula F87 Pro keyboard RGB lighting via reverse-engineered USB HID protocol.

**Architecture:** Three-layer design — libf87 (shared C library abstracting USB protocol), f87ctl (CLI tool), f87control (GTK4 GUI). Each layer depends only on the one below it.

**Tech Stack:** C11, libusb-1.0, GTK4, json-c, lm-sensors, CMake, Python (RE tools)

**Special Note:** This project has a hardware dependency. Protocol constants (VID/PID, command bytes, packet structure) will be discovered through reverse engineering. Tasks use placeholder values marked `0x????` until real values are captured. The user handles Windows-side data capture; the agent writes all code.

---

### Task 1: Project Scaffolding

**Files:**
- Create: `CMakeLists.txt`
- Create: `lib/CMakeLists.txt`
- Create: `cli/CMakeLists.txt`
- Create: `gui/CMakeLists.txt`
- Create: `.gitignore`

**Step 1: Create root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(F87Control VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_CLI "Build f87ctl CLI tool" ON)
option(BUILD_GUI "Build f87control GUI" OFF)

add_subdirectory(lib)

if(BUILD_CLI)
    add_subdirectory(cli)
endif()

if(BUILD_GUI)
    add_subdirectory(gui)
endif()
```

**Step 2: Create lib/CMakeLists.txt**

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

add_library(f87 SHARED
    src/device.c
    src/protocol.c
    src/lighting.c
    src/effects.c
)

target_include_directories(f87
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${LIBUSB_INCLUDE_DIRS}
)

target_link_libraries(f87 PRIVATE ${LIBUSB_LIBRARIES})

set_target_properties(f87 PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION 0
    PUBLIC_HEADER "include/f87/f87.h;include/f87/device.h;include/f87/lighting.h;include/f87/effects.h"
)

install(TARGETS f87
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include/f87
)
```

**Step 3: Create cli/CMakeLists.txt**

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)

add_executable(f87ctl src/main.c)

target_include_directories(f87ctl PRIVATE ${JSONC_INCLUDE_DIRS})
target_link_libraries(f87ctl PRIVATE f87 ${JSONC_LIBRARIES})

install(TARGETS f87ctl RUNTIME DESTINATION bin)
```

**Step 4: Create gui/CMakeLists.txt (placeholder)**

```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)
pkg_check_modules(JSONC REQUIRED json-c)

add_executable(f87control
    src/main.c
    src/app.c
    src/keyboard_widget.c
    src/effects_panel.c
)

target_include_directories(f87control PRIVATE ${GTK4_INCLUDE_DIRS} ${JSONC_INCLUDE_DIRS})
target_link_libraries(f87control PRIVATE f87 ${GTK4_LIBRARIES} ${JSONC_LIBRARIES})

install(TARGETS f87control RUNTIME DESTINATION bin)
```

**Step 5: Create .gitignore**

```
build/
*.o
*.so
*.so.*
compile_commands.json
.cache/
```

**Step 6: Create directory structure with stub files**

Create empty stub `.c` files so CMake doesn't fail:
- `lib/src/device.c`
- `lib/src/protocol.c`
- `lib/src/lighting.c`
- `lib/src/effects.c`
- `cli/src/main.c`

**Step 7: Verify build scaffolding compiles**

```bash
mkdir build && cd build
cmake .. -DBUILD_GUI=OFF
make
```

Expected: Compiles with no errors (empty library, empty CLI binary).

**Step 8: Commit**

```bash
git init
git add -A
git commit -m "chore: project scaffolding — CMake build, directory structure"
```

---

### Task 2: Public Header Files

**Files:**
- Create: `lib/include/f87/f87.h`
- Create: `lib/include/f87/device.h`
- Create: `lib/include/f87/lighting.h`
- Create: `lib/include/f87/effects.h`

**Step 1: Create f87.h (umbrella header)**

```c
#ifndef F87_H
#define F87_H

#include "device.h"
#include "lighting.h"
#include "effects.h"

#define F87_VERSION_MAJOR 0
#define F87_VERSION_MINOR 1
#define F87_VERSION_PATCH 0

const char *f87_version_string(void);

#endif /* F87_H */
```

**Step 2: Create device.h**

```c
#ifndef F87_DEVICE_H
#define F87_DEVICE_H

#include <stdint.h>

typedef struct f87_ctx f87_ctx;
typedef struct f87_device f87_device;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    char serial[64];
    char manufacturer[64];
    char product[128];
    uint8_t bus;
    uint8_t address;
} f87_device_info;

f87_ctx *f87_init(void);
void f87_exit(f87_ctx *ctx);

int f87_find_devices(f87_ctx *ctx, f87_device_info **list, int *count);
void f87_free_device_list(f87_device_info *list);

f87_device *f87_open(f87_ctx *ctx, const f87_device_info *info);
void f87_close(f87_device *dev);

const char *f87_get_firmware_version(f87_device *dev);
int f87_get_battery_level(f87_device *dev);

const char *f87_strerror(int error);

#endif /* F87_DEVICE_H */
```

**Step 3: Create lighting.h**

```c
#ifndef F87_LIGHTING_H
#define F87_LIGHTING_H

#include "device.h"
#include <stdint.h>

typedef struct {
    uint8_t r, g, b;
} f87_color;

#define F87_COLOR(r, g, b) ((f87_color){(r), (g), (b)})
#define F87_COLOR_OFF       F87_COLOR(0, 0, 0)
#define F87_COLOR_WHITE     F87_COLOR(255, 255, 255)
#define F87_COLOR_RED       F87_COLOR(255, 0, 0)
#define F87_COLOR_GREEN     F87_COLOR(0, 255, 0)
#define F87_COLOR_BLUE      F87_COLOR(0, 0, 255)

typedef struct {
    uint8_t key_id;
    const char *name;
    uint8_t row;
    uint8_t col;
} f87_key_info;

int f87_set_brightness(f87_device *dev, uint8_t level);
int f87_get_brightness(f87_device *dev, uint8_t *level);
int f87_lights_off(f87_device *dev);

int f87_set_key_color(f87_device *dev, uint8_t key_id, f87_color color);
int f87_set_all_keys(f87_device *dev, f87_color color);
int f87_set_key_map(f87_device *dev, const f87_color *colors, int count);
int f87_apply(f87_device *dev);

int f87_get_key_count(f87_device *dev);
const f87_key_info *f87_get_key_layout(f87_device *dev);

#endif /* F87_LIGHTING_H */
```

**Step 4: Create effects.h**

```c
#ifndef F87_EFFECTS_H
#define F87_EFFECTS_H

#include "device.h"
#include "lighting.h"
#include <stdint.h>

typedef enum {
    F87_EFFECT_STATIC = 0,
    F87_EFFECT_BREATHING,
    F87_EFFECT_WAVE,
    F87_EFFECT_RAINBOW,
    F87_EFFECT_RIPPLE,
    F87_EFFECT_REACTIVE,
    F87_EFFECT_COUNT
} f87_effect_type;

typedef enum {
    F87_DIR_RIGHT = 0,
    F87_DIR_LEFT,
    F87_DIR_UP,
    F87_DIR_DOWN
} f87_direction;

typedef struct {
    f87_effect_type type;
    uint8_t speed;          /* 0-10 */
    uint8_t brightness;     /* 0-100 */
    f87_color color1;
    f87_color color2;
    f87_direction direction;
} f87_effect;

int f87_set_effect(f87_device *dev, const f87_effect *effect);
int f87_get_current_effect(f87_device *dev, f87_effect *effect);
int f87_get_supported_effects(f87_device *dev, f87_effect_type **list, int *count);

const char *f87_effect_name(f87_effect_type type);

#endif /* F87_EFFECTS_H */
```

**Step 5: Verify headers compile**

Update `lib/src/device.c` to include headers and add version stub:
```c
#include "f87/f87.h"
const char *f87_version_string(void) { return "0.1.0"; }
```

```bash
cd build && cmake .. && make
```

Expected: Compiles cleanly.

**Step 6: Commit**

```bash
git add lib/include/
git commit -m "feat: add libf87 public API headers"
```

---

### Task 3: Internal Protocol Header

**Files:**
- Create: `lib/src/protocol.h`

**Step 1: Create protocol.h**

```c
#ifndef F87_PROTOCOL_H
#define F87_PROTOCOL_H

#include "f87/device.h"
#include "f87/lighting.h"
#include "f87/effects.h"
#include <stdint.h>

/* USB identifiers — to be filled from RE */
#define F87_VENDOR_ID    0x258A  /* Placeholder — common for Aula/Redragon */
#define F87_PRODUCT_ID   0x0049  /* Placeholder — to be confirmed */

#define F87_IFACE_NUM    1       /* HID interface for lighting control */
#define F87_EP_OUT       0x02    /* OUT endpoint */
#define F87_EP_IN        0x82    /* IN endpoint */

#define F87_PKT_SIZE     64      /* HID report size */
#define F87_TIMEOUT_MS   1000

/* Command bytes — to be filled from RE */
#define F87_CMD_EFFECT      0x04
#define F87_CMD_BRIGHTNESS  0x05
#define F87_CMD_PER_KEY     0x06
#define F87_CMD_QUERY       0x07

typedef struct {
    uint8_t data[F87_PKT_SIZE];
} f87_packet;

/* Internal device structure */
struct f87_ctx {
    void *usb_ctx;   /* libusb_context* */
};

struct f87_device {
    f87_ctx *ctx;
    void *usb_handle;        /* libusb_device_handle* */
    f87_device_info info;
    char firmware_ver[32];
    int iface_claimed;

    /* Per-key color buffer for batch apply */
    f87_color key_colors[128];
    int key_dirty[128];
    int num_keys;
};

/* Packet building */
void f87_pkt_init(f87_packet *pkt);
int  f87_pkt_build_effect(f87_packet *pkt, const f87_effect *effect);
int  f87_pkt_build_brightness(f87_packet *pkt, uint8_t level);
int  f87_pkt_build_per_key(f87_packet *pkt, uint8_t key_id, f87_color color);
int  f87_pkt_build_per_key_batch(f87_packet *pkt, const f87_color *colors,
                                  const int *dirty, int offset, int count);

/* Packet send/recv */
int  f87_pkt_send(f87_device *dev, const f87_packet *pkt);
int  f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms);

/* Key layout data (87-key TKL) */
#define F87_KEY_COUNT 87
extern const f87_key_info f87_key_layout[F87_KEY_COUNT];

#endif /* F87_PROTOCOL_H */
```

**Step 2: Verify it compiles**

```bash
cd build && cmake .. && make
```

**Step 3: Commit**

```bash
git add lib/src/protocol.h
git commit -m "feat: add internal protocol header with placeholder constants"
```

---

### Task 4: Device Management (device.c)

**Files:**
- Modify: `lib/src/device.c`

**Step 1: Implement device.c**

Full implementation with libusb: `f87_init`, `f87_exit`, `f87_find_devices`, `f87_free_device_list`, `f87_open`, `f87_close`, `f87_strerror`.

Key behaviors:
- `f87_init` creates libusb context
- `f87_find_devices` enumerates USB, filters by VID/PID, populates `f87_device_info` list
- `f87_open` opens device handle, detaches kernel driver if needed, claims HID interface
- `f87_close` releases interface, reattaches kernel driver, closes handle
- `f87_strerror` maps negative error codes to strings

```c
#include "f87/f87.h"
#include "protocol.h"
#include <libusb-1.0/libusb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Error codes */
#define F87_OK              0
#define F87_ERR_INIT       -1
#define F87_ERR_NOT_FOUND  -2
#define F87_ERR_OPEN       -3
#define F87_ERR_CLAIM      -4
#define F87_ERR_IO         -5
#define F87_ERR_NOMEM      -6

const char *f87_version_string(void)
{
    return "0.1.0";
}

f87_ctx *f87_init(void)
{
    f87_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;

    if (libusb_init((libusb_context **)&ctx->usb_ctx) != 0) {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void f87_exit(f87_ctx *ctx)
{
    if (!ctx)
        return;
    if (ctx->usb_ctx)
        libusb_exit(ctx->usb_ctx);
    free(ctx);
}

int f87_find_devices(f87_ctx *ctx, f87_device_info **list, int *count)
{
    libusb_device **devs;
    ssize_t n = libusb_get_device_list(ctx->usb_ctx, &devs);
    if (n < 0)
        return F87_ERR_INIT;

    *list = NULL;
    *count = 0;

    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devs[i], &desc) != 0)
            continue;

        if (desc.idVendor == F87_VENDOR_ID &&
            desc.idProduct == F87_PRODUCT_ID) {

            f87_device_info *tmp = realloc(*list,
                (*count + 1) * sizeof(f87_device_info));
            if (!tmp) {
                free(*list);
                libusb_free_device_list(devs, 1);
                return F87_ERR_NOMEM;
            }
            *list = tmp;

            f87_device_info *info = &(*list)[*count];
            memset(info, 0, sizeof(*info));
            info->vendor_id = desc.idVendor;
            info->product_id = desc.idProduct;
            info->bus = libusb_get_bus_number(devs[i]);
            info->address = libusb_get_device_address(devs[i]);

            libusb_device_handle *h;
            if (libusb_open(devs[i], &h) == 0) {
                libusb_get_string_descriptor_ascii(h, desc.iManufacturer,
                    (unsigned char *)info->manufacturer, sizeof(info->manufacturer));
                libusb_get_string_descriptor_ascii(h, desc.iProduct,
                    (unsigned char *)info->product, sizeof(info->product));
                libusb_get_string_descriptor_ascii(h, desc.iSerialNumber,
                    (unsigned char *)info->serial, sizeof(info->serial));
                libusb_close(h);
            }

            (*count)++;
        }
    }

    libusb_free_device_list(devs, 1);
    return (*count > 0) ? F87_OK : F87_ERR_NOT_FOUND;
}

void f87_free_device_list(f87_device_info *list)
{
    free(list);
}

f87_device *f87_open(f87_ctx *ctx, const f87_device_info *info)
{
    libusb_device **devs;
    ssize_t n = libusb_get_device_list(ctx->usb_ctx, &devs);
    if (n < 0)
        return NULL;

    f87_device *dev = NULL;

    for (ssize_t i = 0; i < n; i++) {
        if (libusb_get_bus_number(devs[i]) == info->bus &&
            libusb_get_device_address(devs[i]) == info->address) {

            dev = calloc(1, sizeof(*dev));
            if (!dev)
                break;

            dev->ctx = ctx;
            memcpy(&dev->info, info, sizeof(*info));
            dev->num_keys = F87_KEY_COUNT;

            libusb_device_handle *h;
            int rc = libusb_open(devs[i], &h);
            if (rc != 0) {
                free(dev);
                dev = NULL;
                break;
            }
            dev->usb_handle = h;

            if (libusb_kernel_driver_active(h, F87_IFACE_NUM) == 1)
                libusb_detach_kernel_driver(h, F87_IFACE_NUM);

            rc = libusb_claim_interface(h, F87_IFACE_NUM);
            if (rc != 0) {
                libusb_close(h);
                free(dev);
                dev = NULL;
                break;
            }
            dev->iface_claimed = 1;
            break;
        }
    }

    libusb_free_device_list(devs, 1);
    return dev;
}

void f87_close(f87_device *dev)
{
    if (!dev)
        return;

    libusb_device_handle *h = dev->usb_handle;
    if (h) {
        if (dev->iface_claimed)
            libusb_release_interface(h, F87_IFACE_NUM);
        libusb_attach_kernel_driver(h, F87_IFACE_NUM);
        libusb_close(h);
    }
    free(dev);
}

const char *f87_get_firmware_version(f87_device *dev)
{
    /* TODO: query device for firmware version via protocol */
    return "unknown";
}

int f87_get_battery_level(f87_device *dev)
{
    /* TODO: query device for battery level via protocol */
    return -1;
}

const char *f87_strerror(int error)
{
    switch (error) {
    case F87_OK:            return "Success";
    case F87_ERR_INIT:      return "Initialization failed";
    case F87_ERR_NOT_FOUND: return "Device not found";
    case F87_ERR_OPEN:      return "Could not open device";
    case F87_ERR_CLAIM:     return "Could not claim interface";
    case F87_ERR_IO:        return "I/O error";
    case F87_ERR_NOMEM:     return "Out of memory";
    default:                return "Unknown error";
    }
}
```

**Step 2: Verify it compiles**

```bash
cd build && cmake .. && make
```

**Step 3: Commit**

```bash
git add lib/src/device.c
git commit -m "feat: implement device management — init, find, open, close"
```

---

### Task 5: Protocol Layer (protocol.c)

**Files:**
- Modify: `lib/src/protocol.c`

**Step 1: Implement protocol.c**

Packet building and send/recv over USB interrupt transfers. Key layout table for 87-key TKL.

All command bytes are placeholders until RE confirms them.

```c
#include "protocol.h"
#include <libusb-1.0/libusb.h>
#include <string.h>
#include <stdio.h>

void f87_pkt_init(f87_packet *pkt)
{
    memset(pkt, 0, sizeof(*pkt));
}

int f87_pkt_build_brightness(f87_packet *pkt, uint8_t level)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_CMD_BRIGHTNESS;
    pkt->data[1] = level;
    return 0;
}

int f87_pkt_build_effect(f87_packet *pkt, const f87_effect *effect)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_CMD_EFFECT;
    pkt->data[1] = (uint8_t)effect->type;
    pkt->data[2] = effect->speed;
    pkt->data[3] = effect->brightness;
    pkt->data[4] = effect->color1.r;
    pkt->data[5] = effect->color1.g;
    pkt->data[6] = effect->color1.b;
    pkt->data[7] = effect->color2.r;
    pkt->data[8] = effect->color2.g;
    pkt->data[9] = effect->color2.b;
    pkt->data[10] = (uint8_t)effect->direction;
    return 0;
}

int f87_pkt_build_per_key(f87_packet *pkt, uint8_t key_id, f87_color color)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_CMD_PER_KEY;
    pkt->data[1] = 1;  /* single key mode */
    pkt->data[2] = key_id;
    pkt->data[3] = color.r;
    pkt->data[4] = color.g;
    pkt->data[5] = color.b;
    return 0;
}

int f87_pkt_build_per_key_batch(f87_packet *pkt, const f87_color *colors,
                                 const int *dirty, int offset, int count)
{
    f87_pkt_init(pkt);
    pkt->data[0] = F87_CMD_PER_KEY;
    pkt->data[1] = 0;  /* batch mode */
    pkt->data[2] = (uint8_t)offset;
    /* Pack up to 20 RGB values per packet: (64 - 3) / 3 = 20 */
    int packed = 0;
    for (int i = 0; i < count && packed < 20; i++) {
        int idx = offset + i;
        if (!dirty[idx])
            continue;
        pkt->data[3 + packed * 3]     = colors[idx].r;
        pkt->data[3 + packed * 3 + 1] = colors[idx].g;
        pkt->data[3 + packed * 3 + 2] = colors[idx].b;
        packed++;
    }
    return packed;
}

int f87_pkt_send(f87_device *dev, const f87_packet *pkt)
{
    int transferred = 0;
    int rc = libusb_interrupt_transfer(
        dev->usb_handle,
        F87_EP_OUT,
        (unsigned char *)pkt->data,
        F87_PKT_SIZE,
        &transferred,
        F87_TIMEOUT_MS
    );
    if (rc != 0)
        return -1;
    return transferred;
}

int f87_pkt_recv(f87_device *dev, f87_packet *pkt, int timeout_ms)
{
    int transferred = 0;
    f87_pkt_init(pkt);
    int rc = libusb_interrupt_transfer(
        dev->usb_handle,
        F87_EP_IN,
        (unsigned char *)pkt->data,
        F87_PKT_SIZE,
        &transferred,
        timeout_ms > 0 ? timeout_ms : F87_TIMEOUT_MS
    );
    if (rc != 0)
        return -1;
    return transferred;
}

/* 87-key TKL layout — physical positions */
const f87_key_info f87_key_layout[F87_KEY_COUNT] = {
    /* Row 0: Function row */
    { 0,  "ESC",   0, 0 },
    { 1,  "F1",    0, 2 },  { 2,  "F2",    0, 3 },
    { 3,  "F3",    0, 4 },  { 4,  "F4",    0, 5 },
    { 5,  "F5",    0, 7 },  { 6,  "F6",    0, 8 },
    { 7,  "F7",    0, 9 },  { 8,  "F8",    0, 10 },
    { 9,  "F9",    0, 12 }, { 10, "F10",   0, 13 },
    { 11, "F11",   0, 14 }, { 12, "F12",   0, 15 },
    { 13, "PRTSC", 0, 17 }, { 14, "SCRLK", 0, 18 },
    { 15, "PAUSE", 0, 19 },
    /* Row 1: Number row */
    { 16, "GRAVE", 1, 0 },
    { 17, "1",     1, 1 },  { 18, "2",     1, 2 },
    { 19, "3",     1, 3 },  { 20, "4",     1, 4 },
    { 21, "5",     1, 5 },  { 22, "6",     1, 6 },
    { 23, "7",     1, 7 },  { 24, "8",     1, 8 },
    { 25, "9",     1, 9 },  { 26, "0",     1, 10 },
    { 27, "MINUS", 1, 11 }, { 28, "EQUAL", 1, 12 },
    { 29, "BKSP",  1, 13 },
    { 30, "INS",   1, 17 }, { 31, "HOME",  1, 18 },
    { 32, "PGUP",  1, 19 },
    /* Row 2: QWERTY */
    { 33, "TAB",   2, 0 },
    { 34, "Q",     2, 1 },  { 35, "W",     2, 2 },
    { 36, "E",     2, 3 },  { 37, "R",     2, 4 },
    { 38, "T",     2, 5 },  { 39, "Y",     2, 6 },
    { 40, "U",     2, 7 },  { 41, "I",     2, 8 },
    { 42, "O",     2, 9 },  { 43, "P",     2, 10 },
    { 44, "LBRKT", 2, 11 }, { 45, "RBRKT", 2, 12 },
    { 46, "BSLSH", 2, 13 },
    { 47, "DEL",   2, 17 }, { 48, "END",   2, 18 },
    { 49, "PGDN",  2, 19 },
    /* Row 3: Home row */
    { 50, "CAPS",  3, 0 },
    { 51, "A",     3, 1 },  { 52, "S",     3, 2 },
    { 53, "D",     3, 3 },  { 54, "F",     3, 4 },
    { 55, "G",     3, 5 },  { 56, "H",     3, 6 },
    { 57, "J",     3, 7 },  { 58, "K",     3, 8 },
    { 59, "L",     3, 9 },  { 60, "SEMI",  3, 10 },
    { 61, "QUOTE", 3, 11 }, { 62, "ENTER", 3, 13 },
    /* Row 4: Shift row */
    { 63, "LSHFT", 4, 0 },
    { 64, "Z",     4, 1 },  { 65, "X",     4, 2 },
    { 66, "C",     4, 3 },  { 67, "V",     4, 4 },
    { 68, "B",     4, 5 },  { 69, "N",     4, 6 },
    { 70, "M",     4, 7 },  { 71, "COMMA", 4, 8 },
    { 72, "DOT",   4, 9 },  { 73, "SLASH", 4, 10 },
    { 74, "RSHFT", 4, 13 },
    { 75, "UP",    4, 18 },
    /* Row 5: Bottom row */
    { 76, "LCTRL", 5, 0 },  { 77, "LWIN",  5, 1 },
    { 78, "LALT",  5, 2 },  { 79, "SPACE", 5, 6 },
    { 80, "RALT",  5, 10 }, { 81, "FN",    5, 11 },
    { 82, "MENU",  5, 12 }, { 83, "RCTRL", 5, 13 },
    { 84, "LEFT",  5, 17 }, { 85, "DOWN",  5, 18 },
    { 86, "RIGHT", 5, 19 },
};
```

**Step 2: Verify it compiles**

```bash
cd build && cmake .. && make
```

**Step 3: Commit**

```bash
git add lib/src/protocol.c
git commit -m "feat: implement protocol layer — packet build, send/recv, key layout"
```

---

### Task 6: Lighting & Effects Implementation

**Files:**
- Modify: `lib/src/lighting.c`
- Modify: `lib/src/effects.c`

**Step 1: Implement lighting.c**

```c
#include "f87/lighting.h"
#include "protocol.h"
#include <string.h>

int f87_set_brightness(f87_device *dev, uint8_t level)
{
    if (level > 100) level = 100;
    f87_packet pkt;
    f87_pkt_build_brightness(&pkt, level);
    return f87_pkt_send(dev, &pkt) > 0 ? 0 : -1;
}

int f87_get_brightness(f87_device *dev, uint8_t *level)
{
    /* TODO: implement query protocol */
    (void)dev; (void)level;
    return -1;
}

int f87_lights_off(f87_device *dev)
{
    return f87_set_brightness(dev, 0);
}

int f87_set_key_color(f87_device *dev, uint8_t key_id, f87_color color)
{
    if (key_id >= dev->num_keys)
        return -1;
    dev->key_colors[key_id] = color;
    dev->key_dirty[key_id] = 1;
    return 0;
}

int f87_set_all_keys(f87_device *dev, f87_color color)
{
    for (int i = 0; i < dev->num_keys; i++) {
        dev->key_colors[i] = color;
        dev->key_dirty[i] = 1;
    }
    return 0;
}

int f87_set_key_map(f87_device *dev, const f87_color *colors, int count)
{
    if (count > dev->num_keys)
        count = dev->num_keys;
    for (int i = 0; i < count; i++) {
        dev->key_colors[i] = colors[i];
        dev->key_dirty[i] = 1;
    }
    return 0;
}

int f87_apply(f87_device *dev)
{
    f87_packet pkt;
    /* Send dirty keys in batches */
    for (int offset = 0; offset < dev->num_keys; offset += 20) {
        int remaining = dev->num_keys - offset;
        if (remaining > 20) remaining = 20;

        int packed = f87_pkt_build_per_key_batch(&pkt,
            dev->key_colors, dev->key_dirty, offset, remaining);
        if (packed > 0) {
            if (f87_pkt_send(dev, &pkt) <= 0)
                return -1;
        }
    }

    /* Clear dirty flags */
    memset(dev->key_dirty, 0, sizeof(dev->key_dirty));
    return 0;
}

int f87_get_key_count(f87_device *dev)
{
    return dev->num_keys;
}

const f87_key_info *f87_get_key_layout(f87_device *dev)
{
    (void)dev;
    return f87_key_layout;
}
```

**Step 2: Implement effects.c**

```c
#include "f87/effects.h"
#include "protocol.h"

static const char *effect_names[] = {
    [F87_EFFECT_STATIC]    = "static",
    [F87_EFFECT_BREATHING] = "breathing",
    [F87_EFFECT_WAVE]      = "wave",
    [F87_EFFECT_RAINBOW]   = "rainbow",
    [F87_EFFECT_RIPPLE]    = "ripple",
    [F87_EFFECT_REACTIVE]  = "reactive",
};

int f87_set_effect(f87_device *dev, const f87_effect *effect)
{
    f87_packet pkt;
    f87_pkt_build_effect(&pkt, effect);
    return f87_pkt_send(dev, &pkt) > 0 ? 0 : -1;
}

int f87_get_current_effect(f87_device *dev, f87_effect *effect)
{
    /* TODO: implement query protocol */
    (void)dev; (void)effect;
    return -1;
}

int f87_get_supported_effects(f87_device *dev, f87_effect_type **list, int *count)
{
    (void)dev;
    static f87_effect_type all[] = {
        F87_EFFECT_STATIC, F87_EFFECT_BREATHING, F87_EFFECT_WAVE,
        F87_EFFECT_RAINBOW, F87_EFFECT_RIPPLE, F87_EFFECT_REACTIVE,
    };
    *list = all;
    *count = F87_EFFECT_COUNT;
    return 0;
}

const char *f87_effect_name(f87_effect_type type)
{
    if (type >= F87_EFFECT_COUNT)
        return "unknown";
    return effect_names[type];
}
```

**Step 3: Verify it compiles**

```bash
cd build && cmake .. && make
```

**Step 4: Commit**

```bash
git add lib/src/lighting.c lib/src/effects.c
git commit -m "feat: implement lighting control and effects API"
```

---

### Task 7: CLI Tool (f87ctl)

**Files:**
- Modify: `cli/src/main.c`

**Step 1: Implement f87ctl**

Full CLI with subcommands: `info`, `list`, `brightness`, `off`, `effect`, `key`, `raw`, `dump`.

Uses getopt for argument parsing. Connects to first available device.

```c
#include <f87/f87.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static f87_device *open_first_device(f87_ctx *ctx)
{
    f87_device_info *list;
    int count;
    if (f87_find_devices(ctx, &list, &count) != 0) {
        fprintf(stderr, "No Aula F87 device found.\n");
        return NULL;
    }
    f87_device *dev = f87_open(ctx, &list[0]);
    f87_free_device_list(list);
    if (!dev)
        fprintf(stderr, "Could not open device.\n");
    return dev;
}

static int parse_color(const char *hex, f87_color *c)
{
    unsigned int val;
    if (sscanf(hex, "%06x", &val) != 1)
        return -1;
    c->r = (val >> 16) & 0xFF;
    c->g = (val >> 8) & 0xFF;
    c->b = val & 0xFF;
    return 0;
}

static int cmd_list(f87_ctx *ctx)
{
    f87_device_info *list;
    int count;
    if (f87_find_devices(ctx, &list, &count) != 0) {
        printf("No devices found.\n");
        return 1;
    }
    for (int i = 0; i < count; i++)
        printf("[%d] %s %s (bus %d, addr %d)\n",
            i, list[i].manufacturer, list[i].product,
            list[i].bus, list[i].address);
    f87_free_device_list(list);
    return 0;
}

static int cmd_info(f87_ctx *ctx)
{
    f87_device *dev = open_first_device(ctx);
    if (!dev) return 1;
    printf("Product:  %s\n", dev->info.product);
    printf("Vendor:   %s\n", dev->info.manufacturer);
    printf("Serial:   %s\n", dev->info.serial);
    printf("Firmware: %s\n", f87_get_firmware_version(dev));
    printf("Keys:     %d\n", f87_get_key_count(dev));
    f87_close(dev);
    return 0;
}

static int cmd_brightness(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl brightness <0-100>\n");
        return 1;
    }
    int level = atoi(argv[0]);
    f87_device *dev = open_first_device(ctx);
    if (!dev) return 1;
    int rc = f87_set_brightness(dev, (uint8_t)level);
    f87_close(dev);
    return rc == 0 ? 0 : 1;
}

static int cmd_off(f87_ctx *ctx)
{
    f87_device *dev = open_first_device(ctx);
    if (!dev) return 1;
    int rc = f87_lights_off(dev);
    f87_close(dev);
    return rc == 0 ? 0 : 1;
}

static int cmd_effect(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl effect <name> [--speed N] [--color RRGGBB] [--direction left|right|up|down]\n");
        return 1;
    }

    f87_effect eff = {0};
    eff.speed = 5;
    eff.brightness = 100;
    eff.color1 = F87_COLOR_RED;

    /* Match effect name */
    for (int t = 0; t < F87_EFFECT_COUNT; t++) {
        if (strcasecmp(argv[0], f87_effect_name(t)) == 0) {
            eff.type = t;
            break;
        }
    }

    /* Parse optional args */
    for (int i = 1; i < argc - 1; i += 2) {
        if (strcmp(argv[i], "--speed") == 0)
            eff.speed = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--color") == 0)
            parse_color(argv[i + 1], &eff.color1);
        else if (strcmp(argv[i], "--direction") == 0) {
            if (strcmp(argv[i + 1], "left") == 0)  eff.direction = F87_DIR_LEFT;
            if (strcmp(argv[i + 1], "right") == 0) eff.direction = F87_DIR_RIGHT;
            if (strcmp(argv[i + 1], "up") == 0)    eff.direction = F87_DIR_UP;
            if (strcmp(argv[i + 1], "down") == 0)  eff.direction = F87_DIR_DOWN;
        }
    }

    f87_device *dev = open_first_device(ctx);
    if (!dev) return 1;
    int rc = f87_set_effect(dev, &eff);
    f87_close(dev);
    return rc == 0 ? 0 : 1;
}

static int cmd_key(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: f87ctl key set <KEY> <RRGGBB>\n");
        fprintf(stderr, "       f87ctl key set-all <RRGGBB>\n");
        return 1;
    }

    f87_device *dev = open_first_device(ctx);
    if (!dev) return 1;

    int rc = 1;
    if (strcmp(argv[0], "set-all") == 0 && argc >= 2) {
        f87_color c;
        if (parse_color(argv[1], &c) == 0) {
            f87_set_all_keys(dev, c);
            rc = f87_apply(dev);
        }
    } else if (strcmp(argv[0], "set") == 0 && argc >= 3) {
        f87_color c;
        if (parse_color(argv[2], &c) == 0) {
            /* Find key by name */
            const f87_key_info *layout = f87_get_key_layout(dev);
            int nkeys = f87_get_key_count(dev);
            for (int i = 0; i < nkeys; i++) {
                if (strcasecmp(layout[i].name, argv[1]) == 0) {
                    f87_set_key_color(dev, layout[i].key_id, c);
                    rc = f87_apply(dev);
                    break;
                }
            }
        }
    }

    f87_close(dev);
    return rc == 0 ? 0 : 1;
}

static int cmd_raw_send(f87_ctx *ctx, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl raw send \"XX XX XX ...\"\n");
        return 1;
    }

    uint8_t data[64] = {0};
    int len = 0;
    char *tok = strtok(argv[0], " ");
    while (tok && len < 64) {
        unsigned int val;
        if (sscanf(tok, "%02x", &val) == 1)
            data[len++] = (uint8_t)val;
        tok = strtok(NULL, " ");
    }

    f87_device *dev = open_first_device(ctx);
    if (!dev) return 1;

    f87_packet pkt;
    memcpy(pkt.data, data, 64);
    int sent = f87_pkt_send(dev, &pkt);
    printf("Sent %d bytes\n", sent);

    /* Try to read response */
    f87_packet resp;
    int recv = f87_pkt_recv(dev, &resp, 500);
    if (recv > 0) {
        printf("Response (%d bytes):\n", recv);
        for (int i = 0; i < 64; i++) {
            printf("%02x ", resp.data[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
    }

    f87_close(dev);
    return 0;
}

static int cmd_raw_listen(f87_ctx *ctx)
{
    f87_device *dev = open_first_device(ctx);
    if (!dev) return 1;

    printf("Listening for packets (Ctrl+C to stop)...\n");
    f87_packet pkt;
    while (1) {
        int recv = f87_pkt_recv(dev, &pkt, 5000);
        if (recv > 0) {
            for (int i = 0; i < 64; i++) {
                printf("%02x ", pkt.data[i]);
                if ((i + 1) % 16 == 0) printf("\n");
            }
            printf("---\n");
        }
    }

    f87_close(dev);
    return 0;
}

static void usage(void)
{
    printf("f87ctl — Aula F87 Pro keyboard control\n\n");
    printf("Usage: f87ctl <command> [args...]\n\n");
    printf("Commands:\n");
    printf("  list                        List connected devices\n");
    printf("  info                        Device information\n");
    printf("  brightness <0-100>          Set brightness\n");
    printf("  off                         Turn off lighting\n");
    printf("  effect <name> [options]     Set lighting effect\n");
    printf("  key set <KEY> <RRGGBB>      Set single key color\n");
    printf("  key set-all <RRGGBB>        Set all keys color\n");
    printf("  raw send \"XX XX ...\"        Send raw HID packet\n");
    printf("  raw listen                  Listen for HID packets\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage();
        return 1;
    }

    f87_ctx *ctx = f87_init();
    if (!ctx) {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }

    int rc = 1;
    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0)
        rc = cmd_list(ctx);
    else if (strcmp(cmd, "info") == 0)
        rc = cmd_info(ctx);
    else if (strcmp(cmd, "brightness") == 0)
        rc = cmd_brightness(ctx, argc - 2, argv + 2);
    else if (strcmp(cmd, "off") == 0)
        rc = cmd_off(ctx);
    else if (strcmp(cmd, "effect") == 0)
        rc = cmd_effect(ctx, argc - 2, argv + 2);
    else if (strcmp(cmd, "key") == 0)
        rc = cmd_key(ctx, argc - 2, argv + 2);
    else if (strcmp(cmd, "raw") == 0 && argc >= 3) {
        if (strcmp(argv[2], "send") == 0)
            rc = cmd_raw_send(ctx, argc - 3, argv + 3);
        else if (strcmp(argv[2], "listen") == 0)
            rc = cmd_raw_listen(ctx);
    }
    else
        usage();

    f87_exit(ctx);
    return rc;
}
```

**Step 2: Verify it compiles**

```bash
cd build && cmake .. && make
```

**Step 3: Commit**

```bash
git add cli/src/main.c
git commit -m "feat: implement f87ctl CLI — list, info, brightness, effect, key, raw commands"
```

---

### Task 8: udev Rules

**Files:**
- Create: `udev/99-f87.rules`

**Step 1: Create udev rule**

```
# Aula F87 Pro keyboard — allow non-root USB access
SUBSYSTEM=="usb", ATTR{idVendor}=="258a", ATTR{idProduct}=="0049", MODE="0666", TAG+="uaccess"
SUBSYSTEM=="hidraw", ATTRS{idVendor}=="258a", ATTRS{idProduct}=="0049", MODE="0666", TAG+="uaccess"
```

VID/PID are placeholders — update after RE confirms.

**Step 2: Commit**

```bash
git add udev/
git commit -m "feat: add udev rules for non-root keyboard access"
```

---

### Task 9: RE Tool — pcap_parser.py

**Files:**
- Create: `tools/pcap_parser.py`
- Create: `tools/protocol_notes.md`

**Step 1: Create pcap_parser.py**

Python script that reads pcap files (from Wireshark USB captures), filters by device VID/PID, extracts HID packets, and displays them in a structured format. Supports diff mode to compare two captures.

```python
#!/usr/bin/env python3
"""
Aula F87 Pro USB HID packet analyzer.

Usage:
    python pcap_parser.py <capture.pcap>
    python pcap_parser.py diff <a.pcap> <b.pcap>
    python pcap_parser.py --vid 258a --pid 0049 <capture.pcap>
"""
import sys
import struct
from pathlib import Path

try:
    import dpkt
except ImportError:
    print("Install dpkt: pip install dpkt")
    sys.exit(1)


def read_pcap_usb(filepath, vid=None, pid=None):
    """Read USB packets from pcap file."""
    packets = []
    with open(filepath, 'rb') as f:
        try:
            pcap = dpkt.pcap.Reader(f)
        except ValueError:
            pcap = dpkt.pcapng.Reader(f)

        for ts, buf in pcap:
            if len(buf) < 64:
                continue
            # USBPcap header parsing
            header_len = struct.unpack_from('<H', buf, 0)[0]
            if len(buf) <= header_len:
                continue

            direction = buf[header_len - 1] if header_len > 0 else 0
            payload = buf[header_len:]

            if len(payload) >= 1:
                packets.append({
                    'timestamp': ts,
                    'direction': 'OUT' if direction == 0 else 'IN',
                    'data': payload[:64],
                    'length': len(payload)
                })
    return packets


def hexdump(data, prefix=""):
    """Format bytes as hex dump."""
    lines = []
    for i in range(0, len(data), 16):
        hex_part = ' '.join(f'{b:02x}' for b in data[i:i+16])
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
        lines.append(f"{prefix}{i:04x}: {hex_part:<48s}  {ascii_part}")
    return '\n'.join(lines)


def analyze(filepath, vid=None, pid=None):
    """Analyze single pcap file."""
    packets = read_pcap_usb(filepath, vid, pid)
    print(f"File: {filepath}")
    print(f"Total packets: {len(packets)}")
    print(f"OUT packets: {sum(1 for p in packets if p['direction'] == 'OUT')}")
    print(f"IN packets:  {sum(1 for p in packets if p['direction'] == 'IN')}")
    print("=" * 70)

    for i, pkt in enumerate(packets):
        print(f"\n[{i:04d}] {pkt['direction']} ({pkt['length']} bytes) t={pkt['timestamp']:.6f}")
        print(hexdump(pkt['data'], "  "))


def diff(file_a, file_b):
    """Compare two captures, highlight differences."""
    pkts_a = read_pcap_usb(file_a)
    pkts_b = read_pcap_usb(file_b)

    out_a = [p for p in pkts_a if p['direction'] == 'OUT']
    out_b = [p for p in pkts_b if p['direction'] == 'OUT']

    print(f"Comparing OUT packets:")
    print(f"  A ({file_a}): {len(out_a)} packets")
    print(f"  B ({file_b}): {len(out_b)} packets")
    print("=" * 70)

    count = min(len(out_a), len(out_b))
    for i in range(count):
        a_data = out_a[i]['data']
        b_data = out_b[i]['data']
        if a_data != b_data:
            print(f"\n[{i:04d}] DIFFERS:")
            diffs = []
            for j in range(min(len(a_data), len(b_data))):
                if a_data[j] != b_data[j]:
                    diffs.append(f"  byte[{j:2d}]: A=0x{a_data[j]:02x}  B=0x{b_data[j]:02x}")
            print('\n'.join(diffs))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    if sys.argv[1] == 'diff' and len(sys.argv) >= 4:
        diff(sys.argv[2], sys.argv[3])
    else:
        vid = pid = None
        args = sys.argv[1:]
        filepath = args[-1]
        for i, a in enumerate(args[:-1]):
            if a == '--vid' and i + 1 < len(args):
                vid = int(args[i + 1], 16)
            if a == '--pid' and i + 1 < len(args):
                pid = int(args[i + 1], 16)
        analyze(filepath, vid, pid)
```

**Step 2: Create protocol_notes.md**

```markdown
# Aula F87 Pro — Protocol Notes

## USB Identifiers

- VID: 0x???? (to be confirmed)
- PID: 0x???? (to be confirmed)
- Interface: ? (HID interface used for lighting)
- Endpoints: OUT=0x??, IN=0x??

## Packet Structure

Packet size: 64 bytes (to be confirmed)

```
Byte 0: Report ID / Command
Byte 1: Sub-command
Byte 2-63: Payload (TBD)
```

## Captured Commands

### Brightness
<!-- Fill after RE -->

### Effects
<!-- Fill after RE -->

### Per-Key RGB
<!-- Fill after RE -->

## Capture Log

| Date | File | Action | Notes |
|------|------|--------|-------|
<!-- Add captures here -->
```

**Step 3: Commit**

```bash
git add tools/
git commit -m "feat: add RE tools — pcap parser and protocol notes template"
```

---

### Task 10: First Integration Test — Device Detection

**Files:**
- Create: `tests/test_device.c`
- Modify: `CMakeLists.txt` (add tests)

**Step 1: Create test_device.c**

A simple integration test that initializes libf87 and attempts to find devices. Works without hardware (just verifies init/exit don't crash).

```c
#include <f87/f87.h>
#include <stdio.h>
#include <assert.h>

static int test_init_exit(void)
{
    printf("test_init_exit... ");
    f87_ctx *ctx = f87_init();
    assert(ctx != NULL);
    f87_exit(ctx);
    printf("PASS\n");
    return 0;
}

static int test_find_no_device(void)
{
    printf("test_find_no_device... ");
    f87_ctx *ctx = f87_init();
    f87_device_info *list = NULL;
    int count = 0;
    /* This will return NOT_FOUND if no keyboard is connected — that's OK */
    int rc = f87_find_devices(ctx, &list, &count);
    printf("rc=%d count=%d %s\n", rc, count,
        count > 0 ? "DEVICE FOUND!" : "no device (expected)");
    if (list) f87_free_device_list(list);
    f87_exit(ctx);
    printf("PASS\n");
    return 0;
}

static int test_version(void)
{
    printf("test_version... ");
    const char *v = f87_version_string();
    assert(v != NULL);
    printf("%s PASS\n", v);
    return 0;
}

static int test_strerror(void)
{
    printf("test_strerror... ");
    assert(f87_strerror(0) != NULL);
    assert(f87_strerror(-1) != NULL);
    assert(f87_strerror(-99) != NULL);
    printf("PASS\n");
    return 0;
}

int main(void)
{
    printf("=== libf87 device tests ===\n");
    test_init_exit();
    test_version();
    test_strerror();
    test_find_no_device();
    printf("=== All tests passed ===\n");
    return 0;
}
```

**Step 2: Add test target to CMakeLists.txt**

Add to root `CMakeLists.txt`:
```cmake
enable_testing()
add_executable(test_device tests/test_device.c)
target_link_libraries(test_device PRIVATE f87)
add_test(NAME device_tests COMMAND test_device)
```

**Step 3: Build and run tests**

```bash
cd build && cmake .. && make && ctest --output-on-failure
```

Expected: All 4 tests PASS.

**Step 4: Commit**

```bash
git add tests/ CMakeLists.txt
git commit -m "test: add device detection integration tests"
```

---

### Task 11: RE Session 1 — VID/PID Discovery (Requires User)

> **STOP POINT:** This task requires the user to capture USB data on Windows.

**User actions:**

1. Open Device Manager on Windows
2. Find "Aula F87 Pro" under "Human Interface Devices" or "Keyboards"
3. Properties → Details → Hardware IDs
4. Note VID and PID (format: `VID_XXXX&PID_XXXX`)

Alternatively:
```powershell
# PowerShell
Get-PnpDevice -Class HIDClass | Where-Object {$_.FriendlyName -like "*Aula*"} | Get-PnpDeviceProperty -KeyName DEVPKEY_Device_HardwareIds
```

Or install USBDeview / check Wireshark USB device list.

**Agent actions after user provides VID/PID:**

1. Update `lib/src/protocol.h`: `F87_VENDOR_ID` and `F87_PRODUCT_ID`
2. Update `udev/99-f87.rules` with real VID/PID
3. Rebuild and test with `f87ctl list`
4. Commit

---

### Task 12: RE Session 2 — Basic Protocol Capture (Requires User)

> **STOP POINT:** User captures USB traffic.

**User actions:**

1. Install Wireshark + USBPcap on Windows
2. Start capture on the USB bus where keyboard is connected
3. Open Aula driver software
4. Perform these actions, saving separate pcap for each:
   - `capture_brightness_0.pcap` — set brightness to 0
   - `capture_brightness_50.pcap` — set brightness to 50
   - `capture_brightness_100.pcap` — set brightness to 100
   - `capture_static_red.pcap` — static red effect
   - `capture_static_green.pcap` — static green effect
   - `capture_static_blue.pcap` — static blue effect
   - `capture_rainbow.pcap` — rainbow effect
   - `capture_breathing.pcap` — breathing effect

**Agent actions after user provides pcap files:**

1. Run `python tools/pcap_parser.py diff capture_static_red.pcap capture_static_green.pcap`
2. Analyze byte-level differences to identify command structure
3. Update `protocol.h` with real command bytes
4. Update `protocol.c` packet builders
5. Update `tools/protocol_notes.md`
6. Test with `f87ctl raw send` and `f87ctl effect`
7. Commit

---

### Task 13: RE Session 3 — Per-Key and Advanced (Requires User)

> **STOP POINT:** More captures needed.

**User actions:**

1. Capture per-key color changes (single key, multiple keys)
2. Capture all available effects with various speeds
3. Capture any sensor/reactive mode activation

**Agent actions:**

1. Analyze per-key packet structure (batch format, key ID mapping)
2. Confirm key ID mapping matches our layout table
3. Update protocol layer with real per-key commands
4. Full CLI testing
5. Commit

---

### Task 14: GTK GUI — Application Shell

**Files:**
- Create: `gui/src/main.c`
- Create: `gui/src/app.c`
- Create: `gui/src/app.h`

**Step 1: Create app.h**

```c
#ifndef F87_APP_H
#define F87_APP_H

#include <gtk/gtk.h>
#include <f87/f87.h>

#define F87_TYPE_APP (f87_app_get_type())
G_DECLARE_FINAL_TYPE(F87App, f87_app, F87, APP, GtkApplication)

F87App *f87_app_new(void);

#endif /* F87_APP_H */
```

**Step 2: Create app.c**

GtkApplication subclass with main window, header bar, device selector, status bar.

**Step 3: Create main.c**

```c
#include "app.h"

int main(int argc, char **argv)
{
    F87App *app = f87_app_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
```

**Step 4: Verify it compiles and launches**

```bash
cd build && cmake .. -DBUILD_GUI=ON && make
./f87control
```

**Step 5: Commit**

```bash
git add gui/
git commit -m "feat: add GTK4 application shell with main window"
```

---

### Task 15: GTK GUI — Keyboard Widget

**Files:**
- Create: `gui/src/keyboard_widget.c`
- Create: `gui/src/keyboard_widget.h`

Implement GtkDrawingArea-based keyboard visualization using Cairo. Each key drawn as rounded rectangle with current RGB color. Click to select, shift+click for multi-select, drag for region select. Emits `key-selected` signal.

**Commit:**
```bash
git commit -m "feat: add interactive keyboard layout widget with Cairo rendering"
```

---

### Task 16: GTK GUI — Effects Panel

**Files:**
- Create: `gui/src/effects_panel.c`
- Create: `gui/src/effects_panel.h`

Effect type dropdown, speed/brightness sliders, color pickers, direction radio buttons, Apply/Reset buttons.

**Commit:**
```bash
git commit -m "feat: add effects control panel with sliders and color pickers"
```

---

### Task 17: GTK GUI — Per-Key Panel

Color palette, selected key(s) indicator, set/clear buttons. Integrates with keyboard widget selection.

**Commit:**
```bash
git commit -m "feat: add per-key color assignment panel"
```

---

### Task 18: GTK GUI — Profiles Panel

Load/save/delete JSON profiles. File chooser dialog for import/export. Profile list with preview.

**Commit:**
```bash
git commit -m "feat: add profile management panel with JSON import/export"
```

---

### Task 19: GTK GUI — Sensors Panel

System sensor integration using lm-sensors. CPU temp, RAM usage mapping to color gradients. Configurable thresholds and update interval.

**Commit:**
```bash
git commit -m "feat: add system sensor panel with temperature/RAM color mapping"
```

---

### Task 20: Final Integration and Polish

1. End-to-end test: GUI → libf87 → device
2. Error handling review (disconnection, permission errors)
3. Desktop file and icon for application launcher
4. README with build instructions and usage

**Commit:**
```bash
git commit -m "chore: final integration, error handling, desktop entry"
```
