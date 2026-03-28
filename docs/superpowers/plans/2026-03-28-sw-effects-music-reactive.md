# Software Effects & Music-Reactive Lighting — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a software animation engine with 10 custom effects and 5 music-reactive visualizers to libf87, using producer-consumer threading (audio thread + animation thread).

**Architecture:** Two-thread model — audio thread captures system audio via PulseAudio Simple API, performs FFT (KissFFT) and beat detection, writes results to a lock-free SPSC ring buffer. Animation thread reads audio data, runs the active effect's render function, converts frame buffer to direct mode format, and sends to keyboard at 30fps. When no audio is needed, audio thread doesn't start.

**Tech Stack:** C11, pthreads, PulseAudio Simple API (libpulse-simple), KissFFT (vendored source), libevdev (reactive effects input)

---

## File Structure

```
lib/
  include/f87/
    animate.h          — public animation API (start/stop/set_effect/set_color)
    audio_types.h      — f87_audio_data_t, f87_audio_source_t, f87_audio_ring_t
  src/
    animate.c          — animation thread, frame loop, effect dispatch, signal handling
    animate_internal.h — internal types: f87_anim_ctx_t, f87_sw_effect_t, f87_effect_ctx_t
    ring_buffer.c      — lock-free SPSC ring buffer (write/read_latest)
    ring_buffer.h      — ring buffer header
    audio.c            — PulseAudio capture thread
    spectrum.c          — KissFFT wrapper, band grouping, beat detection
    spectrum.h          — internal spectrum API
    effects_sw.c       — software effect registry + 6 non-reactive effects
    effects_sw_reactive.c — 4 reactive effects (need input)
    visualizer.c       — 5 music-reactive effects
vendor/
  kissfft/             — KissFFT source (kiss_fft.h, kiss_fft.c, kiss_fftr.h, kiss_fftr.c)
tests/
  test_ring_buffer.c   — ring buffer unit tests
  test_spectrum.c      — FFT + beat detection unit tests
  test_effects_sw.c    — software effect render tests
```

---

### Task 1: Vendor KissFFT and Update Build System

**Files:**
- Create: `vendor/kissfft/kiss_fft.h`, `vendor/kissfft/kiss_fft.c`, `vendor/kissfft/kiss_fftr.h`, `vendor/kissfft/kiss_fftr.c`
- Create: `vendor/kissfft/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root)
- Modify: `lib/CMakeLists.txt`

- [ ] **Step 1: Download KissFFT source**

```bash
mkdir -p vendor/kissfft
# Download 4 files from KissFFT GitHub (BSD licensed)
curl -sL https://raw.githubusercontent.com/mborgerding/kissfft/master/kiss_fft.h -o vendor/kissfft/kiss_fft.h
curl -sL https://raw.githubusercontent.com/mborgerding/kissfft/master/kiss_fft.c -o vendor/kissfft/kiss_fft.c
curl -sL https://raw.githubusercontent.com/mborgerding/kissfft/master/tools/kiss_fftr.h -o vendor/kissfft/kiss_fftr.h
curl -sL https://raw.githubusercontent.com/mborgerding/kissfft/master/tools/kiss_fftr.c -o vendor/kissfft/kiss_fftr.c
```

- [ ] **Step 2: Create KissFFT CMakeLists.txt**

```cmake
# vendor/kissfft/CMakeLists.txt
add_library(kissfft STATIC kiss_fft.c kiss_fftr.c)
target_include_directories(kissfft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(kissfft PUBLIC kiss_fft_scalar=float)
```

- [ ] **Step 3: Update root CMakeLists.txt**

Add after the existing `add_subdirectory(lib)` line:

```cmake
add_subdirectory(vendor/kissfft)
```

- [ ] **Step 4: Update lib/CMakeLists.txt with new dependencies and source files**

Add PulseAudio detection and conditional compilation. After the existing `pkg_check_modules(LIBUSB REQUIRED libusb-1.0)`:

```cmake
# Audio support (optional)
option(BUILD_AUDIO "Build with PulseAudio audio support" ON)
if(BUILD_AUDIO)
    pkg_check_modules(PULSE libpulse-simple)
    if(PULSE_FOUND)
        message(STATUS "PulseAudio found — audio support enabled")
    else()
        message(STATUS "PulseAudio not found — audio support disabled")
    endif()
endif()
```

Add new source files to the `add_library(f87 SHARED ...)` call:

```cmake
add_library(f87 SHARED
    src/device.c
    src/protocol.c
    src/lighting.c
    src/effects.c
    src/animate.c
    src/ring_buffer.c
    src/effects_sw.c
    src/effects_sw_reactive.c
)
```

Add pthread and conditional audio linkage after existing `target_link_libraries`:

```cmake
target_link_libraries(f87 PRIVATE ${LIBUSB_LIBRARIES} Threads::Threads)
find_package(Threads REQUIRED)

if(BUILD_AUDIO AND PULSE_FOUND)
    target_sources(f87 PRIVATE src/audio.c src/spectrum.c src/visualizer.c)
    target_include_directories(f87 PRIVATE ${PULSE_INCLUDE_DIRS})
    target_link_libraries(f87 PRIVATE ${PULSE_LIBRARIES} kissfft m)
    target_compile_definitions(f87 PRIVATE F87_HAS_AUDIO=1)
endif()
```

Add new public headers to install/include paths:

```cmake
target_include_directories(f87 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${LIBUSB_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/src
)
```

- [ ] **Step 5: Create stub source files so build succeeds**

Create minimal stubs for all new .c files (empty functions, just enough to compile):

```c
/* lib/src/ring_buffer.h */
#ifndef F87_RING_BUFFER_H
#define F87_RING_BUFFER_H
/* Stub — implemented in Task 3 */
#endif

/* lib/src/ring_buffer.c */
#include "ring_buffer.h"
/* Stub — implemented in Task 3 */

/* lib/src/animate.c */
#include "protocol.h"
/* Stub — implemented in Task 4 */

/* lib/src/effects_sw.c */
/* Stub — implemented in Task 7+ */

/* lib/src/effects_sw_reactive.c */
#include "animate_internal.h"
const f87_sw_effect_t *f87_reactive_find_effect(f87_sw_effect_id id) {
    (void)id; return NULL;  /* Stub — populated in Task 11 */
}
```

- [ ] **Step 6: Build and verify**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make
```

Expected: Clean build, no errors. New files compiled, libf87 links with pthreads.

- [ ] **Step 7: Commit**

```bash
git add vendor/kissfft/ lib/CMakeLists.txt CMakeLists.txt lib/src/ring_buffer.h lib/src/ring_buffer.c lib/src/animate.c lib/src/effects_sw.c lib/src/effects_sw_reactive.c
git commit -m "build: add KissFFT vendor, PulseAudio detection, animation stubs"
```

---

### Task 2: Core Types — animate.h, audio_types.h, animate_internal.h

**Files:**
- Create: `lib/include/f87/animate.h`
- Create: `lib/include/f87/audio_types.h`
- Create: `lib/src/animate_internal.h`
- Modify: `lib/include/f87/f87.h` (add animate.h include)
- Modify: `lib/src/protocol.h` (add new error codes)

- [ ] **Step 1: Create audio_types.h**

```c
/* lib/include/f87/audio_types.h */
#ifndef F87_AUDIO_TYPES_H
#define F87_AUDIO_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define F87_AUDIO_BANDS 6

typedef enum {
    F87_AUDIO_MONITOR = 0,  /* System audio (PulseAudio monitor source) */
    F87_AUDIO_MIC     = 1,  /* Microphone input */
} f87_audio_source_t;

typedef struct {
    float bands[F87_AUDIO_BANDS]; /* Normalized frequency bands (0.0-1.0) */
    float energy;                  /* Total audio energy (0.0-1.0) */
    float beat_intensity;          /* Beat intensity (0.0-1.0, 0=no beat) */
    bool  beat;                    /* Beat detected this frame */
    uint64_t timestamp_us;         /* Microsecond timestamp */
} f87_audio_data_t;

#endif /* F87_AUDIO_TYPES_H */
```

- [ ] **Step 2: Create animate.h (public API)**

```c
/* lib/include/f87/animate.h */
#ifndef F87_ANIMATE_H
#define F87_ANIMATE_H

#include <stdint.h>
#include "audio_types.h"

typedef struct f87_anim_ctx f87_anim_ctx_t;

/* Software effect IDs */
typedef enum {
    /* Non-reactive software effects */
    F87_SW_FIRE      = 100,
    F87_SW_MATRIX    = 101,
    F87_SW_PLASMA    = 102,
    F87_SW_HEATMAP   = 103,
    F87_SW_RADAR     = 104,
    F87_SW_LIGHTNING  = 105,
    /* Reactive software effects (need /dev/input) */
    F87_SW_EXPLODE   = 110,
    F87_SW_RIPPLE    = 111,
    F87_SW_TYPEWRITER = 112,
    F87_SW_LIFE      = 113,
    /* Music-reactive effects */
    F87_MU_SPECTRUM  = 200,
    F87_MU_BEAT      = 201,
    F87_MU_ENERGY    = 202,
    F87_MU_VU        = 203,
    F87_MU_FREQ_MAP  = 204,
} f87_sw_effect_id;

typedef struct {
    uint8_t color[3];               /* Base color RGB (default: effect-specific) */
    uint8_t brightness;             /* 1-4 (default: 3) */
    uint8_t speed;                  /* 0-4 (default: 2) */
    f87_audio_source_t audio_source; /* Monitor or mic (music effects only) */
} f87_anim_config_t;

/* Start animation — launches threads, returns context. Non-blocking. */
f87_anim_ctx_t *f87_anim_start(struct f87_device *dev, f87_sw_effect_id effect_id,
                                const f87_anim_config_t *config);

/* Stop animation — joins threads, restores previous hardware effect. */
int f87_anim_stop(f87_anim_ctx_t *ctx);

/* Change active effect while running. */
int f87_anim_set_effect(f87_anim_ctx_t *ctx, f87_sw_effect_id effect_id);

/* Change base color while running. */
int f87_anim_set_color(f87_anim_ctx_t *ctx, uint8_t r, uint8_t g, uint8_t b);

/* Check if animation is still running (false if error stopped it). */
int f87_anim_is_running(f87_anim_ctx_t *ctx);

/* Get error code if animation stopped unexpectedly. */
int f87_anim_get_error(f87_anim_ctx_t *ctx);

/* Get effect name string from sw effect ID. */
const char *f87_sw_effect_name(f87_sw_effect_id id);

#endif /* F87_ANIMATE_H */
```

- [ ] **Step 3: Create animate_internal.h**

```c
/* lib/src/animate_internal.h */
#ifndef F87_ANIMATE_INTERNAL_H
#define F87_ANIMATE_INTERNAL_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include "protocol.h"
#include "ring_buffer.h"
#include "f87/animate.h"
#include "f87/audio_types.h"
#include "f87/lighting.h"

/* Frame buffer: 88 keys × RGB */
typedef struct {
    uint8_t keys[F87_KEY_COUNT][3];
} f87_frame_t;

/* Effect context passed to render functions */
typedef struct {
    f87_device *dev;
    uint8_t base_color[3];
    uint8_t brightness;        /* 1-4 */
    uint8_t speed;             /* 0-4 */
    uint64_t frame_count;
    uint64_t start_time_us;
    void *effect_data;         /* Effect-private state */
} f87_effect_ctx_t;

/* Software effect interface */
typedef struct {
    const char *name;
    f87_sw_effect_id id;
    bool needs_audio;
    bool needs_input;

    int  (*init)(f87_effect_ctx_t *ctx);
    void (*render)(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                   const f87_audio_data_t *audio);
    void (*on_key)(f87_effect_ctx_t *ctx, int key_id);  /* NULL if not reactive */
    void (*destroy)(f87_effect_ctx_t *ctx);
} f87_sw_effect_t;

/* Shared PRNG for effects */
uint32_t f87_effect_rand(uint32_t *seed);

/* Animation context (opaque to callers) */
struct f87_anim_ctx {
    f87_device *dev;
    f87_anim_config_t config;

    /* Threads */
    pthread_t anim_thread;
    pthread_t audio_thread;
    atomic_bool running;
    atomic_int error;

    /* Current effect */
    const f87_sw_effect_t *active_effect;
    f87_effect_ctx_t effect_ctx;
    pthread_mutex_t effect_mutex;  /* Protects effect switch */

    /* Audio ring buffer (NULL if no audio) */
    f87_audio_ring_t *audio_ring;

    /* Input fd for reactive effects (-1 if not needed) */
    int input_fd;
};

/* Effect registries — defined in effects_sw.c, effects_sw_reactive.c, visualizer.c */
const f87_sw_effect_t *f87_sw_find_effect(f87_sw_effect_id id);

/* Utility: get current time in microseconds */
uint64_t f87_time_us(void);

#endif /* F87_ANIMATE_INTERNAL_H */
```

- [ ] **Step 4: Add error codes to protocol.h**

Add after existing `#define F87_ERR_NOMEM -6`:

```c
#define F87_ERR_AUDIO    -7   /* PulseAudio connection/read error */
#define F87_ERR_ANIMATE  -8   /* Animation thread start error */
```

Also update `f87_strerror()` in device.c to handle the new codes.

- [ ] **Step 5: Add animate.h to umbrella header**

In `lib/include/f87/f87.h`, add:

```c
#include "animate.h"
```

- [ ] **Step 6: Build and verify**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make
```

Expected: Clean build.

- [ ] **Step 7: Commit**

```bash
git add lib/include/f87/animate.h lib/include/f87/audio_types.h lib/src/animate_internal.h lib/include/f87/f87.h lib/src/protocol.h lib/src/device.c
git commit -m "feat: add animation core types — public API, effect interface, audio types"
```

---

### Task 3: Lock-Free SPSC Ring Buffer

**Files:**
- Modify: `lib/src/ring_buffer.h`
- Modify: `lib/src/ring_buffer.c`
- Create: `tests/test_ring_buffer.c`
- Modify: `CMakeLists.txt` (add test)

- [ ] **Step 1: Write ring buffer tests**

```c
/* tests/test_ring_buffer.c */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include "../lib/src/ring_buffer.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_init(void) {
    f87_audio_ring_t ring;
    f87_ring_init(&ring);
    return ring.write_idx == 0 && ring.read_idx == 0;
}

static int test_write_read(void) {
    f87_audio_ring_t ring;
    f87_ring_init(&ring);

    f87_audio_data_t data = {0};
    data.energy = 0.75f;
    data.beat = true;
    data.bands[0] = 1.0f;

    f87_ring_write(&ring, &data);

    f87_audio_data_t out;
    int got = f87_ring_read_latest(&ring, &out);
    return got == 1 && out.energy == 0.75f && out.beat == true && out.bands[0] == 1.0f;
}

static int test_read_empty(void) {
    f87_audio_ring_t ring;
    f87_ring_init(&ring);

    f87_audio_data_t out;
    int got = f87_ring_read_latest(&ring, &out);
    return got == 0;  /* No data available */
}

static int test_overwrite(void) {
    f87_audio_ring_t ring;
    f87_ring_init(&ring);

    /* Write more than ring size — should not crash, latest value available */
    for (int i = 0; i < F87_AUDIO_RING_SIZE + 4; i++) {
        f87_audio_data_t data = {0};
        data.energy = (float)i / 100.0f;
        f87_ring_write(&ring, &data);
    }

    f87_audio_data_t out;
    int got = f87_ring_read_latest(&ring, &out);
    float expected = (float)(F87_AUDIO_RING_SIZE + 3) / 100.0f;
    return got == 1 && (out.energy - expected) < 0.001f;
}

/* Concurrent producer-consumer test */
static f87_audio_ring_t shared_ring;
static atomic_bool producer_done;

static void *producer_func(void *arg) {
    (void)arg;
    for (int i = 0; i < 10000; i++) {
        f87_audio_data_t data = {0};
        data.energy = (float)i;
        data.timestamp_us = (uint64_t)i;
        f87_ring_write(&shared_ring, &data);
    }
    atomic_store(&producer_done, true);
    return NULL;
}

static int test_concurrent(void) {
    f87_ring_init(&shared_ring);
    atomic_store(&producer_done, false);

    pthread_t producer;
    pthread_create(&producer, NULL, producer_func, NULL);

    int reads = 0;
    uint64_t last_ts = 0;
    while (!atomic_load(&producer_done) || reads == 0) {
        f87_audio_data_t out;
        if (f87_ring_read_latest(&shared_ring, &out)) {
            /* Timestamps must be monotonically non-decreasing */
            if (out.timestamp_us < last_ts) {
                pthread_join(producer, NULL);
                return 0;
            }
            last_ts = out.timestamp_us;
            reads++;
        }
    }

    pthread_join(producer, NULL);
    return reads > 0 && last_ts > 0;
}

int main(void) {
    printf("Ring buffer tests:\n");
    TEST(init);
    TEST(write_read);
    TEST(read_empty);
    TEST(overwrite);
    TEST(concurrent);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

In root `CMakeLists.txt`, after existing `add_test(NAME device_tests COMMAND test_device)`:

```cmake
add_executable(test_ring_buffer tests/test_ring_buffer.c lib/src/ring_buffer.c)
target_include_directories(test_ring_buffer PRIVATE lib/src lib/include)
target_link_libraries(test_ring_buffer Threads::Threads)
add_test(NAME ring_buffer_tests COMMAND test_ring_buffer)
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make test_ring_buffer 2>&1
```

Expected: Compile error — ring buffer functions not implemented.

- [ ] **Step 4: Implement ring_buffer.h**

```c
/* lib/src/ring_buffer.h */
#ifndef F87_RING_BUFFER_H
#define F87_RING_BUFFER_H

#include <stdatomic.h>
#include <stdint.h>
#include "f87/audio_types.h"

#define F87_AUDIO_RING_SIZE 8  /* Must be power of 2 */
#define F87_AUDIO_RING_MASK (F87_AUDIO_RING_SIZE - 1)

typedef struct {
    f87_audio_data_t slots[F87_AUDIO_RING_SIZE];
    atomic_uint_fast32_t write_idx;
    atomic_uint_fast32_t read_idx;
} f87_audio_ring_t;

/* Initialize ring buffer (zero all slots, reset indices). */
void f87_ring_init(f87_audio_ring_t *ring);

/* Write one audio data frame (producer — audio thread). */
void f87_ring_write(f87_audio_ring_t *ring, const f87_audio_data_t *data);

/* Read the most recent audio data (consumer — anim thread).
   Skips all intermediate frames to get latest.
   Returns 1 if data was read, 0 if ring is empty. */
int f87_ring_read_latest(f87_audio_ring_t *ring, f87_audio_data_t *out);

#endif /* F87_RING_BUFFER_H */
```

- [ ] **Step 5: Implement ring_buffer.c**

```c
/* lib/src/ring_buffer.c */
#include "ring_buffer.h"
#include <string.h>

void f87_ring_init(f87_audio_ring_t *ring)
{
    memset(ring->slots, 0, sizeof(ring->slots));
    atomic_store(&ring->write_idx, 0);
    atomic_store(&ring->read_idx, 0);
}

void f87_ring_write(f87_audio_ring_t *ring, const f87_audio_data_t *data)
{
    uint32_t wi = atomic_load_explicit(&ring->write_idx, memory_order_relaxed);
    ring->slots[wi & F87_AUDIO_RING_MASK] = *data;
    atomic_store_explicit(&ring->write_idx, wi + 1, memory_order_release);
}

int f87_ring_read_latest(f87_audio_ring_t *ring, f87_audio_data_t *out)
{
    uint32_t wi = atomic_load_explicit(&ring->write_idx, memory_order_acquire);
    uint32_t ri = atomic_load_explicit(&ring->read_idx, memory_order_relaxed);

    if (wi == ri)
        return 0;  /* Empty */

    /* Jump to latest: read the most recently written slot */
    uint32_t latest = wi - 1;
    *out = ring->slots[latest & F87_AUDIO_RING_MASK];
    atomic_store_explicit(&ring->read_idx, wi, memory_order_relaxed);
    return 1;
}
```

- [ ] **Step 6: Build and run tests**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make test_ring_buffer && ./test_ring_buffer
```

Expected: 5/5 tests passed.

- [ ] **Step 7: Commit**

```bash
git add lib/src/ring_buffer.h lib/src/ring_buffer.c tests/test_ring_buffer.c CMakeLists.txt
git commit -m "feat: add lock-free SPSC ring buffer with tests"
```

---

### Task 4: Animation Thread Core

**Files:**
- Modify: `lib/src/animate.c`
- Modify: `lib/src/effects_sw.c` (stub effect registry)

- [ ] **Step 1: Implement f87_time_us() utility**

In `lib/src/animate.c`:

```c
#include "animate_internal.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint64_t f87_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}
```

- [ ] **Step 2: Implement animation loop (anim_thread_func)**

```c
#define F87_ANIM_FRAME_US  33333  /* ~30fps = 33.3ms */

static void *anim_thread_func(void *arg)
{
    f87_anim_ctx_t *ctx = arg;

    int rc = f87_direct_mode_enable(ctx->dev);
    if (rc < 0) {
        atomic_store(&ctx->error, rc);
        atomic_store(&ctx->running, false);
        return NULL;
    }

    f87_frame_t frame;
    f87_audio_data_t audio;

    while (atomic_load(&ctx->running)) {
        uint64_t t0 = f87_time_us();

        /* Read latest audio data if available */
        const f87_audio_data_t *audio_ptr = NULL;
        if (ctx->audio_ring && f87_ring_read_latest(ctx->audio_ring, &audio))
            audio_ptr = &audio;

        /* Render current effect */
        pthread_mutex_lock(&ctx->effect_mutex);
        if (ctx->active_effect && ctx->active_effect->render) {
            memset(&frame, 0, sizeof(frame));
            ctx->effect_ctx.frame_count++;
            ctx->active_effect->render(&ctx->effect_ctx, &frame, audio_ptr);
        }
        pthread_mutex_unlock(&ctx->effect_mutex);

        /* Convert frame to direct mode and send */
        f87_color colors[F87_KEY_COUNT];
        for (int i = 0; i < F87_KEY_COUNT; i++) {
            colors[i].r = frame.keys[i][0];
            colors[i].g = frame.keys[i][1];
            colors[i].b = frame.keys[i][2];
        }

        rc = f87_direct_send_frame(ctx->dev, colors, f87_led_index, F87_KEY_COUNT);
        if (rc < 0) {
            atomic_store(&ctx->error, rc);
            atomic_store(&ctx->running, false);
            break;
        }

        /* Sleep to maintain target frame rate */
        uint64_t elapsed = f87_time_us() - t0;
        if (elapsed < F87_ANIM_FRAME_US)
            usleep((useconds_t)(F87_ANIM_FRAME_US - elapsed));
    }

    f87_direct_mode_disable(ctx->dev);
    return NULL;
}
```

- [ ] **Step 3: Implement f87_anim_start()**

```c
f87_anim_ctx_t *f87_anim_start(f87_device *dev, f87_sw_effect_id effect_id,
                                 const f87_anim_config_t *config)
{
    if (!dev) return NULL;

    const f87_sw_effect_t *effect = f87_sw_find_effect(effect_id);
    if (!effect) return NULL;

    f87_anim_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    ctx->dev = dev;
    ctx->input_fd = -1;
    pthread_mutex_init(&ctx->effect_mutex, NULL);

    /* Apply config with defaults */
    if (config) {
        ctx->config = *config;
    } else {
        ctx->config.brightness = 3;
        ctx->config.speed = 2;
        ctx->config.audio_source = F87_AUDIO_MONITOR;
    }

    /* Initialize effect context */
    ctx->effect_ctx.dev = dev;
    memcpy(ctx->effect_ctx.base_color, ctx->config.color, 3);
    ctx->effect_ctx.brightness = ctx->config.brightness;
    ctx->effect_ctx.speed = ctx->config.speed;
    ctx->effect_ctx.frame_count = 0;
    ctx->effect_ctx.start_time_us = f87_time_us();
    ctx->effect_ctx.effect_data = NULL;

    /* Init effect */
    ctx->active_effect = effect;
    if (effect->init) {
        int rc = effect->init(&ctx->effect_ctx);
        if (rc < 0) {
            free(ctx);
            return NULL;
        }
    }

    /* Start audio thread if needed */
#ifdef F87_HAS_AUDIO
    if (effect->needs_audio) {
        ctx->audio_ring = calloc(1, sizeof(f87_audio_ring_t));
        if (!ctx->audio_ring) {
            if (effect->destroy) effect->destroy(&ctx->effect_ctx);
            free(ctx);
            return NULL;
        }
        f87_ring_init(ctx->audio_ring);

        atomic_store(&ctx->running, true);

        /* Audio thread started in audio.c — Task 5 */
        extern int f87_audio_thread_start(f87_anim_ctx_t *ctx);
        int rc = f87_audio_thread_start(ctx);
        if (rc < 0) {
            free(ctx->audio_ring);
            if (effect->destroy) effect->destroy(&ctx->effect_ctx);
            free(ctx);
            return NULL;
        }
    } else {
        atomic_store(&ctx->running, true);
    }
#else
    if (effect->needs_audio) {
        if (effect->destroy) effect->destroy(&ctx->effect_ctx);
        free(ctx);
        return NULL;  /* No audio support compiled */
    }
    atomic_store(&ctx->running, true);
#endif

    /* Start animation thread */
    int rc = pthread_create(&ctx->anim_thread, NULL, anim_thread_func, ctx);
    if (rc != 0) {
        atomic_store(&ctx->running, false);
        if (effect->destroy) effect->destroy(&ctx->effect_ctx);
#ifdef F87_HAS_AUDIO
        if (ctx->audio_ring) {
            /* TODO: stop audio thread if started */
            free(ctx->audio_ring);
        }
#endif
        free(ctx);
        return NULL;
    }

    return ctx;
}
```

- [ ] **Step 4: Implement f87_anim_stop()**

```c
int f87_anim_stop(f87_anim_ctx_t *ctx)
{
    if (!ctx) return F87_ERR_INIT;

    atomic_store(&ctx->running, false);

    /* Join animation thread */
    pthread_join(ctx->anim_thread, NULL);

#ifdef F87_HAS_AUDIO
    /* Join audio thread if running */
    if (ctx->audio_ring) {
        pthread_join(ctx->audio_thread, NULL);
        free(ctx->audio_ring);
    }
#endif

    /* Destroy effect */
    pthread_mutex_lock(&ctx->effect_mutex);
    if (ctx->active_effect && ctx->active_effect->destroy)
        ctx->active_effect->destroy(&ctx->effect_ctx);
    pthread_mutex_unlock(&ctx->effect_mutex);

    /* Close input fd if open */
    if (ctx->input_fd >= 0)
        close(ctx->input_fd);

    pthread_mutex_destroy(&ctx->effect_mutex);

    int err = atomic_load(&ctx->error);
    free(ctx);
    return err;
}
```

- [ ] **Step 5: Implement f87_anim_set_effect(), set_color(), is_running(), get_error()**

```c
int f87_anim_set_effect(f87_anim_ctx_t *ctx, f87_sw_effect_id effect_id)
{
    if (!ctx) return F87_ERR_INIT;

    const f87_sw_effect_t *new_effect = f87_sw_find_effect(effect_id);
    if (!new_effect) return F87_ERR_INIT;

    pthread_mutex_lock(&ctx->effect_mutex);

    /* Destroy old effect */
    if (ctx->active_effect && ctx->active_effect->destroy)
        ctx->active_effect->destroy(&ctx->effect_ctx);

    /* Reset context for new effect */
    ctx->effect_ctx.frame_count = 0;
    ctx->effect_ctx.start_time_us = f87_time_us();
    ctx->effect_ctx.effect_data = NULL;

    /* Init new effect */
    ctx->active_effect = new_effect;
    if (new_effect->init)
        new_effect->init(&ctx->effect_ctx);

    pthread_mutex_unlock(&ctx->effect_mutex);
    return F87_OK;
}

int f87_anim_set_color(f87_anim_ctx_t *ctx, uint8_t r, uint8_t g, uint8_t b)
{
    if (!ctx) return F87_ERR_INIT;
    ctx->effect_ctx.base_color[0] = r;
    ctx->effect_ctx.base_color[1] = g;
    ctx->effect_ctx.base_color[2] = b;
    return F87_OK;
}

int f87_anim_is_running(f87_anim_ctx_t *ctx)
{
    return ctx && atomic_load(&ctx->running);
}

int f87_anim_get_error(f87_anim_ctx_t *ctx)
{
    return ctx ? atomic_load(&ctx->error) : F87_ERR_INIT;
}
```

- [ ] **Step 6: Implement stub effect registry in effects_sw.c**

```c
/* lib/src/effects_sw.c */
#include "animate_internal.h"
#include <stddef.h>

/* Forward declarations — effects added in later tasks */
/* extern const f87_sw_effect_t f87_effect_fire; */

static const f87_sw_effect_t *all_effects[] = {
    /* Populated as effects are implemented */
    NULL
};

const f87_sw_effect_t *f87_sw_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; all_effects[i] != NULL; i++) {
        if (all_effects[i]->id == id)
            return all_effects[i];
    }
    return NULL;
}

const char *f87_sw_effect_name(f87_sw_effect_id id)
{
    const f87_sw_effect_t *e = f87_sw_find_effect(id);
    return e ? e->name : "Unknown";
}
```

- [ ] **Step 7: Build and verify**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make
```

Expected: Clean build. Animation engine compiles but no effects registered yet.

- [ ] **Step 8: Commit**

```bash
git add lib/src/animate.c lib/src/effects_sw.c
git commit -m "feat: add animation thread core — start/stop, frame loop, effect dispatch"
```

---

### Task 5: PulseAudio Audio Capture Thread

**Files:**
- Create: `lib/src/audio.c`

- [ ] **Step 1: Implement audio thread**

```c
/* lib/src/audio.c */
#ifdef F87_HAS_AUDIO

#include "animate_internal.h"
#include <pulse/simple.h>
#include <pulse/error.h>
#include <string.h>
#include <stdio.h>

#define F87_AUDIO_RATE      44100
#define F87_AUDIO_CHANNELS  1
#define F87_AUDIO_BUF_SIZE  1024

/* Forward declaration from spectrum.c */
extern void f87_spectrum_analyze(const float *samples, int count,
                                  f87_audio_data_t *out);

static void *audio_thread_func(void *arg)
{
    f87_anim_ctx_t *ctx = arg;
    int pa_err;

    pa_sample_spec ss = {
        .format   = PA_SAMPLE_FLOAT32LE,
        .rate     = F87_AUDIO_RATE,
        .channels = F87_AUDIO_CHANNELS,
    };

    /* Select source: NULL = default monitor, or specific device */
    const char *source = NULL;
    if (ctx->config.audio_source == F87_AUDIO_MONITOR) {
        /* PulseAudio monitor source — use default monitor
           PipeWire also exposes this via PulseAudio compat */
        source = NULL;  /* We'll set server to NULL and use .monitor */
    }

    pa_simple *pa = pa_simple_new(
        NULL,                /* Default server */
        "f87control",        /* App name */
        PA_STREAM_RECORD,
        source,              /* Source device (NULL = default) */
        "audio-visualizer",  /* Stream description */
        &ss,
        NULL,                /* Default channel map */
        NULL,                /* Default buffer attr */
        &pa_err
    );

    if (!pa) {
        fprintf(stderr, "f87: PulseAudio error: %s\n", pa_strerror(pa_err));
        atomic_store(&ctx->error, F87_ERR_AUDIO);
        return NULL;
    }

    float samples[F87_AUDIO_BUF_SIZE];

    while (atomic_load(&ctx->running)) {
        /* pa_simple_read blocks until buffer is full (~23ms at 44100Hz) */
        if (pa_simple_read(pa, samples, sizeof(samples), &pa_err) < 0) {
            fprintf(stderr, "f87: PulseAudio read error: %s\n", pa_strerror(pa_err));
            break;
        }

        f87_audio_data_t data;
        f87_spectrum_analyze(samples, F87_AUDIO_BUF_SIZE, &data);
        data.timestamp_us = f87_time_us();

        f87_ring_write(ctx->audio_ring, &data);
    }

    pa_simple_free(pa);
    return NULL;
}

int f87_audio_thread_start(f87_anim_ctx_t *ctx)
{
    int rc = pthread_create(&ctx->audio_thread, NULL, audio_thread_func, ctx);
    if (rc != 0) {
        atomic_store(&ctx->error, F87_ERR_AUDIO);
        return F87_ERR_AUDIO;
    }
    return F87_OK;
}

#else /* !F87_HAS_AUDIO */

#include "animate_internal.h"

int f87_audio_thread_start(f87_anim_ctx_t *ctx)
{
    (void)ctx;
    return F87_ERR_AUDIO;
}

#endif /* F87_HAS_AUDIO */
```

- [ ] **Step 2: Build and verify**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make
```

Expected: Clean build. If PulseAudio is found, audio.c compiles with PA support.

- [ ] **Step 3: Commit**

```bash
git add lib/src/audio.c
git commit -m "feat: add PulseAudio audio capture thread"
```

---

### Task 6: Spectrum Analysis and Beat Detection

**Files:**
- Create: `lib/src/spectrum.h`
- Create: `lib/src/spectrum.c`
- Create: `tests/test_spectrum.c`
- Modify: `CMakeLists.txt` (add test)

- [ ] **Step 1: Write spectrum tests**

```c
/* tests/test_spectrum.c */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../lib/src/spectrum.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100
#define BUF_SIZE 1024

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

/* Generate a pure sine wave at given frequency */
static void gen_sine(float *buf, int count, float freq, float amplitude) {
    for (int i = 0; i < count; i++)
        buf[i] = amplitude * sinf(2.0f * (float)M_PI * freq * (float)i / SAMPLE_RATE);
}

static int test_silence(void) {
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float samples[BUF_SIZE] = {0};
    f87_audio_data_t out;
    f87_spectrum_analyze_ctx(&ctx, samples, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);

    /* All bands should be ~0, no beat */
    return out.energy < 0.01f && out.beat == false;
}

static int test_bass_detection(void) {
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float samples[BUF_SIZE];
    gen_sine(samples, BUF_SIZE, 100.0f, 0.9f);  /* 100Hz = bass */

    f87_audio_data_t out;
    f87_spectrum_analyze_ctx(&ctx, samples, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);

    /* Bass band (60-250Hz) should be significantly higher than treble */
    return out.bands[1] > 0.1f && out.bands[1] > out.bands[5];
}

static int test_treble_detection(void) {
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float samples[BUF_SIZE];
    gen_sine(samples, BUF_SIZE, 8000.0f, 0.9f);  /* 8kHz = treble */

    f87_audio_data_t out;
    f87_spectrum_analyze_ctx(&ctx, samples, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);

    /* Treble band (4k-16k) should be dominant */
    return out.bands[5] > 0.1f && out.bands[5] > out.bands[0];
}

static int test_beat_on_spike(void) {
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float silence[BUF_SIZE] = {0};
    float loud[BUF_SIZE];
    gen_sine(loud, BUF_SIZE, 80.0f, 1.0f);  /* Strong bass */

    f87_audio_data_t out;

    /* Feed silence for a while to establish baseline */
    for (int i = 0; i < 20; i++)
        f87_spectrum_analyze_ctx(&ctx, silence, BUF_SIZE, &out);

    /* Now hit with loud bass — should trigger beat */
    f87_spectrum_analyze_ctx(&ctx, loud, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);
    return out.beat == true && out.beat_intensity > 0.0f;
}

static int test_no_beat_on_constant(void) {
    f87_spectrum_ctx_t ctx;
    f87_spectrum_init(&ctx);

    float steady[BUF_SIZE];
    gen_sine(steady, BUF_SIZE, 80.0f, 0.5f);

    f87_audio_data_t out;

    /* Feed same constant signal many times — no beat after initial transient */
    for (int i = 0; i < 30; i++)
        f87_spectrum_analyze_ctx(&ctx, steady, BUF_SIZE, &out);

    f87_spectrum_destroy(&ctx);

    /* After steady-state, no beat should be detected */
    return out.beat == false;
}

int main(void) {
    printf("Spectrum analysis tests:\n");
    TEST(silence);
    TEST(bass_detection);
    TEST(treble_detection);
    TEST(beat_on_spike);
    TEST(no_beat_on_constant);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

In root `CMakeLists.txt`:

```cmake
if(BUILD_AUDIO AND PULSE_FOUND)
    add_executable(test_spectrum tests/test_spectrum.c lib/src/spectrum.c)
    target_include_directories(test_spectrum PRIVATE lib/src lib/include vendor/kissfft)
    target_link_libraries(test_spectrum kissfft m)
    target_compile_definitions(test_spectrum PRIVATE F87_HAS_AUDIO=1)
    add_test(NAME spectrum_tests COMMAND test_spectrum)
endif()
```

- [ ] **Step 3: Create spectrum.h**

```c
/* lib/src/spectrum.h */
#ifndef F87_SPECTRUM_H
#define F87_SPECTRUM_H

#include "f87/audio_types.h"
#include "kiss_fftr.h"

#define F87_FFT_SIZE     2048
#define F87_BEAT_HISTORY 22  /* ~500ms at 43Hz audio rate */
#define F87_BEAT_COOLDOWN_FRAMES 5  /* ~115ms at 43Hz */

typedef struct {
    kiss_fftr_cfg fft_cfg;
    float window[F87_FFT_SIZE];         /* Hanning window coefficients */
    float fft_input[F87_FFT_SIZE];      /* Accumulated samples for FFT */
    int sample_pos;                      /* Current position in fft_input */

    /* Beat detection state */
    float bass_history[F87_BEAT_HISTORY];
    int bass_history_idx;
    int beat_cooldown;
} f87_spectrum_ctx_t;

/* Initialize spectrum analyzer (allocates FFT plan). */
void f87_spectrum_init(f87_spectrum_ctx_t *ctx);

/* Analyze samples and fill audio_data_t. Uses internal state for beat detection. */
void f87_spectrum_analyze_ctx(f87_spectrum_ctx_t *ctx, const float *samples,
                               int count, f87_audio_data_t *out);

/* Convenience: stateless single-shot analysis (allocates/frees internally).
   Used by audio thread — wraps a persistent ctx. */
void f87_spectrum_analyze(const float *samples, int count, f87_audio_data_t *out);

/* Free FFT resources. */
void f87_spectrum_destroy(f87_spectrum_ctx_t *ctx);

#endif /* F87_SPECTRUM_H */
```

- [ ] **Step 4: Implement spectrum.c**

```c
/* lib/src/spectrum.c */
#ifdef F87_HAS_AUDIO

#include "spectrum.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 44100

/* Frequency band boundaries in Hz */
static const float band_edges[F87_AUDIO_BANDS + 1] = {
    20.0f, 60.0f, 250.0f, 500.0f, 2000.0f, 4000.0f, 16000.0f
};

void f87_spectrum_init(f87_spectrum_ctx_t *ctx)
{
    memset(ctx, 0, sizeof(*ctx));

    ctx->fft_cfg = kiss_fftr_alloc(F87_FFT_SIZE, 0, NULL, NULL);

    /* Pre-compute Hanning window */
    for (int i = 0; i < F87_FFT_SIZE; i++)
        ctx->window[i] = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * (float)i / (float)(F87_FFT_SIZE - 1)));

    ctx->sample_pos = 0;
    ctx->bass_history_idx = 0;
    ctx->beat_cooldown = 0;
}

void f87_spectrum_destroy(f87_spectrum_ctx_t *ctx)
{
    if (ctx->fft_cfg) {
        kiss_fftr_free(ctx->fft_cfg);
        ctx->fft_cfg = NULL;
    }
}

static float bin_to_freq(int bin)
{
    return (float)bin * (float)SAMPLE_RATE / (float)F87_FFT_SIZE;
}

void f87_spectrum_analyze_ctx(f87_spectrum_ctx_t *ctx, const float *samples,
                               int count, f87_audio_data_t *out)
{
    memset(out, 0, sizeof(*out));

    /* Accumulate samples into FFT input buffer */
    int to_copy = count;
    if (ctx->sample_pos + to_copy > F87_FFT_SIZE)
        to_copy = F87_FFT_SIZE - ctx->sample_pos;

    memcpy(ctx->fft_input + ctx->sample_pos, samples, to_copy * sizeof(float));
    ctx->sample_pos += to_copy;

    /* Not enough samples yet for FFT */
    if (ctx->sample_pos < F87_FFT_SIZE) return;

    /* Apply Hanning window */
    float windowed[F87_FFT_SIZE];
    for (int i = 0; i < F87_FFT_SIZE; i++)
        windowed[i] = ctx->fft_input[i] * ctx->window[i];

    /* Reset sample position (slide by half for overlap) */
    memmove(ctx->fft_input, ctx->fft_input + F87_FFT_SIZE / 2,
            (F87_FFT_SIZE / 2) * sizeof(float));
    ctx->sample_pos = F87_FFT_SIZE / 2;

    /* Run FFT */
    kiss_fft_cpx spectrum[F87_FFT_SIZE / 2 + 1];
    kiss_fftr(ctx->fft_cfg, windowed, spectrum);

    /* Compute magnitude spectrum and group into bands */
    int num_bins = F87_FFT_SIZE / 2 + 1;
    float band_energy[F87_AUDIO_BANDS] = {0};
    int band_count[F87_AUDIO_BANDS] = {0};
    float total_energy = 0.0f;

    for (int i = 1; i < num_bins; i++) {
        float freq = bin_to_freq(i);
        float mag = sqrtf(spectrum[i].r * spectrum[i].r + spectrum[i].i * spectrum[i].i);
        mag /= (float)(F87_FFT_SIZE / 2);  /* Normalize */

        total_energy += mag * mag;

        /* Find which band this frequency belongs to */
        for (int b = 0; b < F87_AUDIO_BANDS; b++) {
            if (freq >= band_edges[b] && freq < band_edges[b + 1]) {
                band_energy[b] += mag;
                band_count[b]++;
                break;
            }
        }
    }

    /* Average and normalize bands */
    float max_band = 0.0f;
    for (int b = 0; b < F87_AUDIO_BANDS; b++) {
        if (band_count[b] > 0)
            band_energy[b] /= (float)band_count[b];
        if (band_energy[b] > max_band)
            max_band = band_energy[b];
    }

    if (max_band > 0.0f) {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            out->bands[b] = band_energy[b] / max_band;
    }

    /* Total energy (normalized roughly) */
    out->energy = sqrtf(total_energy / (float)num_bins);
    if (out->energy > 1.0f) out->energy = 1.0f;

    /* Beat detection: bass energy (bands 0+1) vs recent history */
    float bass = (band_energy[0] + band_energy[1]) * 0.5f;

    /* Update history */
    ctx->bass_history[ctx->bass_history_idx % F87_BEAT_HISTORY] = bass;
    ctx->bass_history_idx++;

    /* Compute average bass energy over history */
    float avg_bass = 0.0f;
    int hist_count = ctx->bass_history_idx < F87_BEAT_HISTORY
                     ? ctx->bass_history_idx : F87_BEAT_HISTORY;
    for (int i = 0; i < hist_count; i++)
        avg_bass += ctx->bass_history[i];
    avg_bass /= (float)hist_count;

    /* Beat if current bass > 1.5x average and cooldown expired */
    if (ctx->beat_cooldown > 0) {
        ctx->beat_cooldown--;
    } else if (hist_count >= 3 && bass > avg_bass * 1.5f && bass > 0.001f) {
        out->beat = true;
        out->beat_intensity = (bass - avg_bass) / (avg_bass + 0.001f);
        if (out->beat_intensity > 1.0f) out->beat_intensity = 1.0f;
        ctx->beat_cooldown = F87_BEAT_COOLDOWN_FRAMES;
    }
}

/* Thread-local persistent context for convenience wrapper */
static __thread f87_spectrum_ctx_t *tl_ctx = NULL;

void f87_spectrum_analyze(const float *samples, int count, f87_audio_data_t *out)
{
    if (!tl_ctx) {
        tl_ctx = calloc(1, sizeof(f87_spectrum_ctx_t));
        f87_spectrum_init(tl_ctx);
    }
    f87_spectrum_analyze_ctx(tl_ctx, samples, count, out);
}

#endif /* F87_HAS_AUDIO */
```

- [ ] **Step 5: Build and run tests**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make test_spectrum && ./test_spectrum
```

Expected: 5/5 tests passed.

- [ ] **Step 6: Commit**

```bash
git add lib/src/spectrum.h lib/src/spectrum.c tests/test_spectrum.c CMakeLists.txt
git commit -m "feat: add FFT spectrum analysis and beat detection with tests"
```

---

### Task 7: First Software Effect — Fire

**Files:**
- Modify: `lib/src/effects_sw.c`
- Create: `tests/test_effects_sw.c`
- Modify: `CMakeLists.txt` (add test)

This task implements the fire effect as proof-of-concept and establishes the effect testing pattern used for all subsequent effects.

- [ ] **Step 1: Write effect test framework**

```c
/* tests/test_effects_sw.c */
#include <stdio.h>
#include <string.h>
#include "../lib/src/animate_internal.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

/* Helper: create a default effect context */
static f87_effect_ctx_t make_ctx(void) {
    f87_effect_ctx_t ctx = {0};
    ctx.base_color[0] = 255;
    ctx.base_color[1] = 80;
    ctx.base_color[2] = 0;
    ctx.brightness = 3;
    ctx.speed = 2;
    ctx.frame_count = 0;
    ctx.start_time_us = 0;
    return ctx;
}

static int test_find_fire(void) {
    const f87_sw_effect_t *e = f87_sw_find_effect(F87_SW_FIRE);
    return e != NULL && e->id == F87_SW_FIRE && e->needs_audio == false;
}

static int test_fire_init_destroy(void) {
    const f87_sw_effect_t *e = f87_sw_find_effect(F87_SW_FIRE);
    if (!e) return 0;

    f87_effect_ctx_t ctx = make_ctx();
    int rc = e->init(&ctx);
    if (rc < 0) return 0;

    e->destroy(&ctx);
    return 1;  /* No crash */
}

static int test_fire_render_no_crash(void) {
    const f87_sw_effect_t *e = f87_sw_find_effect(F87_SW_FIRE);
    if (!e) return 0;

    f87_effect_ctx_t ctx = make_ctx();
    e->init(&ctx);

    f87_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    /* Render 100 frames — should not crash, NULL audio is fine */
    for (int i = 0; i < 100; i++) {
        ctx.frame_count = (uint64_t)i;
        e->render(&ctx, &frame, NULL);
    }

    e->destroy(&ctx);
    return 1;
}

static int test_fire_produces_output(void) {
    const f87_sw_effect_t *e = f87_sw_find_effect(F87_SW_FIRE);
    if (!e) return 0;

    f87_effect_ctx_t ctx = make_ctx();
    e->init(&ctx);

    f87_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    /* Render a few frames to let the fire build up */
    for (int i = 0; i < 30; i++) {
        ctx.frame_count = (uint64_t)i;
        e->render(&ctx, &frame, NULL);
    }

    /* At least some keys should have non-zero color */
    int lit = 0;
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        if (frame.keys[k][0] || frame.keys[k][1] || frame.keys[k][2])
            lit++;
    }

    e->destroy(&ctx);
    return lit > 0;
}

static int test_effect_name(void) {
    const char *name = f87_sw_effect_name(F87_SW_FIRE);
    return name != NULL && strcmp(name, "Fire") == 0;
}

int main(void) {
    printf("Software effects tests:\n");
    TEST(find_fire);
    TEST(fire_init_destroy);
    TEST(fire_render_no_crash);
    TEST(fire_produces_output);
    TEST(effect_name);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
```

- [ ] **Step 2: Add test to CMakeLists.txt**

```cmake
add_executable(test_effects_sw tests/test_effects_sw.c lib/src/effects_sw.c lib/src/ring_buffer.c)
target_include_directories(test_effects_sw PRIVATE lib/src lib/include)
target_link_libraries(test_effects_sw Threads::Threads)
add_test(NAME effects_sw_tests COMMAND test_effects_sw)
```

- [ ] **Step 3: Run test to verify it fails**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make test_effects_sw && ./test_effects_sw
```

Expected: FAIL — fire effect not found.

- [ ] **Step 4: Implement fire effect in effects_sw.c**

The Doom fire algorithm: bottom row is "hot" (base_color at full intensity), heat propagates upward with random decay. Uses the keyboard's 6-row layout.

```c
/* lib/src/effects_sw.c */
#include "animate_internal.h"
#include <stdlib.h>
#include <string.h>

/* ===== FIRE EFFECT ===== */
/* Doom fire algorithm adapted for 6-row keyboard layout */

#define FIRE_ROWS 6
#define FIRE_COLS 22  /* Maximum columns in F87 layout */

typedef struct {
    float heat[FIRE_ROWS][FIRE_COLS];  /* Heat map 0.0-1.0 */
    uint32_t seed;                      /* PRNG state */
} fire_data_t;

uint32_t f87_effect_rand(uint32_t *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return (*seed >> 16) & 0x7FFF;
}

static int fire_init(f87_effect_ctx_t *ctx)
{
    fire_data_t *fd = calloc(1, sizeof(fire_data_t));
    if (!fd) return F87_ERR_NOMEM;
    fd->seed = 42;
    ctx->effect_data = fd;
    return F87_OK;
}

static void fire_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                         const f87_audio_data_t *audio)
{
    (void)audio;
    fire_data_t *fd = ctx->effect_data;

    /* Speed affects decay: higher speed = less decay = bigger flames */
    float decay_base = 0.15f - (float)ctx->speed * 0.02f;
    if (decay_base < 0.05f) decay_base = 0.05f;

    /* Set bottom row to max heat */
    for (int c = 0; c < FIRE_COLS; c++)
        fd->heat[FIRE_ROWS - 1][c] = 1.0f;

    /* Propagate heat upward with decay and random spread */
    for (int r = 0; r < FIRE_ROWS - 1; r++) {
        for (int c = 0; c < FIRE_COLS; c++) {
            /* Sample from row below with slight horizontal jitter */
            int src_c = c + (int)(f87_effect_rand(&fd->seed) % 3) - 1;
            if (src_c < 0) src_c = 0;
            if (src_c >= FIRE_COLS) src_c = FIRE_COLS - 1;

            float decay = decay_base * (float)(f87_effect_rand(&fd->seed) % 100) / 100.0f;
            fd->heat[r][c] = fd->heat[r + 1][src_c] - decay;
            if (fd->heat[r][c] < 0.0f) fd->heat[r][c] = 0.0f;
        }
    }

    /* Map heat to key colors using the key layout */
    float br_scale = (float)ctx->brightness / 4.0f;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= FIRE_ROWS || col >= FIRE_COLS) continue;

        float h = fd->heat[row][col];

        /* Fire color gradient: black → red → orange → yellow → white */
        float r, g, b;
        if (h < 0.33f) {
            float t = h / 0.33f;
            r = t * (float)ctx->base_color[0];
            g = 0;
            b = 0;
        } else if (h < 0.66f) {
            float t = (h - 0.33f) / 0.33f;
            r = (float)ctx->base_color[0];
            g = t * (float)ctx->base_color[1];
            b = 0;
        } else {
            float t = (h - 0.66f) / 0.34f;
            r = (float)ctx->base_color[0];
            g = (float)ctx->base_color[1];
            b = t * 255.0f;
        }

        frame->keys[k][0] = (uint8_t)(r * br_scale);
        frame->keys[k][1] = (uint8_t)(g * br_scale);
        frame->keys[k][2] = (uint8_t)(b * br_scale);
    }
}

static void fire_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_fire = {
    .name = "Fire",
    .id = F87_SW_FIRE,
    .needs_audio = false,
    .needs_input = false,
    .init = fire_init,
    .render = fire_render,
    .destroy = fire_destroy,
};

/* ===== EFFECT REGISTRY ===== */

static const f87_sw_effect_t *all_effects[] = {
    &effect_fire,
    NULL
};

const f87_sw_effect_t *f87_sw_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; all_effects[i] != NULL; i++) {
        if (all_effects[i]->id == id)
            return all_effects[i];
    }
    return NULL;
}

const char *f87_sw_effect_name(f87_sw_effect_id id)
{
    const f87_sw_effect_t *e = f87_sw_find_effect(id);
    return e ? e->name : "Unknown";
}
```

- [ ] **Step 5: Run tests**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make test_effects_sw && ./test_effects_sw
```

Expected: 5/5 tests passed.

- [ ] **Step 6: Commit**

```bash
git add lib/src/effects_sw.c tests/test_effects_sw.c CMakeLists.txt
git commit -m "feat: add fire effect (Doom fire algorithm) with tests"
```

---

### Task 8: CLI animate/music Commands

**Files:**
- Modify: `cli/src/main.c`

- [ ] **Step 1: Add animate command parsing**

Add to `main.c` after the existing `raw` command handler. Add `#include "f87/animate.h"` at top and a signal handler:

```c
#include <signal.h>

static f87_anim_ctx_t *g_anim_ctx = NULL;

static void signal_handler(int sig)
{
    (void)sig;
    if (g_anim_ctx) {
        f87_anim_stop(g_anim_ctx);
        g_anim_ctx = NULL;
    }
    exit(0);
}

/* Parse software effect name to ID. Returns -1 if not found. */
static int parse_sw_effect(const char *name)
{
    struct { const char *name; f87_sw_effect_id id; } map[] = {
        {"fire",       F87_SW_FIRE},
        {"matrix",     F87_SW_MATRIX},
        {"plasma",     F87_SW_PLASMA},
        {"heatmap",    F87_SW_HEATMAP},
        {"radar",      F87_SW_RADAR},
        {"lightning",  F87_SW_LIGHTNING},
        {"explode",    F87_SW_EXPLODE},
        {"ripple-sw",  F87_SW_RIPPLE},
        {"typewriter", F87_SW_TYPEWRITER},
        {"life",       F87_SW_LIFE},
        {NULL, 0}
    };
    for (int i = 0; map[i].name; i++) {
        if (strcmp(name, map[i].name) == 0)
            return map[i].id;
    }
    return -1;
}

static int parse_music_mode(const char *name)
{
    struct { const char *name; f87_sw_effect_id id; } map[] = {
        {"spectrum",  F87_MU_SPECTRUM},
        {"beat",      F87_MU_BEAT},
        {"energy",    F87_MU_ENERGY},
        {"vu",        F87_MU_VU},
        {"freqmap",   F87_MU_FREQ_MAP},
        {NULL, 0}
    };
    for (int i = 0; map[i].name; i++) {
        if (strcmp(name, map[i].name) == 0)
            return map[i].id;
    }
    return -1;
}

static int cmd_animate(f87_device *dev, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl animate <effect> [RRGGBB] [--speed 0-4] [--brightness 1-4]\n");
        return 1;
    }

    if (strcmp(argv[0], "stop") == 0) {
        /* Stop is handled by signal — nothing to do in non-daemon mode */
        fprintf(stderr, "No animation running\n");
        return 1;
    }

    int effect_id = parse_sw_effect(argv[0]);
    if (effect_id < 0) {
        fprintf(stderr, "Unknown effect: %s\n", argv[0]);
        return 1;
    }

    f87_anim_config_t config = {
        .color = {255, 80, 0},  /* Default: orange */
        .brightness = 3,
        .speed = 2,
        .audio_source = F87_AUDIO_MONITOR,
    };

    /* Parse optional arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            config.speed = (uint8_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--brightness") == 0 && i + 1 < argc) {
            config.brightness = (uint8_t)atoi(argv[++i]);
        } else if (strlen(argv[i]) == 6) {
            f87_color c;
            if (parse_color(argv[i], &c) == 0) {
                config.color[0] = c.r;
                config.color[1] = c.g;
                config.color[2] = c.b;
            }
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_anim_ctx = f87_anim_start(dev, effect_id, &config);
    if (!g_anim_ctx) {
        fprintf(stderr, "Failed to start animation\n");
        return 1;
    }

    /* Block until Ctrl+C */
    while (f87_anim_is_running(g_anim_ctx))
        usleep(100000);  /* 100ms poll */

    int err = f87_anim_get_error(g_anim_ctx);
    f87_anim_stop(g_anim_ctx);
    g_anim_ctx = NULL;

    if (err < 0)
        fprintf(stderr, "Animation error: %s\n", f87_strerror(err));
    return err < 0 ? 1 : 0;
}

static int cmd_music(f87_device *dev, int argc, char **argv)
{
    if (argc < 1) {
        fprintf(stderr, "Usage: f87ctl music <mode> [--source monitor|mic]\n");
        return 1;
    }

    int effect_id = parse_music_mode(argv[0]);
    if (effect_id < 0) {
        fprintf(stderr, "Unknown music mode: %s\n", argv[0]);
        return 1;
    }

    f87_anim_config_t config = {
        .color = {0, 128, 255},
        .brightness = 4,
        .speed = 2,
        .audio_source = F87_AUDIO_MONITOR,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "mic") == 0)
                config.audio_source = F87_AUDIO_MIC;
        } else if (strlen(argv[i]) == 6) {
            f87_color c;
            if (parse_color(argv[i], &c) == 0) {
                config.color[0] = c.r;
                config.color[1] = c.g;
                config.color[2] = c.b;
            }
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    g_anim_ctx = f87_anim_start(dev, effect_id, &config);
    if (!g_anim_ctx) {
        fprintf(stderr, "Failed to start music mode (audio support compiled in?)\n");
        return 1;
    }

    while (f87_anim_is_running(g_anim_ctx))
        usleep(100000);

    int err = f87_anim_get_error(g_anim_ctx);
    f87_anim_stop(g_anim_ctx);
    g_anim_ctx = NULL;

    if (err < 0)
        fprintf(stderr, "Music mode error: %s\n", f87_strerror(err));
    return err < 0 ? 1 : 0;
}
```

Add to the main command dispatch (in the existing if/else chain):

```c
    } else if (strcmp(cmd, "animate") == 0) {
        return cmd_animate(dev, argc - 2, argv + 2);
    } else if (strcmp(cmd, "music") == 0) {
        return cmd_music(dev, argc - 2, argv + 2);
    }
```

- [ ] **Step 2: Build and verify**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make f87ctl
```

Expected: Clean build.

- [ ] **Step 3: Hardware test (with keyboard)**

```bash
./f87ctl animate fire
# Ctrl+C to stop

./f87ctl animate fire ff0000 --speed 4 --brightness 4
# Ctrl+C to stop
```

Expected: Fire animation visible on keyboard, Ctrl+C restores previous effect.

- [ ] **Step 4: Commit**

```bash
git add cli/src/main.c
git commit -m "feat: add CLI animate and music commands"
```

---

### Task 9: Remaining Non-Reactive Software Effects

**Files:**
- Modify: `lib/src/effects_sw.c`

Adds 5 effects: Matrix, Plasma, Heatmap, Radar, Lightning. Each follows the same pattern as Fire.

- [ ] **Step 1: Add Matrix effect**

```c
/* ===== MATRIX EFFECT ===== */
/* Falling green columns at random speeds */

typedef struct {
    float drops[FIRE_COLS];     /* Y position of each raindrop */
    float speeds[FIRE_COLS];    /* Fall speed per column */
    float trail[FIRE_ROWS][FIRE_COLS]; /* Brightness trail */
    uint32_t seed;
} matrix_data_t;

static int matrix_init(f87_effect_ctx_t *ctx)
{
    matrix_data_t *md = calloc(1, sizeof(matrix_data_t));
    if (!md) return F87_ERR_NOMEM;
    md->seed = 12345;

    /* Initialize drops at random positions */
    for (int c = 0; c < FIRE_COLS; c++) {
        md->drops[c] = -(float)(f87_effect_rand(&md->seed) % (FIRE_ROWS * 3));
        md->speeds[c] = 0.1f + (float)(f87_effect_rand(&md->seed) % 50) / 100.0f;
    }

    ctx->effect_data = md;
    return F87_OK;
}

static void matrix_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    (void)audio;
    matrix_data_t *md = ctx->effect_data;
    float speed_mult = 0.5f + (float)ctx->speed * 0.3f;
    float br_scale = (float)ctx->brightness / 4.0f;

    /* Fade existing trails */
    for (int r = 0; r < FIRE_ROWS; r++)
        for (int c = 0; c < FIRE_COLS; c++)
            md->trail[r][c] *= 0.85f;

    /* Advance drops */
    for (int c = 0; c < FIRE_COLS; c++) {
        md->drops[c] += md->speeds[c] * speed_mult;

        int row = (int)md->drops[c];
        if (row >= 0 && row < FIRE_ROWS)
            md->trail[row][c] = 1.0f;

        /* Reset when off screen */
        if (row >= FIRE_ROWS + 3) {
            md->drops[c] = -(float)(f87_effect_rand(&md->seed) % (FIRE_ROWS * 2));
            md->speeds[c] = 0.1f + (float)(f87_effect_rand(&md->seed) % 50) / 100.0f;
        }
    }

    /* Map to keys */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= FIRE_ROWS || col >= FIRE_COLS) continue;

        float v = md->trail[row][col];
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void matrix_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_matrix = {
    .name = "Matrix",
    .id = F87_SW_MATRIX,
    .needs_audio = false,
    .needs_input = false,
    .init = matrix_init,
    .render = matrix_render,
    .destroy = matrix_destroy,
};
```

- [ ] **Step 2: Add Plasma effect**

```c
/* ===== PLASMA EFFECT ===== */
/* Sinusoidal interference pattern creating flowing color plasma */

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int plasma_init(f87_effect_ctx_t *ctx)
{
    ctx->effect_data = NULL;  /* No extra state needed */
    return F87_OK;
}

static void plasma_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    (void)audio;
    float t = (float)ctx->frame_count * (0.02f + (float)ctx->speed * 0.02f);
    float br_scale = (float)ctx->brightness / 4.0f;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float x = (float)f87_key_layout[k].col / 22.0f;
        float y = (float)f87_key_layout[k].row / 6.0f;

        float v1 = sinf(x * 10.0f + t);
        float v2 = sinf(y * 10.0f + t * 0.7f);
        float v3 = sinf((x + y) * 5.0f + t * 1.3f);
        float v4 = sinf(sqrtf(x * x + y * y) * 8.0f - t);

        float v = (v1 + v2 + v3 + v4) / 4.0f;  /* -1 to 1 */
        v = (v + 1.0f) / 2.0f;  /* 0 to 1 */

        /* HSV-like color mapping: cycle through hue */
        float h = v * 6.0f;
        float r, g, b;
        int hi = (int)h % 6;
        float f = h - (float)hi;

        switch (hi) {
            case 0: r = 1; g = f; b = 0; break;
            case 1: r = 1 - f; g = 1; b = 0; break;
            case 2: r = 0; g = 1; b = f; break;
            case 3: r = 0; g = 1 - f; b = 1; break;
            case 4: r = f; g = 0; b = 1; break;
            default: r = 1; g = 0; b = 1 - f; break;
        }

        frame->keys[k][0] = (uint8_t)(r * 255.0f * br_scale);
        frame->keys[k][1] = (uint8_t)(g * 255.0f * br_scale);
        frame->keys[k][2] = (uint8_t)(b * 255.0f * br_scale);
    }
}

static void plasma_destroy(f87_effect_ctx_t *ctx)
{
    (void)ctx;
}

static const f87_sw_effect_t effect_plasma = {
    .name = "Plasma",
    .id = F87_SW_PLASMA,
    .needs_audio = false,
    .needs_input = false,
    .init = plasma_init,
    .render = plasma_render,
    .destroy = plasma_destroy,
};
```

- [ ] **Step 3: Add Heatmap effect**

```c
/* ===== HEATMAP EFFECT ===== */
/* CPU temperature → color mapping (reads /sys/class/thermal) */

typedef struct {
    float current_temp;   /* Smoothed temperature 0.0-1.0 */
} heatmap_data_t;

static float read_cpu_temp(void)
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return 0.5f;
    int millideg;
    if (fscanf(f, "%d", &millideg) != 1) millideg = 50000;
    fclose(f);
    /* Normalize: 30°C=0.0, 90°C=1.0 */
    float t = ((float)millideg / 1000.0f - 30.0f) / 60.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

static int heatmap_init(f87_effect_ctx_t *ctx)
{
    heatmap_data_t *hd = calloc(1, sizeof(heatmap_data_t));
    if (!hd) return F87_ERR_NOMEM;
    hd->current_temp = read_cpu_temp();
    ctx->effect_data = hd;
    return F87_OK;
}

static void heatmap_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                            const f87_audio_data_t *audio)
{
    (void)audio;
    heatmap_data_t *hd = ctx->effect_data;

    /* Read temp every 30 frames (~1 second) */
    if (ctx->frame_count % 30 == 0) {
        float target = read_cpu_temp();
        hd->current_temp += (target - hd->current_temp) * 0.1f;  /* Smooth */
    }

    float t = hd->current_temp;
    float br_scale = (float)ctx->brightness / 4.0f;

    /* Color gradient: blue (cold) → green → yellow → red (hot) */
    float r, g, b;
    if (t < 0.33f) {
        float f = t / 0.33f;
        r = 0; g = f * 255; b = (1 - f) * 255;
    } else if (t < 0.66f) {
        float f = (t - 0.33f) / 0.33f;
        r = f * 255; g = 255; b = 0;
    } else {
        float f = (t - 0.66f) / 0.34f;
        r = 255; g = (1 - f) * 255; b = 0;
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        frame->keys[k][0] = (uint8_t)(r * br_scale);
        frame->keys[k][1] = (uint8_t)(g * br_scale);
        frame->keys[k][2] = (uint8_t)(b * br_scale);
    }
}

static void heatmap_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_heatmap = {
    .name = "Heatmap",
    .id = F87_SW_HEATMAP,
    .needs_audio = false,
    .needs_input = false,
    .init = heatmap_init,
    .render = heatmap_render,
    .destroy = heatmap_destroy,
};
```

- [ ] **Step 4: Add Radar effect**

```c
/* ===== RADAR EFFECT ===== */
/* Rotating beam from center, fading trail */

typedef struct {
    float angle;  /* Current beam angle in radians */
    float trail[F87_KEY_COUNT];  /* Per-key fade trail */
} radar_data_t;

static int radar_init(f87_effect_ctx_t *ctx)
{
    radar_data_t *rd = calloc(1, sizeof(radar_data_t));
    if (!rd) return F87_ERR_NOMEM;
    ctx->effect_data = rd;
    return F87_OK;
}

static void radar_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                          const f87_audio_data_t *audio)
{
    (void)audio;
    radar_data_t *rd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;

    /* Rotate beam */
    float rot_speed = 0.05f + (float)ctx->speed * 0.03f;
    rd->angle += rot_speed;
    if (rd->angle > 2.0f * (float)M_PI) rd->angle -= 2.0f * (float)M_PI;

    /* Fade all trails */
    for (int k = 0; k < F87_KEY_COUNT; k++)
        rd->trail[k] *= 0.92f;

    /* Center of keyboard (approx col 10, row 2.5) */
    float cx = 10.0f, cy = 2.5f;

    /* Light up keys the beam passes over */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float kx = (float)f87_key_layout[k].col - cx;
        float ky = (float)f87_key_layout[k].row - cy;

        float key_angle = atan2f(ky, kx);
        if (key_angle < 0) key_angle += 2.0f * (float)M_PI;

        float diff = fabsf(key_angle - rd->angle);
        if (diff > (float)M_PI) diff = 2.0f * (float)M_PI - diff;

        /* Beam width ~15 degrees */
        if (diff < 0.26f) {
            float intensity = 1.0f - diff / 0.26f;
            if (intensity > rd->trail[k])
                rd->trail[k] = intensity;
        }

        float v = rd->trail[k];
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void radar_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_radar = {
    .name = "Radar",
    .id = F87_SW_RADAR,
    .needs_audio = false,
    .needs_input = false,
    .init = radar_init,
    .render = radar_render,
    .destroy = radar_destroy,
};
```

- [ ] **Step 5: Add Lightning effect**

```c
/* ===== LIGHTNING EFFECT ===== */
/* Random electrical arcs between keys with branching paths */

#define LIGHTNING_MAX_BOLTS 3
#define LIGHTNING_MAX_PATH 12

typedef struct {
    struct {
        int path[LIGHTNING_MAX_PATH];  /* Key IDs in the bolt path */
        int length;
        int age;   /* Frames since bolt started */
    } bolts[LIGHTNING_MAX_BOLTS];
    float glow[F87_KEY_COUNT];
    uint32_t seed;
    int cooldown;
} lightning_data_t;

static int lightning_init(f87_effect_ctx_t *ctx)
{
    lightning_data_t *ld = calloc(1, sizeof(lightning_data_t));
    if (!ld) return F87_ERR_NOMEM;
    ld->seed = 99;
    ctx->effect_data = ld;
    return F87_OK;
}

/* Find a neighbor key (adjacent row/col) */
static int find_neighbor(int key_id, uint32_t *seed)
{
    int row = f87_key_layout[key_id].row;
    int col = f87_key_layout[key_id].col;

    int dr = (int)(f87_effect_rand(seed) % 3) - 1;
    int dc = (int)(f87_effect_rand(seed) % 3) - 1;
    if (dr == 0 && dc == 0) dc = 1;

    int tr = row + dr;
    int tc = col + dc;

    /* Find closest key to target position */
    int best = -1;
    int best_dist = 999;
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int d = abs(f87_key_layout[k].row - tr) + abs(f87_key_layout[k].col - tc);
        if (d < best_dist) {
            best_dist = d;
            best = k;
        }
    }
    return best;
}

static void lightning_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                              const f87_audio_data_t *audio)
{
    (void)audio;
    lightning_data_t *ld = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    int spawn_rate = 10 - ctx->speed * 2;  /* Higher speed = more frequent */
    if (spawn_rate < 2) spawn_rate = 2;

    /* Fade glow */
    for (int k = 0; k < F87_KEY_COUNT; k++)
        ld->glow[k] *= 0.8f;

    /* Age and remove old bolts */
    for (int b = 0; b < LIGHTNING_MAX_BOLTS; b++) {
        if (ld->bolts[b].length > 0) {
            ld->bolts[b].age++;
            if (ld->bolts[b].age > 8)
                ld->bolts[b].length = 0;
        }
    }

    /* Maybe spawn a new bolt */
    ld->cooldown--;
    if (ld->cooldown <= 0) {
        for (int b = 0; b < LIGHTNING_MAX_BOLTS; b++) {
            if (ld->bolts[b].length == 0) {
                int start = f87_effect_rand(&ld->seed) % F87_KEY_COUNT;
                ld->bolts[b].path[0] = start;
                ld->bolts[b].length = 1;
                ld->bolts[b].age = 0;

                int len = 4 + (int)(f87_effect_rand(&ld->seed) % 6);
                int cur = start;
                for (int i = 1; i < len && i < LIGHTNING_MAX_PATH; i++) {
                    cur = find_neighbor(cur, &ld->seed);
                    if (cur < 0) break;
                    ld->bolts[b].path[i] = cur;
                    ld->bolts[b].length++;
                }

                ld->cooldown = spawn_rate;
                break;
            }
        }
    }

    /* Draw active bolts */
    for (int b = 0; b < LIGHTNING_MAX_BOLTS; b++) {
        if (ld->bolts[b].length == 0) continue;
        float intensity = 1.0f - (float)ld->bolts[b].age / 8.0f;
        for (int i = 0; i < ld->bolts[b].length; i++) {
            int k = ld->bolts[b].path[i];
            if (intensity > ld->glow[k])
                ld->glow[k] = intensity;
        }
    }

    /* Render */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float v = ld->glow[k];
        /* Lightning: white core with colored glow */
        float white = v > 0.5f ? (v - 0.5f) * 2.0f : 0.0f;
        frame->keys[k][0] = (uint8_t)((white * 255.0f + (1 - white) * (float)ctx->base_color[0] * v) * br_scale);
        frame->keys[k][1] = (uint8_t)((white * 255.0f + (1 - white) * (float)ctx->base_color[1] * v) * br_scale);
        frame->keys[k][2] = (uint8_t)((white * 255.0f + (1 - white) * (float)ctx->base_color[2] * v) * br_scale);
    }
}

static void lightning_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_lightning = {
    .name = "Lightning",
    .id = F87_SW_LIGHTNING,
    .needs_audio = false,
    .needs_input = false,
    .init = lightning_init,
    .render = lightning_render,
    .destroy = lightning_destroy,
};
```

- [ ] **Step 6: Update effect registry**

```c
static const f87_sw_effect_t *all_effects[] = {
    &effect_fire,
    &effect_matrix,
    &effect_plasma,
    &effect_heatmap,
    &effect_radar,
    &effect_lightning,
    NULL
};
```

- [ ] **Step 7: Build and run tests**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make && make test_effects_sw && ./test_effects_sw
```

Expected: All tests pass. Add quick tests for new effects to test_effects_sw.c (same pattern as fire: find, init/destroy, render no crash).

- [ ] **Step 8: Hardware test**

```bash
./f87ctl animate matrix 00ff00
./f87ctl animate plasma --speed 3
./f87ctl animate radar 00ffff --speed 2
./f87ctl animate lightning ff88ff
./f87ctl animate heatmap
```

- [ ] **Step 9: Commit**

```bash
git add lib/src/effects_sw.c tests/test_effects_sw.c
git commit -m "feat: add matrix, plasma, heatmap, radar, lightning effects"
```

---

### Task 10: Music-Reactive Visualizers

**Files:**
- Create: `lib/src/visualizer.c`

All 5 visualizers: spectrum, beat, energy, VU, freq_map.

- [ ] **Step 1: Implement spectrum visualizer**

```c
/* lib/src/visualizer.c */
#ifdef F87_HAS_AUDIO

#include "animate_internal.h"
#include <string.h>
#include <math.h>

/* ===== SPECTRUM VISUALIZER ===== */
/* Maps 6 frequency bands to keyboard rows (bottom=bass, top=treble) */

typedef struct {
    float smooth_bands[F87_AUDIO_BANDS];  /* Smoothed band levels */
} spectrum_viz_data_t;

static int spectrum_viz_init(f87_effect_ctx_t *ctx)
{
    spectrum_viz_data_t *sd = calloc(1, sizeof(spectrum_viz_data_t));
    if (!sd) return F87_ERR_NOMEM;
    ctx->effect_data = sd;
    return F87_OK;
}

static void spectrum_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                                 const f87_audio_data_t *audio)
{
    spectrum_viz_data_t *sd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;

    /* Smooth band values */
    float smooth = 0.3f + (float)(4 - ctx->speed) * 0.1f;
    if (audio) {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            sd->smooth_bands[b] += (audio->bands[b] - sd->smooth_bands[b]) * smooth;
    } else {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            sd->smooth_bands[b] *= 0.95f;
    }

    /* Map bands to rows: row 5 (bottom) = sub_bass, row 0 (top) = treble */
    /* Each band's level determines how many columns light up (left to right) */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= F87_AUDIO_BANDS) continue;

        int band = F87_AUDIO_BANDS - 1 - row;  /* Invert: bottom=bass */
        float level = sd->smooth_bands[band];

        /* How many columns should be lit (out of ~22) */
        float max_col = level * 22.0f;

        if ((float)col < max_col) {
            float intensity = 1.0f - (float)col / (max_col + 1.0f);
            /* Color per band: bass=red, mid=green, treble=blue */
            float r = 0, g = 0, b = 0;
            switch (band) {
                case 0: r = 1.0f; break;           /* Sub-bass: red */
                case 1: r = 1.0f; g = 0.3f; break; /* Bass: orange */
                case 2: r = 0.8f; g = 0.8f; break; /* Low-mid: yellow */
                case 3: g = 1.0f; break;            /* Mid: green */
                case 4: g = 0.5f; b = 1.0f; break; /* High-mid: cyan */
                case 5: b = 1.0f; break;            /* Treble: blue */
            }
            frame->keys[k][0] = (uint8_t)(r * intensity * 255.0f * br_scale);
            frame->keys[k][1] = (uint8_t)(g * intensity * 255.0f * br_scale);
            frame->keys[k][2] = (uint8_t)(b * intensity * 255.0f * br_scale);
        }
    }
}

static void spectrum_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_spectrum = {
    .name = "Spectrum",
    .id = F87_MU_SPECTRUM,
    .needs_audio = true,
    .needs_input = false,
    .init = spectrum_viz_init,
    .render = spectrum_viz_render,
    .destroy = spectrum_viz_destroy,
};
```

- [ ] **Step 2: Implement beat pulse visualizer**

```c
/* ===== BEAT PULSE VISUALIZER ===== */
/* Full keyboard flash on beat, color shifts with intensity */

typedef struct {
    float flash;       /* Current flash intensity (decays) */
    float hue_offset;  /* Rotating hue for variety */
} beat_viz_data_t;

static int beat_viz_init(f87_effect_ctx_t *ctx)
{
    beat_viz_data_t *bd = calloc(1, sizeof(beat_viz_data_t));
    if (!bd) return F87_ERR_NOMEM;
    ctx->effect_data = bd;
    return F87_OK;
}

static void beat_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                             const f87_audio_data_t *audio)
{
    beat_viz_data_t *bd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float decay = 0.85f - (float)ctx->speed * 0.05f;

    bd->flash *= decay;

    if (audio && audio->beat) {
        bd->flash = audio->beat_intensity;
        bd->hue_offset += 0.15f;
    }

    if (bd->flash < 0.01f) return;  /* All black */

    /* Color: base_color modulated by flash + hue shift */
    float h = bd->hue_offset;
    float r = (sinf(h) * 0.5f + 0.5f) * (float)ctx->base_color[0] +
              (1.0f - sinf(h) * 0.5f - 0.5f) * 255.0f;
    float g = (sinf(h + 2.09f) * 0.5f + 0.5f) * (float)ctx->base_color[1] +
              (1.0f - sinf(h + 2.09f) * 0.5f - 0.5f) * 128.0f;
    float b = (sinf(h + 4.19f) * 0.5f + 0.5f) * (float)ctx->base_color[2] +
              (1.0f - sinf(h + 4.19f) * 0.5f - 0.5f) * 200.0f;

    r *= bd->flash * br_scale;
    g *= bd->flash * br_scale;
    b *= bd->flash * br_scale;

    uint8_t cr = r > 255 ? 255 : (uint8_t)r;
    uint8_t cg = g > 255 ? 255 : (uint8_t)g;
    uint8_t cb = b > 255 ? 255 : (uint8_t)b;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        frame->keys[k][0] = cr;
        frame->keys[k][1] = cg;
        frame->keys[k][2] = cb;
    }
}

static void beat_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_beat = {
    .name = "Beat",
    .id = F87_MU_BEAT,
    .needs_audio = true,
    .needs_input = false,
    .init = beat_viz_init,
    .render = beat_viz_render,
    .destroy = beat_viz_destroy,
};
```

- [ ] **Step 3: Implement energy wave visualizer**

```c
/* ===== ENERGY WAVE VISUALIZER ===== */
/* Waves expanding from center, size proportional to audio energy */

typedef struct {
    float waves[8];    /* Active wave radii */
    float wave_str[8]; /* Wave strengths */
    int wave_idx;
    int cooldown;
} energy_viz_data_t;

static int energy_viz_init(f87_effect_ctx_t *ctx)
{
    energy_viz_data_t *ed = calloc(1, sizeof(energy_viz_data_t));
    if (!ed) return F87_ERR_NOMEM;
    ctx->effect_data = ed;
    return F87_OK;
}

static void energy_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                               const f87_audio_data_t *audio)
{
    energy_viz_data_t *ed = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float expand_speed = 0.3f + (float)ctx->speed * 0.15f;
    float cx = 10.0f, cy = 2.5f;

    /* Spawn wave on beat or high energy */
    ed->cooldown--;
    if (audio && ed->cooldown <= 0 &&
        (audio->beat || audio->energy > 0.4f)) {
        ed->waves[ed->wave_idx % 8] = 0.1f;
        ed->wave_str[ed->wave_idx % 8] = audio->energy;
        ed->wave_idx++;
        ed->cooldown = 3;
    }

    /* Expand and fade waves */
    for (int w = 0; w < 8; w++) {
        if (ed->waves[w] > 0) {
            ed->waves[w] += expand_speed;
            ed->wave_str[w] *= 0.93f;
            if (ed->wave_str[w] < 0.01f)
                ed->waves[w] = 0;
        }
    }

    /* Render */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float kx = (float)f87_key_layout[k].col - cx;
        float ky = ((float)f87_key_layout[k].row - cy) * 3.0f;  /* Scale Y for aspect */
        float dist = sqrtf(kx * kx + ky * ky);

        float v = 0;
        for (int w = 0; w < 8; w++) {
            if (ed->waves[w] <= 0) continue;
            float diff = fabsf(dist - ed->waves[w]);
            if (diff < 1.5f) {
                float ring = (1.0f - diff / 1.5f) * ed->wave_str[w];
                if (ring > v) v = ring;
            }
        }

        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void energy_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_energy = {
    .name = "Energy",
    .id = F87_MU_ENERGY,
    .needs_audio = true,
    .needs_input = false,
    .init = energy_viz_init,
    .render = energy_viz_render,
    .destroy = energy_viz_destroy,
};
```

- [ ] **Step 4: Implement VU meter visualizer**

```c
/* ===== VU METER VISUALIZER ===== */
/* Left-to-right volume bar with green/yellow/red zones */

typedef struct {
    float smooth_level;
    float peak;
    int peak_hold;
} vu_viz_data_t;

static int vu_viz_init(f87_effect_ctx_t *ctx)
{
    vu_viz_data_t *vd = calloc(1, sizeof(vu_viz_data_t));
    if (!vd) return F87_ERR_NOMEM;
    ctx->effect_data = vd;
    return F87_OK;
}

static void vu_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    vu_viz_data_t *vd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float smooth = 0.3f + (float)(4 - ctx->speed) * 0.1f;

    float target = audio ? audio->energy : 0.0f;
    vd->smooth_level += (target - vd->smooth_level) * smooth;

    /* Peak hold */
    if (vd->smooth_level > vd->peak) {
        vd->peak = vd->smooth_level;
        vd->peak_hold = 15;  /* Hold for ~0.5s */
    } else if (vd->peak_hold > 0) {
        vd->peak_hold--;
    } else {
        vd->peak *= 0.97f;
    }

    float level = vd->smooth_level;
    int max_col = 21;  /* Max column index */

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int col = f87_key_layout[k].col;
        float pos = (float)col / (float)max_col;

        if (pos <= level) {
            /* VU color zones: green → yellow → red */
            float r, g, b;
            if (pos < 0.6f) {
                r = 0; g = 255; b = 0;       /* Green */
            } else if (pos < 0.8f) {
                r = 255; g = 255; b = 0;     /* Yellow */
            } else {
                r = 255; g = 0; b = 0;       /* Red */
            }
            frame->keys[k][0] = (uint8_t)(r * br_scale);
            frame->keys[k][1] = (uint8_t)(g * br_scale);
            frame->keys[k][2] = (uint8_t)(b * br_scale);
        } else if (fabsf(pos - vd->peak) < 0.05f && vd->peak > 0.05f) {
            /* Peak indicator: white */
            frame->keys[k][0] = (uint8_t)(255 * br_scale);
            frame->keys[k][1] = (uint8_t)(255 * br_scale);
            frame->keys[k][2] = (uint8_t)(255 * br_scale);
        }
    }
}

static void vu_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_vu = {
    .name = "VU",
    .id = F87_MU_VU,
    .needs_audio = true,
    .needs_input = false,
    .init = vu_viz_init,
    .render = vu_viz_render,
    .destroy = vu_viz_destroy,
};
```

- [ ] **Step 5: Implement frequency map visualizer**

```c
/* ===== FREQ MAP VISUALIZER ===== */
/* Each row colored by its frequency band intensity */

typedef struct {
    float smooth[F87_AUDIO_BANDS];
} freqmap_viz_data_t;

static int freqmap_viz_init(f87_effect_ctx_t *ctx)
{
    freqmap_viz_data_t *fd = calloc(1, sizeof(freqmap_viz_data_t));
    if (!fd) return F87_ERR_NOMEM;
    ctx->effect_data = fd;
    return F87_OK;
}

static void freqmap_viz_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                                const f87_audio_data_t *audio)
{
    freqmap_viz_data_t *fd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float smooth = 0.3f + (float)(4 - ctx->speed) * 0.1f;

    if (audio) {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            fd->smooth[b] += (audio->bands[b] - fd->smooth[b]) * smooth;
    } else {
        for (int b = 0; b < F87_AUDIO_BANDS; b++)
            fd->smooth[b] *= 0.95f;
    }

    /* Each row gets the color of its frequency band */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        if (row >= F87_AUDIO_BANDS) continue;

        int band = F87_AUDIO_BANDS - 1 - row;
        float v = fd->smooth[band];

        /* Use base_color modulated by band intensity */
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void freqmap_viz_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t viz_freqmap = {
    .name = "FreqMap",
    .id = F87_MU_FREQ_MAP,
    .needs_audio = true,
    .needs_input = false,
    .init = freqmap_viz_init,
    .render = freqmap_viz_render,
    .destroy = freqmap_viz_destroy,
};

/* ===== VISUALIZER REGISTRY ===== */
/* Called from effects_sw.c via f87_sw_find_effect fallback */

static const f87_sw_effect_t *viz_effects[] = {
    &viz_spectrum,
    &viz_beat,
    &viz_energy,
    &viz_vu,
    &viz_freqmap,
    NULL
};

const f87_sw_effect_t *f87_viz_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; viz_effects[i] != NULL; i++) {
        if (viz_effects[i]->id == id)
            return viz_effects[i];
    }
    return NULL;
}

#endif /* F87_HAS_AUDIO */
```

- [ ] **Step 6: Update effects_sw.c to chain visualizer lookup**

In `f87_sw_find_effect()`, add fallback after local registry:

```c
const f87_sw_effect_t *f87_sw_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; all_effects[i] != NULL; i++) {
        if (all_effects[i]->id == id)
            return all_effects[i];
    }

#ifdef F87_HAS_AUDIO
    /* Check visualizer registry */
    extern const f87_sw_effect_t *f87_viz_find_effect(f87_sw_effect_id id);
    const f87_sw_effect_t *viz = f87_viz_find_effect(id);
    if (viz) return viz;
#endif

    /* Check reactive effects registry */
    extern const f87_sw_effect_t *f87_reactive_find_effect(f87_sw_effect_id id);
    return f87_reactive_find_effect(id);
}
```

- [ ] **Step 7: Build and test**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make
```

Expected: Clean build.

- [ ] **Step 8: Hardware test with music playing**

```bash
# Play music on system (Spotify, YouTube, etc.) then:
./f87ctl music spectrum
./f87ctl music beat ff0000
./f87ctl music energy 00ff88
./f87ctl music vu
./f87ctl music freqmap 8800ff
```

- [ ] **Step 9: Commit**

```bash
git add lib/src/visualizer.c lib/src/effects_sw.c
git commit -m "feat: add 5 music-reactive visualizers — spectrum, beat, energy, VU, freqmap"
```

---

### Task 11: Reactive Effects with Input Capture

**Files:**
- Modify: `lib/src/effects_sw_reactive.c`
- Modify: `lib/src/animate.c` (add input fd setup)
- Modify: `lib/CMakeLists.txt` (add libevdev optional dependency)

- [ ] **Step 1: Add input polling to animation loop**

In `animate.c`, add input detection to `anim_thread_func()`. Before the render call, add:

```c
#include <poll.h>
#include <linux/input.h>

/* In anim_thread_func, inside the while loop, before render: */
if (ctx->input_fd >= 0) {
    struct pollfd pfd = { .fd = ctx->input_fd, .events = POLLIN };
    while (poll(&pfd, 1, 0) > 0) {
        struct input_event ev;
        if (read(ctx->input_fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                /* Key press — notify active effect via effect_data callback */
                /* Store in a simple key event queue in ctx */
            }
        }
    }
}
```

Add input fd detection to `f87_anim_start()` when `effect->needs_input`:

```c
/* Find keyboard input device */
static int find_keyboard_input(void)
{
    char path[64];
    for (int i = 0; i < 32; i++) {
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* Check if it's a keyboard (has EV_KEY capability) */
        unsigned long evbits = 0;
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) >= 0) {
            if (evbits & (1 << EV_KEY)) {
                /* Check for letter keys to confirm it's a real keyboard */
                unsigned long keybits[KEY_MAX / (sizeof(unsigned long) * 8) + 1] = {0};
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) >= 0) {
                    if (keybits[KEY_A / (sizeof(unsigned long) * 8)] & (1UL << (KEY_A % (sizeof(unsigned long) * 8)))) {
                        return fd;
                    }
                }
            }
        }
        close(fd);
    }
    return -1;
}
```

- [ ] **Step 2: Implement reactive effects in effects_sw_reactive.c**

```c
/* lib/src/effects_sw_reactive.c */
#include "animate_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <linux/input.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Shared: map Linux keycode to F87 key_id (-1 if not found) */
static int keycode_to_key_id(int keycode)
{
    /* Simplified mapping — maps common keycodes to key layout indices.
       Full mapping would use a lookup table from linux/input-event-codes.h */
    static const struct { int keycode; int key_id; } map[] = {
        {KEY_ESC, 0}, {KEY_F1, 1}, {KEY_F2, 2}, {KEY_F3, 3}, {KEY_F4, 4},
        {KEY_F5, 5}, {KEY_F6, 6}, {KEY_F7, 7}, {KEY_F8, 8}, {KEY_F9, 9},
        {KEY_F10, 10}, {KEY_F11, 11}, {KEY_F12, 12},
        {KEY_GRAVE, 15}, {KEY_1, 16}, {KEY_2, 17}, {KEY_3, 18}, {KEY_4, 19},
        {KEY_5, 20}, {KEY_6, 21}, {KEY_7, 22}, {KEY_8, 23}, {KEY_9, 24},
        {KEY_0, 25}, {KEY_MINUS, 26}, {KEY_EQUAL, 27}, {KEY_BACKSPACE, 28},
        {KEY_TAB, 30}, {KEY_Q, 31}, {KEY_W, 32}, {KEY_E, 33}, {KEY_R, 34},
        {KEY_T, 35}, {KEY_Y, 36}, {KEY_U, 37}, {KEY_I, 38}, {KEY_O, 39},
        {KEY_P, 40}, {KEY_LEFTBRACE, 41}, {KEY_RIGHTBRACE, 42}, {KEY_BACKSLASH, 43},
        {KEY_CAPSLOCK, 45}, {KEY_A, 46}, {KEY_S, 47}, {KEY_D, 48}, {KEY_F, 49},
        {KEY_G, 50}, {KEY_H, 51}, {KEY_J, 52}, {KEY_K, 53}, {KEY_L, 54},
        {KEY_SEMICOLON, 55}, {KEY_APOSTROPHE, 56}, {KEY_ENTER, 57},
        {KEY_LEFTSHIFT, 58}, {KEY_Z, 59}, {KEY_X, 60}, {KEY_C, 61},
        {KEY_V, 62}, {KEY_B, 63}, {KEY_N, 64}, {KEY_M, 65},
        {KEY_COMMA, 66}, {KEY_DOT, 67}, {KEY_SLASH, 68}, {KEY_RIGHTSHIFT, 69},
        {KEY_LEFTCTRL, 71}, {KEY_LEFTMETA, 72}, {KEY_LEFTALT, 73},
        {KEY_SPACE, 74}, {KEY_RIGHTALT, 75}, {KEY_FN, 76},
        {KEY_COMPOSE, 77}, {KEY_RIGHTCTRL, 78},
        {-1, -1}
    };

    for (int i = 0; map[i].keycode >= 0; i++) {
        if (map[i].keycode == keycode)
            return map[i].key_id;
    }
    return -1;
}

/* ===== EXPLODE EFFECT ===== */

#define EXPLODE_MAX 8

typedef struct {
    struct {
        int key_id;
        float radius;
        float strength;
        float hue;
    } explosions[EXPLODE_MAX];
    int exp_idx;
    uint32_t seed;
} explode_data_t;

static int explode_init(f87_effect_ctx_t *ctx)
{
    explode_data_t *ed = calloc(1, sizeof(explode_data_t));
    if (!ed) return F87_ERR_NOMEM;
    ed->seed = 777;
    ctx->effect_data = ed;
    return F87_OK;
}

static void explode_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    explode_data_t *ed = ctx->effect_data;
    int idx = ed->exp_idx % EXPLODE_MAX;
    ed->explosions[idx].key_id = key_id;
    ed->explosions[idx].radius = 0.1f;
    ed->explosions[idx].strength = 1.0f;
    ed->explosions[idx].hue = (float)(f87_effect_rand(&ed->seed) % 360) / 360.0f;
    ed->exp_idx++;
}

static void explode_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                            const f87_audio_data_t *audio)
{
    (void)audio;
    explode_data_t *ed = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float expand = 0.4f + (float)ctx->speed * 0.2f;

    /* Expand and fade explosions */
    for (int e = 0; e < EXPLODE_MAX; e++) {
        if (ed->explosions[e].strength <= 0) continue;
        ed->explosions[e].radius += expand;
        ed->explosions[e].strength *= 0.9f;
        if (ed->explosions[e].strength < 0.01f)
            ed->explosions[e].strength = 0;
    }

    /* Render explosions */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float max_v = 0;
        float best_hue = 0;

        for (int e = 0; e < EXPLODE_MAX; e++) {
            if (ed->explosions[e].strength <= 0) continue;

            int src = ed->explosions[e].key_id;
            float dx = (float)(f87_key_layout[k].col - f87_key_layout[src].col);
            float dy = (float)(f87_key_layout[k].row - f87_key_layout[src].row) * 2.0f;
            float dist = sqrtf(dx * dx + dy * dy);

            float ring_dist = fabsf(dist - ed->explosions[e].radius);
            if (ring_dist < 1.5f) {
                float v = (1.0f - ring_dist / 1.5f) * ed->explosions[e].strength;
                if (v > max_v) {
                    max_v = v;
                    best_hue = ed->explosions[e].hue;
                }
            }
        }

        if (max_v > 0) {
            /* HSV to RGB with the explosion's hue */
            float h = best_hue * 6.0f;
            int hi = (int)h % 6;
            float f = h - (float)hi;
            float r = 0, g = 0, b = 0;
            switch (hi) {
                case 0: r = 1; g = f; b = 0; break;
                case 1: r = 1 - f; g = 1; b = 0; break;
                case 2: r = 0; g = 1; b = f; break;
                case 3: r = 0; g = 1 - f; b = 1; break;
                case 4: r = f; g = 0; b = 1; break;
                default: r = 1; g = 0; b = 1 - f; break;
            }
            frame->keys[k][0] = (uint8_t)(r * max_v * 255.0f * br_scale);
            frame->keys[k][1] = (uint8_t)(g * max_v * 255.0f * br_scale);
            frame->keys[k][2] = (uint8_t)(b * max_v * 255.0f * br_scale);
        }
    }
}

static void explode_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_explode = {
    .name = "Explode",
    .id = F87_SW_EXPLODE,
    .needs_audio = false,
    .needs_input = true,
    .init = explode_init,
    .render = explode_render,
    .destroy = explode_destroy,
};

/* ===== RIPPLE EFFECT ===== */

#define RIPPLE_MAX 12

typedef struct {
    struct { int key_id; float radius; float strength; } waves[RIPPLE_MAX];
    int wave_idx;
} ripple_data_t;

static int ripple_init(f87_effect_ctx_t *ctx)
{
    ripple_data_t *rd = calloc(1, sizeof(ripple_data_t));
    if (!rd) return F87_ERR_NOMEM;
    ctx->effect_data = rd;
    return F87_OK;
}

static void ripple_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    ripple_data_t *rd = ctx->effect_data;
    int idx = rd->wave_idx % RIPPLE_MAX;
    rd->waves[idx].key_id = key_id;
    rd->waves[idx].radius = 0.1f;
    rd->waves[idx].strength = 1.0f;
    rd->wave_idx++;
}

static void ripple_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                           const f87_audio_data_t *audio)
{
    (void)audio;
    ripple_data_t *rd = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float expand = 0.3f + (float)ctx->speed * 0.15f;

    for (int w = 0; w < RIPPLE_MAX; w++) {
        if (rd->waves[w].strength <= 0) continue;
        rd->waves[w].radius += expand;
        rd->waves[w].strength *= 0.92f;
        if (rd->waves[w].strength < 0.01f)
            rd->waves[w].strength = 0;
    }

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        float total = 0;
        for (int w = 0; w < RIPPLE_MAX; w++) {
            if (rd->waves[w].strength <= 0) continue;
            int src = rd->waves[w].key_id;
            float dx = (float)(f87_key_layout[k].col - f87_key_layout[src].col);
            float dy = (float)(f87_key_layout[k].row - f87_key_layout[src].row) * 2.0f;
            float dist = sqrtf(dx * dx + dy * dy);

            /* Sine wave interference */
            float wave = sinf(dist * 2.0f - rd->waves[w].radius * 3.0f);
            wave = (wave + 1.0f) / 2.0f;

            float falloff = 1.0f - dist / (rd->waves[w].radius + 1.0f);
            if (falloff < 0) falloff = 0;

            total += wave * falloff * rd->waves[w].strength;
        }
        if (total > 1.0f) total = 1.0f;

        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * total * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * total * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * total * br_scale);
    }
}

static void ripple_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_ripple = {
    .name = "Ripple",
    .id = F87_SW_RIPPLE,
    .needs_audio = false,
    .needs_input = true,
    .init = ripple_init,
    .render = ripple_render,
    .destroy = ripple_destroy,
};

/* ===== TYPEWRITER EFFECT ===== */

typedef struct {
    float heat[F87_KEY_COUNT];  /* Per-key heat (1.0 on press, decays) */
} typewriter_data_t;

static int typewriter_init(f87_effect_ctx_t *ctx)
{
    typewriter_data_t *td = calloc(1, sizeof(typewriter_data_t));
    if (!td) return F87_ERR_NOMEM;
    ctx->effect_data = td;
    return F87_OK;
}

static void typewriter_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    typewriter_data_t *td = ctx->effect_data;
    td->heat[key_id] = 1.0f;
}

static void typewriter_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                               const f87_audio_data_t *audio)
{
    (void)audio;
    typewriter_data_t *td = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;
    float decay = 0.96f - (float)ctx->speed * 0.01f;

    for (int k = 0; k < F87_KEY_COUNT; k++) {
        td->heat[k] *= decay;

        float h = td->heat[k];
        if (h < 0.01f) continue;

        /* Hot=white/yellow, cooling=orange, cold=red, off=black */
        float r, g, b;
        if (h > 0.7f) {
            r = 255; g = 255; b = (h - 0.7f) / 0.3f * 200.0f;
        } else if (h > 0.3f) {
            r = 255; g = (h - 0.3f) / 0.4f * 200.0f; b = 0;
        } else {
            r = h / 0.3f * 255.0f; g = 0; b = 0;
        }

        frame->keys[k][0] = (uint8_t)(r * br_scale);
        frame->keys[k][1] = (uint8_t)(g * br_scale);
        frame->keys[k][2] = (uint8_t)(b * br_scale);
    }
}

static void typewriter_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_typewriter = {
    .name = "Typewriter",
    .id = F87_SW_TYPEWRITER,
    .needs_audio = false,
    .needs_input = true,
    .init = typewriter_init,
    .render = typewriter_render,
    .destroy = typewriter_destroy,
};

/* ===== GAME OF LIFE EFFECT ===== */

#define LIFE_ROWS 6
#define LIFE_COLS 22

typedef struct {
    uint8_t grid[LIFE_ROWS][LIFE_COLS];
    uint8_t next[LIFE_ROWS][LIFE_COLS];
    float brightness_map[LIFE_ROWS][LIFE_COLS];
    int step_counter;
} life_data_t;

static int life_init(f87_effect_ctx_t *ctx)
{
    life_data_t *ld = calloc(1, sizeof(life_data_t));
    if (!ld) return F87_ERR_NOMEM;
    ctx->effect_data = ld;
    return F87_OK;
}

static void life_on_key(f87_effect_ctx_t *ctx, int key_id)
{
    life_data_t *ld = ctx->effect_data;
    int row = f87_key_layout[key_id].row;
    int col = f87_key_layout[key_id].col;
    if (row < LIFE_ROWS && col < LIFE_COLS) {
        ld->grid[row][col] = 1;
        ld->brightness_map[row][col] = 1.0f;
    }
}

static int count_neighbors(life_data_t *ld, int r, int c)
{
    int count = 0;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0) continue;
            int nr = r + dr, nc = c + dc;
            if (nr >= 0 && nr < LIFE_ROWS && nc >= 0 && nc < LIFE_COLS)
                count += ld->grid[nr][nc];
        }
    }
    return count;
}

static void life_render(f87_effect_ctx_t *ctx, f87_frame_t *frame,
                         const f87_audio_data_t *audio)
{
    (void)audio;
    life_data_t *ld = ctx->effect_data;
    float br_scale = (float)ctx->brightness / 4.0f;

    /* Step every N frames based on speed */
    int step_interval = 15 - ctx->speed * 3;
    if (step_interval < 3) step_interval = 3;

    ld->step_counter++;
    if (ld->step_counter >= step_interval) {
        ld->step_counter = 0;

        /* Compute next generation */
        for (int r = 0; r < LIFE_ROWS; r++) {
            for (int c = 0; c < LIFE_COLS; c++) {
                int n = count_neighbors(ld, r, c);
                if (ld->grid[r][c]) {
                    ld->next[r][c] = (n == 2 || n == 3) ? 1 : 0;
                } else {
                    ld->next[r][c] = (n == 3) ? 1 : 0;
                }
            }
        }

        /* Swap grids */
        memcpy(ld->grid, ld->next, sizeof(ld->grid));
    }

    /* Update brightness with smooth fade */
    for (int r = 0; r < LIFE_ROWS; r++) {
        for (int c = 0; c < LIFE_COLS; c++) {
            float target = ld->grid[r][c] ? 1.0f : 0.0f;
            ld->brightness_map[r][c] += (target - ld->brightness_map[r][c]) * 0.3f;
        }
    }

    /* Render */
    for (int k = 0; k < F87_KEY_COUNT; k++) {
        int row = f87_key_layout[k].row;
        int col = f87_key_layout[k].col;
        if (row >= LIFE_ROWS || col >= LIFE_COLS) continue;

        float v = ld->brightness_map[row][col];
        frame->keys[k][0] = (uint8_t)((float)ctx->base_color[0] * v * br_scale);
        frame->keys[k][1] = (uint8_t)((float)ctx->base_color[1] * v * br_scale);
        frame->keys[k][2] = (uint8_t)((float)ctx->base_color[2] * v * br_scale);
    }
}

static void life_destroy(f87_effect_ctx_t *ctx)
{
    free(ctx->effect_data);
    ctx->effect_data = NULL;
}

static const f87_sw_effect_t effect_life = {
    .name = "Life",
    .id = F87_SW_LIFE,
    .needs_audio = false,
    .needs_input = true,
    .init = life_init,
    .render = life_render,
    .destroy = life_destroy,
};

/* ===== REACTIVE REGISTRY ===== */

static const f87_sw_effect_t *reactive_effects[] = {
    &effect_explode,
    &effect_ripple,
    &effect_typewriter,
    &effect_life,
    NULL
};

const f87_sw_effect_t *f87_reactive_find_effect(f87_sw_effect_id id)
{
    for (int i = 0; reactive_effects[i] != NULL; i++) {
        if (reactive_effects[i]->id == id)
            return reactive_effects[i];
    }
    return NULL;
}
```

Note: The `f87_effect_rand()` function is defined in effects_sw.c. Either make it non-static and declare in animate_internal.h, or duplicate it as a static in effects_sw_reactive.c.

- [ ] **Step 3: Add key event dispatch to animation loop**

In animate.c, add a key event dispatch mechanism. In the input polling section, call the effect's `on_key` handler. The simplest approach: store a function pointer in animate_internal.h:

Add to `f87_sw_effect_t`:

```c
void (*on_key)(f87_effect_ctx_t *ctx, int key_id);  /* NULL if not reactive */
```

Set `on_key` for each reactive effect (explode_on_key, ripple_on_key, typewriter_on_key, life_on_key).

In the animation loop's input section:

```c
if (ctx->input_fd >= 0 && ctx->active_effect->on_key) {
    struct pollfd pfd = { .fd = ctx->input_fd, .events = POLLIN };
    while (poll(&pfd, 1, 0) > 0) {
        struct input_event ev;
        if (read(ctx->input_fd, &ev, sizeof(ev)) == sizeof(ev)) {
            if (ev.type == EV_KEY && ev.value == 1) {
                int key_id = keycode_to_key_id(ev.code);
                if (key_id >= 0)
                    ctx->active_effect->on_key(&ctx->effect_ctx, key_id);
            }
        }
    }
}
```

- [ ] **Step 4: Build and test**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make
```

- [ ] **Step 5: Hardware test**

```bash
./f87ctl animate explode
# Type on keyboard — should see colored explosions

./f87ctl animate typewriter
# Type — keys glow hot and fade

./f87ctl animate life 00ff00
# Press keys to seed cells, watch Conway's Game of Life evolve
```

- [ ] **Step 6: Commit**

```bash
git add lib/src/effects_sw_reactive.c lib/src/animate.c lib/src/animate_internal.h
git commit -m "feat: add 4 reactive effects — explode, ripple, typewriter, game of life"
```

---

### Task 12: Update CLAUDE.md and Final Integration Test

**Files:**
- Modify: `CLAUDE.md`
- Modify: `tests/test_effects_sw.c` (add tests for all effects)

- [ ] **Step 1: Add tests for all non-reactive effects**

In `test_effects_sw.c`, add tests for each effect using the same pattern (find, init/destroy, render no crash, produces output). Use a helper macro:

```c
#define TEST_EFFECT(ID, NAME) \
static int test_find_##NAME(void) { \
    const f87_sw_effect_t *e = f87_sw_find_effect(ID); \
    return e != NULL && e->id == ID; \
} \
static int test_render_##NAME(void) { \
    const f87_sw_effect_t *e = f87_sw_find_effect(ID); \
    if (!e) return 0; \
    f87_effect_ctx_t ctx = make_ctx(); \
    if (e->init && e->init(&ctx) < 0) return 0; \
    f87_frame_t frame; \
    memset(&frame, 0, sizeof(frame)); \
    for (int i = 0; i < 60; i++) { \
        ctx.frame_count = (uint64_t)i; \
        e->render(&ctx, &frame, NULL); \
    } \
    if (e->destroy) e->destroy(&ctx); \
    return 1; \
}

TEST_EFFECT(F87_SW_FIRE, fire)
TEST_EFFECT(F87_SW_MATRIX, matrix)
TEST_EFFECT(F87_SW_PLASMA, plasma)
TEST_EFFECT(F87_SW_HEATMAP, heatmap)
TEST_EFFECT(F87_SW_RADAR, radar)
TEST_EFFECT(F87_SW_LIGHTNING, lightning)
TEST_EFFECT(F87_SW_EXPLODE, explode)
TEST_EFFECT(F87_SW_RIPPLE, ripple)
TEST_EFFECT(F87_SW_TYPEWRITER, typewriter)
TEST_EFFECT(F87_SW_LIFE, life)
```

- [ ] **Step 2: Run full test suite**

```bash
cd build && cmake .. -DBUILD_GUI=OFF && make && ctest --output-on-failure
```

Expected: All tests pass (device, ring_buffer, spectrum, effects_sw).

- [ ] **Step 3: Update CLAUDE.md**

Add to the Architecture section:

```markdown
- **animate** (`lib/src/`) — software animation engine: thread management, frame loop, effect dispatch
- **audio/spectrum** (`lib/src/`) — PulseAudio capture, KissFFT analysis, beat detection (optional, BUILD_AUDIO)
```

Add to Key Files:

```markdown
- `lib/src/animate.c` — animation thread core, frame loop, input capture
- `lib/src/animate_internal.h` — internal types: effect interface, context, frame buffer
- `lib/src/ring_buffer.c` — lock-free SPSC ring buffer for audio data
- `lib/src/effects_sw.c` — 6 non-reactive software effects
- `lib/src/effects_sw_reactive.c` — 4 reactive software effects
- `lib/src/audio.c` — PulseAudio capture thread
- `lib/src/spectrum.c` — KissFFT FFT, band grouping, beat detection
- `lib/src/visualizer.c` — 5 music-reactive visualizers
- `lib/include/f87/animate.h` — animation public API
- `lib/include/f87/audio_types.h` — audio data types
```

Update Project Status:

```markdown
- Faz 0-3: Complete (lib + CLI + protocol + hardware testing)
- Faz 3.5: Software effects + music-reactive lighting (complete)
- Faz 4: Sensor integration — extended heatmap (CPU/GPU already in SW effects)
- Faz 5: GTK4 GUI (not started)
- Faz 6: Daemon mode, profiles, wireless support (not started)
```

Update Build section:

```markdown
sudo apt install libusb-1.0-0-dev libjson-c-dev libpulse-dev cmake build-essential
```

Update Testing section:

```markdown
# Software animation test
./f87ctl animate fire
./f87ctl animate plasma --speed 3
./f87ctl music spectrum --source monitor
```

- [ ] **Step 4: Run full hardware integration test**

```bash
# Test each effect category
./f87ctl animate fire ff4400 --speed 3
./f87ctl animate matrix 00ff00
./f87ctl animate plasma --speed 4
./f87ctl animate radar 00ffff
./f87ctl animate lightning 8800ff
./f87ctl animate heatmap
./f87ctl animate explode
./f87ctl animate typewriter
./f87ctl animate life 00ff00

# Music reactive (play music first)
./f87ctl music spectrum
./f87ctl music beat ff0000
./f87ctl music energy 00ff88
./f87ctl music vu
./f87ctl music freqmap 8800ff
```

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md tests/test_effects_sw.c
git commit -m "docs: update CLAUDE.md with animation engine, add comprehensive effect tests"
```
