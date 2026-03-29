# Error Collection System Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add centralized 5-level logging (TRACE/DEBUG/INFO/WARN/ERROR) with systemd journal + stderr backends, a 128-entry daemon error ring buffer queryable over D-Bus, and color-coded GUI status bar.

**Architecture:** New `logger.c` module in libf87 provides the log API used by all layers. Daemon initializes journal backend and registers a ring buffer callback. CLI/GUI use stderr backend. D-Bus exposes error history query and runtime log level control.

**Tech Stack:** C11, libsystemd (sd_journal_send), pthreads (mutex), sd-bus, GTK4/libadwaita (CSS)

---

### Task 1: Logger Header and Test

**Files:**
- Create: `lib/include/f87/logger.h`
- Create: `tests/test_logger.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the logger public header**

```c
/* lib/include/f87/logger.h */
#ifndef F87_LOGGER_H
#define F87_LOGGER_H

#include <stdarg.h>

/* Log levels */
#define F87_LOG_TRACE  0
#define F87_LOG_DEBUG  1
#define F87_LOG_INFO   2
#define F87_LOG_WARN   3
#define F87_LOG_ERROR  4

/* Log sources */
typedef enum {
    F87_SRC_USB,
    F87_SRC_AUDIO,
    F87_SRC_DBUS,
    F87_SRC_DEVICE,
    F87_SRC_EFFECT,
    F87_SRC_GUI
} f87_log_source_t;

/* Backend types */
typedef enum {
    F87_LOG_STDERR,
    F87_LOG_JOURNAL
} f87_log_backend_t;

/* Log entry (used by ring buffer callback) */
typedef struct {
    uint64_t timestamp_us;
    int      level;
    int      source;
    int      error_code;
    char     message[256];
} f87_log_entry_t;

/* Callback type for log consumers (daemon ring buffer) */
typedef void (*f87_log_callback_t)(const f87_log_entry_t *entry, void *userdata);

/* Init / shutdown */
int  f87_log_init(f87_log_backend_t backend);
void f87_log_shutdown(void);

/* Level control */
void f87_log_set_level(int level);
int  f87_log_get_level(void);
int  f87_log_level_from_string(const char *str);
const char *f87_log_level_to_string(int level);
const char *f87_log_source_to_string(int source);

/* Register callback (called for WARN and ERROR entries) */
void f87_log_set_callback(f87_log_callback_t cb, void *userdata);

/* Core log function */
void f87_log(int level, int source, int error_code,
             const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* Convenience macros */
#define F87_TRACE(src, fmt, ...) \
    f87_log(F87_LOG_TRACE, (src), 0, fmt, ##__VA_ARGS__)
#define F87_DEBUG(src, fmt, ...) \
    f87_log(F87_LOG_DEBUG, (src), 0, fmt, ##__VA_ARGS__)
#define F87_INFO(src, fmt, ...)  \
    f87_log(F87_LOG_INFO,  (src), 0, fmt, ##__VA_ARGS__)
#define F87_WARN(src, fmt, ...)  \
    f87_log(F87_LOG_WARN,  (src), 0, fmt, ##__VA_ARGS__)
#define F87_ERROR(src, fmt, ...) \
    f87_log(F87_LOG_ERROR, (src), 0, fmt, ##__VA_ARGS__)

/* Error-code variants */
#define F87_WARN_EC(src, ec, fmt, ...) \
    f87_log(F87_LOG_WARN,  (src), (ec), fmt, ##__VA_ARGS__)
#define F87_ERROR_EC(src, ec, fmt, ...) \
    f87_log(F87_LOG_ERROR, (src), (ec), fmt, ##__VA_ARGS__)

#endif /* F87_LOGGER_H */
```

- [ ] **Step 2: Write the test file**

```c
/* tests/test_logger.c */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../lib/include/f87/logger.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_level_from_string(void)
{
    return f87_log_level_from_string("trace") == F87_LOG_TRACE &&
           f87_log_level_from_string("DEBUG") == F87_LOG_DEBUG &&
           f87_log_level_from_string("Info")  == F87_LOG_INFO &&
           f87_log_level_from_string("WARN")  == F87_LOG_WARN &&
           f87_log_level_from_string("error") == F87_LOG_ERROR &&
           f87_log_level_from_string("bogus") == -1;
}

static int test_level_to_string(void)
{
    return strcmp(f87_log_level_to_string(F87_LOG_TRACE), "TRACE") == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_DEBUG), "DEBUG") == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_INFO),  "INFO")  == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_WARN),  "WARN")  == 0 &&
           strcmp(f87_log_level_to_string(F87_LOG_ERROR), "ERROR") == 0 &&
           strcmp(f87_log_level_to_string(99), "UNKNOWN") == 0;
}

static int test_source_to_string(void)
{
    return strcmp(f87_log_source_to_string(F87_SRC_USB),    "USB")    == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_AUDIO),  "AUDIO")  == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_DBUS),   "DBUS")   == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_DEVICE), "DEVICE") == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_EFFECT), "EFFECT") == 0 &&
           strcmp(f87_log_source_to_string(F87_SRC_GUI),    "GUI")    == 0;
}

static int test_set_get_level(void)
{
    f87_log_init(F87_LOG_STDERR);
    f87_log_set_level(F87_LOG_WARN);
    int ok = f87_log_get_level() == F87_LOG_WARN;
    f87_log_set_level(F87_LOG_TRACE);
    ok = ok && f87_log_get_level() == F87_LOG_TRACE;
    f87_log_shutdown();
    return ok;
}

static f87_log_entry_t last_cb_entry;
static int cb_count;

static void test_callback(const f87_log_entry_t *entry, void *userdata)
{
    (void)userdata;
    memcpy(&last_cb_entry, entry, sizeof(*entry));
    cb_count++;
}

static int test_callback_fires_on_warn_error(void)
{
    f87_log_init(F87_LOG_STDERR);
    f87_log_set_level(F87_LOG_TRACE);
    cb_count = 0;
    memset(&last_cb_entry, 0, sizeof(last_cb_entry));
    f87_log_set_callback(test_callback, NULL);

    /* DEBUG should NOT fire callback */
    f87_log(F87_LOG_DEBUG, F87_SRC_USB, 0, "debug msg");
    int ok = cb_count == 0;

    /* WARN should fire callback */
    f87_log(F87_LOG_WARN, F87_SRC_AUDIO, -5, "warn msg %d", 42);
    ok = ok && cb_count == 1;
    ok = ok && last_cb_entry.level == F87_LOG_WARN;
    ok = ok && last_cb_entry.source == F87_SRC_AUDIO;
    ok = ok && last_cb_entry.error_code == -5;
    ok = ok && strstr(last_cb_entry.message, "warn msg 42") != NULL;

    /* ERROR should fire callback */
    f87_log(F87_LOG_ERROR, F87_SRC_DEVICE, -3, "error");
    ok = ok && cb_count == 2;
    ok = ok && last_cb_entry.level == F87_LOG_ERROR;

    f87_log_set_callback(NULL, NULL);
    f87_log_shutdown();
    return ok;
}

static int test_level_filtering(void)
{
    f87_log_init(F87_LOG_STDERR);
    f87_log_set_level(F87_LOG_ERROR);
    cb_count = 0;
    f87_log_set_callback(test_callback, NULL);

    /* WARN is below ERROR level — callback should NOT fire
     * because the message is filtered before reaching callback */
    f87_log(F87_LOG_WARN, F87_SRC_USB, 0, "filtered");
    int ok = cb_count == 0;

    /* ERROR should fire */
    f87_log(F87_LOG_ERROR, F87_SRC_USB, 0, "not filtered");
    ok = ok && cb_count == 1;

    f87_log_set_callback(NULL, NULL);
    f87_log_shutdown();
    return ok;
}

int main(void)
{
    printf("Logger tests:\n");
    TEST(level_from_string);
    TEST(level_to_string);
    TEST(source_to_string);
    TEST(set_get_level);
    TEST(callback_fires_on_warn_error);
    TEST(level_filtering);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
```

- [ ] **Step 3: Add test to CMakeLists.txt**

In the root `CMakeLists.txt`, add after the existing test_profile block (around line 50):

```cmake
add_executable(test_logger tests/test_logger.c lib/src/logger.c)
target_include_directories(test_logger PRIVATE lib/src lib/include)
pkg_check_modules(SYSTEMD_TEST libsystemd)
if(SYSTEMD_TEST_FOUND)
    target_include_directories(test_logger PRIVATE ${SYSTEMD_TEST_INCLUDE_DIRS})
    target_link_libraries(test_logger PRIVATE ${SYSTEMD_TEST_LIBRARIES})
    target_compile_definitions(test_logger PRIVATE F87_HAS_JOURNAL=1)
endif()
add_test(NAME logger_tests COMMAND test_logger)
```

- [ ] **Step 4: Run tests to verify they fail**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make test_logger 2>&1`
Expected: FAIL — `logger.c` does not exist yet.

- [ ] **Step 5: Commit**

```bash
git add lib/include/f87/logger.h tests/test_logger.c CMakeLists.txt
git commit -m "feat(logger): add logger header and tests (red)"
```

---

### Task 2: Logger Implementation (stderr backend)

**Files:**
- Create: `lib/src/logger.c`
- Modify: `lib/CMakeLists.txt`

- [ ] **Step 1: Write the logger implementation**

```c
/* lib/src/logger.c */
#include "f87/logger.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <time.h>
#include <strings.h>  /* strcasecmp */

#ifdef F87_HAS_JOURNAL
#include <systemd/sd-journal.h>
#endif

static atomic_int g_level = F87_LOG_INFO;
static f87_log_backend_t g_backend = F87_LOG_STDERR;
static f87_log_callback_t g_callback = NULL;
static void *g_callback_data = NULL;

int f87_log_init(f87_log_backend_t backend)
{
    g_backend = backend;

    /* Check environment variable for initial level */
    const char *env = getenv("F87_LOG_LEVEL");
    if (env) {
        int lvl = f87_log_level_from_string(env);
        if (lvl >= 0)
            atomic_store(&g_level, lvl);
    }

    return 0;
}

void f87_log_shutdown(void)
{
    g_callback = NULL;
    g_callback_data = NULL;
}

void f87_log_set_level(int level)
{
    if (level >= F87_LOG_TRACE && level <= F87_LOG_ERROR)
        atomic_store(&g_level, level);
}

int f87_log_get_level(void)
{
    return atomic_load(&g_level);
}

int f87_log_level_from_string(const char *str)
{
    if (!str) return -1;
    if (strcasecmp(str, "trace") == 0) return F87_LOG_TRACE;
    if (strcasecmp(str, "debug") == 0) return F87_LOG_DEBUG;
    if (strcasecmp(str, "info")  == 0) return F87_LOG_INFO;
    if (strcasecmp(str, "warn")  == 0) return F87_LOG_WARN;
    if (strcasecmp(str, "error") == 0) return F87_LOG_ERROR;
    return -1;
}

const char *f87_log_level_to_string(int level)
{
    switch (level) {
    case F87_LOG_TRACE: return "TRACE";
    case F87_LOG_DEBUG: return "DEBUG";
    case F87_LOG_INFO:  return "INFO";
    case F87_LOG_WARN:  return "WARN";
    case F87_LOG_ERROR: return "ERROR";
    default:            return "UNKNOWN";
    }
}

const char *f87_log_source_to_string(int source)
{
    switch (source) {
    case F87_SRC_USB:    return "USB";
    case F87_SRC_AUDIO:  return "AUDIO";
    case F87_SRC_DBUS:   return "DBUS";
    case F87_SRC_DEVICE: return "DEVICE";
    case F87_SRC_EFFECT: return "EFFECT";
    case F87_SRC_GUI:    return "GUI";
    default:             return "UNKNOWN";
    }
}

void f87_log_set_callback(f87_log_callback_t cb, void *userdata)
{
    g_callback = cb;
    g_callback_data = userdata;
}

static uint64_t log_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void f87_log(int level, int source, int error_code,
             const char *fmt, ...)
{
    if (level < atomic_load(&g_level))
        return;

    char message[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    /* Write to backend */
    if (g_backend == F87_LOG_STDERR) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm;
        localtime_r(&ts.tv_sec, &tm);

        if (error_code != 0) {
            fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] [%s] (%d) %s\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    ts.tv_nsec / 1000000L,
                    f87_log_level_to_string(level),
                    f87_log_source_to_string(source),
                    error_code, message);
        } else {
            fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] [%s] %s\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    ts.tv_nsec / 1000000L,
                    f87_log_level_to_string(level),
                    f87_log_source_to_string(source),
                    message);
        }
    }
#ifdef F87_HAS_JOURNAL
    else if (g_backend == F87_LOG_JOURNAL) {
        /* Map log level to syslog priority */
        int priority;
        switch (level) {
        case F87_LOG_TRACE: priority = 7; break; /* debug */
        case F87_LOG_DEBUG: priority = 7; break; /* debug */
        case F87_LOG_INFO:  priority = 6; break; /* info */
        case F87_LOG_WARN:  priority = 4; break; /* warning */
        case F87_LOG_ERROR: priority = 3; break; /* err */
        default:            priority = 6; break;
        }

        sd_journal_send(
            "MESSAGE=%s", message,
            "PRIORITY=%d", priority,
            "F87_LEVEL=%s", f87_log_level_to_string(level),
            "F87_SOURCE=%s", f87_log_source_to_string(source),
            "F87_ERROR_CODE=%d", error_code,
            NULL);
    }
#endif

    /* Fire callback for WARN and ERROR */
    if (level >= F87_LOG_WARN && g_callback) {
        f87_log_entry_t entry;
        entry.timestamp_us = log_time_us();
        entry.level = level;
        entry.source = source;
        entry.error_code = error_code;
        strncpy(entry.message, message, sizeof(entry.message) - 1);
        entry.message[sizeof(entry.message) - 1] = '\0';
        g_callback(&entry, g_callback_data);
    }
}
```

- [ ] **Step 2: Add logger.c to lib/CMakeLists.txt**

In `lib/CMakeLists.txt`, add `src/logger.c` to the `add_library(f87 SHARED ...)` list, after `src/client.c`:

```
    src/client.c
    src/logger.c
```

Also add `F87_HAS_JOURNAL=1` compile definition since libsystemd is already linked:

After the existing `target_compile_options` block, add:

```cmake
target_compile_definitions(f87 PRIVATE F87_HAS_JOURNAL=1)
```

- [ ] **Step 3: Build and run tests**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make test_logger && ./test_logger`
Expected: All 6 tests PASS.

- [ ] **Step 4: Run full test suite**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make && ctest --output-on-failure`
Expected: All existing tests still pass.

- [ ] **Step 5: Commit**

```bash
git add lib/src/logger.c lib/CMakeLists.txt
git commit -m "feat(logger): implement logger with stderr and journal backends"
```

---

### Task 3: Error Ring Buffer (Daemon)

**Files:**
- Create: `daemon/src/error_history.h`
- Create: `daemon/src/error_history.c`
- Create: `tests/test_error_history.c`
- Modify: `daemon/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write error_history header**

```c
/* daemon/src/error_history.h */
#ifndef F87D_ERROR_HISTORY_H
#define F87D_ERROR_HISTORY_H

#include <f87/logger.h>
#include <pthread.h>
#include <stdint.h>

#define F87D_ERROR_RING_SIZE 128

typedef struct {
    f87_log_entry_t entries[F87D_ERROR_RING_SIZE];
    int head;
    int count;
    pthread_mutex_t lock;
} f87d_error_ring_t;

void f87d_error_ring_init(f87d_error_ring_t *ring);
void f87d_error_ring_destroy(f87d_error_ring_t *ring);

/* Insert an entry (thread-safe) */
void f87d_error_ring_push(f87d_error_ring_t *ring, const f87_log_entry_t *entry);

/* Copy up to F87D_ERROR_RING_SIZE entries into out[]. Returns count. */
int f87d_error_ring_get_all(f87d_error_ring_t *ring,
                             f87_log_entry_t *out, int max_out);

/* Clear all entries */
void f87d_error_ring_clear(f87d_error_ring_t *ring);

/* Logger callback adapter — pass ring as userdata */
void f87d_error_ring_log_callback(const f87_log_entry_t *entry, void *userdata);

#endif
```

- [ ] **Step 2: Write error_history implementation**

```c
/* daemon/src/error_history.c */
#include "error_history.h"
#include <string.h>

void f87d_error_ring_init(f87d_error_ring_t *ring)
{
    memset(ring, 0, sizeof(*ring));
    pthread_mutex_init(&ring->lock, NULL);
}

void f87d_error_ring_destroy(f87d_error_ring_t *ring)
{
    pthread_mutex_destroy(&ring->lock);
}

void f87d_error_ring_push(f87d_error_ring_t *ring, const f87_log_entry_t *entry)
{
    pthread_mutex_lock(&ring->lock);
    memcpy(&ring->entries[ring->head], entry, sizeof(*entry));
    ring->head = (ring->head + 1) % F87D_ERROR_RING_SIZE;
    if (ring->count < F87D_ERROR_RING_SIZE)
        ring->count++;
    pthread_mutex_unlock(&ring->lock);
}

int f87d_error_ring_get_all(f87d_error_ring_t *ring,
                             f87_log_entry_t *out, int max_out)
{
    pthread_mutex_lock(&ring->lock);
    int n = ring->count < max_out ? ring->count : max_out;
    int start = (ring->head - ring->count + F87D_ERROR_RING_SIZE) % F87D_ERROR_RING_SIZE;
    for (int i = 0; i < n; i++) {
        int idx = (start + i) % F87D_ERROR_RING_SIZE;
        memcpy(&out[i], &ring->entries[idx], sizeof(f87_log_entry_t));
    }
    pthread_mutex_unlock(&ring->lock);
    return n;
}

void f87d_error_ring_clear(f87d_error_ring_t *ring)
{
    pthread_mutex_lock(&ring->lock);
    ring->head = 0;
    ring->count = 0;
    pthread_mutex_unlock(&ring->lock);
}

void f87d_error_ring_log_callback(const f87_log_entry_t *entry, void *userdata)
{
    f87d_error_ring_t *ring = userdata;
    f87d_error_ring_push(ring, entry);
}
```

- [ ] **Step 3: Write test**

```c
/* tests/test_error_history.c */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "../daemon/src/error_history.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-40s ", #name); \
    tests_run++; \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else { printf("FAIL\n"); } \
} while(0)

static int test_init_empty(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);
    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);
    return n == 0;
}

static int test_push_and_get(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    f87_log_entry_t e = {0};
    e.timestamp_us = 1000;
    e.level = F87_LOG_ERROR;
    e.source = F87_SRC_USB;
    e.error_code = -5;
    snprintf(e.message, sizeof(e.message), "test error");

    f87d_error_ring_push(&ring, &e);

    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);

    return n == 1 &&
           out[0].timestamp_us == 1000 &&
           out[0].level == F87_LOG_ERROR &&
           out[0].error_code == -5 &&
           strcmp(out[0].message, "test error") == 0;
}

static int test_circular_overflow(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    for (int i = 0; i < F87D_ERROR_RING_SIZE + 10; i++) {
        f87_log_entry_t e = {0};
        e.timestamp_us = (uint64_t)i;
        e.level = F87_LOG_WARN;
        snprintf(e.message, sizeof(e.message), "msg %d", i);
        f87d_error_ring_push(&ring, &e);
    }

    f87_log_entry_t out[F87D_ERROR_RING_SIZE];
    int n = f87d_error_ring_get_all(&ring, out, F87D_ERROR_RING_SIZE);
    f87d_error_ring_destroy(&ring);

    /* Should have exactly 128 entries, oldest is index 10 */
    return n == F87D_ERROR_RING_SIZE &&
           out[0].timestamp_us == 10 &&
           out[F87D_ERROR_RING_SIZE - 1].timestamp_us == (uint64_t)(F87D_ERROR_RING_SIZE + 9);
}

static int test_clear(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    f87_log_entry_t e = {.level = F87_LOG_ERROR};
    f87d_error_ring_push(&ring, &e);
    f87d_error_ring_clear(&ring);

    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);
    return n == 0;
}

static int test_log_callback_adapter(void)
{
    f87d_error_ring_t ring;
    f87d_error_ring_init(&ring);

    f87_log_entry_t e = {0};
    e.level = F87_LOG_WARN;
    e.source = F87_SRC_AUDIO;
    snprintf(e.message, sizeof(e.message), "callback test");

    f87d_error_ring_log_callback(&e, &ring);

    f87_log_entry_t out[1];
    int n = f87d_error_ring_get_all(&ring, out, 1);
    f87d_error_ring_destroy(&ring);

    return n == 1 && strcmp(out[0].message, "callback test") == 0;
}

int main(void)
{
    printf("Error history tests:\n");
    TEST(init_empty);
    TEST(push_and_get);
    TEST(circular_overflow);
    TEST(clear);
    TEST(log_callback_adapter);
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
```

- [ ] **Step 4: Add error_history.c to daemon CMakeLists.txt**

In `daemon/CMakeLists.txt`, add `src/error_history.c` to the source list:

```
add_executable(f87d
    src/main.c
    src/device_manager.c
    src/effect_manager.c
    src/dbus_interface.c
    src/idle_monitor.c
    src/profile_manager.c
    src/error_history.c
)
```

- [ ] **Step 5: Add test to root CMakeLists.txt**

After the logger test block, add:

```cmake
add_executable(test_error_history tests/test_error_history.c daemon/src/error_history.c)
target_include_directories(test_error_history PRIVATE daemon/src lib/include lib/src)
target_link_libraries(test_error_history Threads::Threads)
add_test(NAME error_history_tests COMMAND test_error_history)
```

- [ ] **Step 6: Build and run tests**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make test_error_history && ./test_error_history`
Expected: All 5 tests PASS.

- [ ] **Step 7: Commit**

```bash
git add daemon/src/error_history.h daemon/src/error_history.c tests/test_error_history.c daemon/CMakeLists.txt CMakeLists.txt
git commit -m "feat(daemon): add error history ring buffer with tests"
```

---

### Task 4: D-Bus Error History and Log Level Methods

**Files:**
- Modify: `daemon/src/dbus_interface.h`
- Modify: `daemon/src/dbus_interface.c`
- Modify: `daemon/src/main.c`

- [ ] **Step 1: Add error ring to dbus context**

In `daemon/src/dbus_interface.h`, add the include and field:

```c
#include "error_history.h"
```

Add to `f87d_dbus_ctx_t`:

```c
typedef struct {
    sd_bus *bus;
    f87d_device_manager_t *devmgr;
    f87d_effect_manager_t *effmgr;
    f87d_idle_monitor_t *idle;
    f87d_error_ring_t *error_ring;
} f87d_dbus_ctx_t;
```

- [ ] **Step 2: Add D-Bus method handlers in dbus_interface.c**

Add these method handlers before the vtable in `daemon/src/dbus_interface.c`:

```c
static int method_get_error_history(sd_bus_message *msg, void *userdata,
                                     sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;

    f87_log_entry_t entries[F87D_ERROR_RING_SIZE];
    int count = f87d_error_ring_get_all(ctx->error_ring, entries, F87D_ERROR_RING_SIZE);

    sd_bus_message *reply = NULL;
    int r = sd_bus_message_new_method_return(msg, &reply);
    if (r < 0) return r;

    r = sd_bus_message_open_container(reply, 'a', "(tsssis)");
    if (r < 0) goto fail;

    for (int i = 0; i < count; i++) {
        r = sd_bus_message_append(reply, "(tsssis)",
            entries[i].timestamp_us,
            f87_log_level_to_string(entries[i].level),
            f87_log_source_to_string(entries[i].source),
            entries[i].message,
            entries[i].error_code,
            entries[i].error_code != 0 ? f87_strerror(entries[i].error_code) : "");
        if (r < 0) goto fail;
    }

    r = sd_bus_message_close_container(reply);
    if (r < 0) goto fail;

    return sd_bus_send(NULL, reply, NULL);

fail:
    sd_bus_message_unref(reply);
    return r;
}

static int method_clear_error_history(sd_bus_message *msg, void *userdata,
                                       sd_bus_error *error)
{
    (void)error;
    f87d_dbus_ctx_t *ctx = userdata;
    f87d_error_ring_clear(ctx->error_ring);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_set_log_level(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    (void)userdata;
    const char *level_str = NULL;

    int rc = sd_bus_message_read(msg, "s", &level_str);
    if (rc < 0)
        return sd_bus_error_set_errno(error, -rc);

    int level = f87_log_level_from_string(level_str);
    if (level < 0)
        return sd_bus_reply_method_errorf(msg,
            "org.f87.Error.InvalidParam", "Invalid log level: %s", level_str);

    f87_log_set_level(level);
    F87_INFO(F87_SRC_DBUS, "Log level changed to %s", level_str);
    return sd_bus_reply_method_return(msg, "b", 1);
}

static int method_get_log_level(sd_bus_message *msg, void *userdata,
                                 sd_bus_error *error)
{
    (void)userdata;
    (void)error;
    return sd_bus_reply_method_return(msg, "s",
        f87_log_level_to_string(f87_log_get_level()));
}
```

- [ ] **Step 3: Add methods to vtable**

In the `f87_vtable[]` array, add before `SD_BUS_VTABLE_END`:

```c
    SD_BUS_METHOD("GetErrorHistory", "", "a(tsssis)", method_get_error_history,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("ClearErrorHistory", "", "b", method_clear_error_history,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("SetLogLevel", "s", "b", method_set_log_level,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetLogLevel", "", "s", method_get_log_level,
                  SD_BUS_VTABLE_UNPRIVILEGED),
```

- [ ] **Step 4: Add logger include to dbus_interface.c**

At the top of `daemon/src/dbus_interface.c`, add:

```c
#include <f87/logger.h>
```

- [ ] **Step 5: Wire up logger and ring buffer in daemon main.c**

In `daemon/src/main.c`:

Add include:
```c
#include <f87/logger.h>
#include "error_history.h"
```

Add global ring buffer after other globals:
```c
static f87d_error_ring_t g_error_ring;
```

In `main()`, before `f87d_devmgr_init`, add:
```c
    f87_log_init(F87_LOG_JOURNAL);
    f87d_error_ring_init(&g_error_ring);
    f87_log_set_callback(f87d_error_ring_log_callback, &g_error_ring);
```

When setting up `g_dbus_ctx`, add the error_ring field:
```c
    g_dbus_ctx = (f87d_dbus_ctx_t){
        .bus = bus,
        .devmgr = &g_devmgr,
        .effmgr = &g_effmgr,
        .idle = &g_idle,
        .error_ring = &g_error_ring,
    };
```

Before `return EXIT_SUCCESS`, add cleanup:
```c
    f87_log_shutdown();
    f87d_error_ring_destroy(&g_error_ring);
```

- [ ] **Step 6: Build and test**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make && ctest --output-on-failure`
Expected: All tests pass, daemon builds successfully.

- [ ] **Step 7: Commit**

```bash
git add daemon/src/dbus_interface.h daemon/src/dbus_interface.c daemon/src/main.c
git commit -m "feat(daemon): D-Bus error history query and runtime log level control"
```

---

### Task 5: Client API for Error History and Log Level

**Files:**
- Modify: `lib/include/f87/client.h`
- Modify: `lib/src/client.c`

- [ ] **Step 1: Add declarations to client.h**

Add after the existing profile function declarations:

```c
/* Error history */
int f87_client_get_error_history(f87_client *client,
                                  f87_log_entry_t *entries, int max_entries);
int f87_client_clear_error_history(f87_client *client);

/* Log level */
int f87_client_set_log_level(f87_client *client, const char *level);
int f87_client_get_log_level(f87_client *client, char *out, int out_size);
```

Add include at top of client.h:
```c
#include "logger.h"
```

- [ ] **Step 2: Implement in client.c**

Add include at top of `lib/src/client.c`:
```c
#include "f87/logger.h"
```

Add the implementations before the signal match callbacks section:

```c
int f87_client_get_error_history(f87_client *client,
                                  f87_log_entry_t *entries, int max_entries)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "GetErrorHistory", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    r = sd_bus_message_enter_container(reply, 'a', "(tsssis)");
    if (r < 0) goto done;

    int count = 0;
    uint64_t ts;
    const char *level_str, *source_str, *msg_str, *strerr;
    int32_t ec;

    while (sd_bus_message_read(reply, "(tsssis)",
           &ts, &level_str, &source_str, &msg_str, &ec, &strerr) > 0) {
        if (count >= max_entries) break;
        entries[count].timestamp_us = ts;
        entries[count].level = f87_log_level_from_string(level_str);
        entries[count].error_code = ec;
        strncpy(entries[count].message, msg_str,
                sizeof(entries[count].message) - 1);
        entries[count].message[sizeof(entries[count].message) - 1] = '\0';
        /* Resolve source string back to enum */
        if (strcmp(source_str, "USB") == 0)         entries[count].source = F87_SRC_USB;
        else if (strcmp(source_str, "AUDIO") == 0)  entries[count].source = F87_SRC_AUDIO;
        else if (strcmp(source_str, "DBUS") == 0)   entries[count].source = F87_SRC_DBUS;
        else if (strcmp(source_str, "DEVICE") == 0) entries[count].source = F87_SRC_DEVICE;
        else if (strcmp(source_str, "EFFECT") == 0) entries[count].source = F87_SRC_EFFECT;
        else if (strcmp(source_str, "GUI") == 0)    entries[count].source = F87_SRC_GUI;
        count++;
    }

    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return count;
}

int f87_client_clear_error_history(f87_client *client)
{
    return call_bool(client, "ClearErrorHistory", NULL);
}

int f87_client_set_log_level(f87_client *client, const char *level)
{
    return call_bool(client, "SetLogLevel", "s", level);
}

int f87_client_get_log_level(f87_client *client, char *out, int out_size)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;

    int r = sd_bus_call_method(client->bus, DBUS_DEST, DBUS_PATH, DBUS_IFACE,
                               "GetLogLevel", &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return -1;
    }

    const char *val = NULL;
    sd_bus_message_read(reply, "s", &val);
    if (val)
        strncpy(out, val, (size_t)(out_size - 1));
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
    return 0;
}
```

- [ ] **Step 3: Build**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add lib/include/f87/client.h lib/src/client.c
git commit -m "feat(client): add error history and log level D-Bus client API"
```

---

### Task 6: Migrate Daemon Logging

**Files:**
- Modify: `daemon/src/main.c`
- Modify: `daemon/src/device_manager.c`
- Modify: `daemon/src/effect_manager.c`
- Modify: `daemon/src/dbus_interface.c`
- Modify: `daemon/src/profile_manager.c`
- Modify: `daemon/src/idle_monitor.c`

- [ ] **Step 1: Migrate daemon/src/main.c**

Replace all `printf(...)` and `fprintf(stderr, ...)` calls with log macros. The logger include was already added in Task 4.

| Old | New |
|-----|-----|
| `printf("f87d: device connected — %s (%04X:%04X)\n", ...)` | `F87_INFO(F87_SRC_DEVICE, "Device connected: %s (%04X:%04X)", ...)` |
| `printf("f87d: device disconnected\n")` | `F87_INFO(F87_SRC_DEVICE, "Device disconnected")` |
| `fprintf(stderr, "f87d: failed to init device manager\n")` | `F87_ERROR(F87_SRC_DEVICE, "Failed to init device manager")` |
| `fprintf(stderr, "f87d: session bus: %s\n", strerror(-r))` | `F87_ERROR(F87_SRC_DBUS, "Session bus: %s", strerror(-r))` |
| `fprintf(stderr, "f87d: dbus register: %s\n", strerror(-r))` | `F87_ERROR(F87_SRC_DBUS, "D-Bus register: %s", strerror(-r))` |
| `fprintf(stderr, "f87d: bus name: %s\n", strerror(-r))` | `F87_ERROR(F87_SRC_DBUS, "Bus name: %s", strerror(-r))` |
| `printf("f87d: restoring last state (%s, effect %d)\n", ...)` | `F87_INFO(F87_SRC_EFFECT, "Restoring last state: %s effect %d", ...)` |
| `printf("f87d: running (device %s)\n", ...)` | `F87_INFO(F87_SRC_DBUS, "Running (device %s)", ...)` |
| `printf("f87d: idle timeout, exiting\n")` | `F87_INFO(F87_SRC_DBUS, "Idle timeout, exiting")` |
| `printf("f87d: shutting down\n")` | `F87_INFO(F87_SRC_DBUS, "Shutting down")` |

- [ ] **Step 2: Migrate daemon/src/dbus_interface.c**

Replace `printf` calls in profile methods:

| Old | New |
|-----|-----|
| `printf("f87d: profile saved: %s\n", name)` | `F87_INFO(F87_SRC_DBUS, "Profile saved: %s", name)` |
| `printf("f87d: profile loaded: %s\n", name)` | `F87_INFO(F87_SRC_DBUS, "Profile loaded: %s", name)` |
| `printf("f87d: profile deleted: %s\n", name)` | `F87_INFO(F87_SRC_DBUS, "Profile deleted: %s", name)` |

- [ ] **Step 3: Build and test**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make && ctest --output-on-failure`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add daemon/src/main.c daemon/src/dbus_interface.c
git commit -m "refactor(daemon): migrate printf/fprintf to logger macros"
```

---

### Task 7: Migrate Library Logging

**Files:**
- Modify: `lib/src/audio.c`
- Modify: `lib/src/device.c`
- Modify: `lib/src/animate.c`

- [ ] **Step 1: Migrate lib/src/audio.c**

Add include at top (inside the `#ifdef F87_HAS_AUDIO` block, after existing includes):
```c
#include "f87/logger.h"
```

Replace fprintf calls:

| Old | New |
|-----|-----|
| `fprintf(stderr, "f87: audio source: %s\n", source)` | `F87_DEBUG(F87_SRC_AUDIO, "Audio source: %s", source)` |
| `fprintf(stderr, "f87: PulseAudio error: %s\n", pa_strerror(pa_err))` | `F87_ERROR(F87_SRC_AUDIO, "PulseAudio error: %s", pa_strerror(pa_err))` |
| `fprintf(stderr, "f87: PulseAudio read error: %s\n", pa_strerror(pa_err))` | `F87_ERROR(F87_SRC_AUDIO, "PulseAudio read error: %s", pa_strerror(pa_err))` |

- [ ] **Step 2: Add TRACE logging for USB transfers in protocol.c (optional, selective)**

In `lib/src/protocol.c`, add include:
```c
#include "f87/logger.h"
```

Add TRACE after `f87_pkt_send()` returns:
```c
F87_TRACE(F87_SRC_USB, "USB send: %d bytes, cmd=0x%02X", len, buf[1]);
```

Add TRACE after `f87_pkt_recv()` returns:
```c
F87_TRACE(F87_SRC_USB, "USB recv: %d bytes", rc);
```

- [ ] **Step 3: Build and test**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make && ctest --output-on-failure`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add lib/src/audio.c lib/src/protocol.c
git commit -m "refactor(lib): migrate fprintf to logger macros in audio and protocol"
```

---

### Task 8: Migrate CLI Logging

**Files:**
- Modify: `cli/src/main.c`

- [ ] **Step 1: Add logger init to CLI**

At the top of `cli/src/main.c`, add:
```c
#include <f87/logger.h>
```

At the beginning of `main()`, add:
```c
    f87_log_init(F87_LOG_STDERR);
```

- [ ] **Step 2: Migrate key error fprintf calls**

Replace `fprintf(stderr, "Error ...")` patterns with log macros. The CLI has many user-facing messages that should stay as `fprintf(stderr)` (usage, prompts). Only migrate actual error/diagnostic messages:

Examples:
| Old | New |
|-----|-----|
| `fprintf(stderr, "Error finding devices: %s\n", f87_strerror(rc))` | `F87_ERROR_EC(F87_SRC_DEVICE, rc, "Finding devices: %s", f87_strerror(rc))` |
| `fprintf(stderr, "Error setting brightness: %s\n", f87_strerror(rc))` | `F87_ERROR_EC(F87_SRC_USB, rc, "Setting brightness: %s", f87_strerror(rc))` |
| `fprintf(stderr, "Animation error: %s\n", f87_strerror(err))` | `F87_ERROR_EC(F87_SRC_EFFECT, err, "Animation error: %s", f87_strerror(err))` |

Keep `usage()` and other user-facing output as `fprintf(stderr)` — these are not log messages.

- [ ] **Step 3: Build and test**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. && make && ctest --output-on-failure`
Expected: All tests pass, CLI builds.

- [ ] **Step 4: Commit**

```bash
git add cli/src/main.c
git commit -m "refactor(cli): init logger and migrate error messages to log macros"
```

---

### Task 9: GUI Status Bar Color Coding

**Files:**
- Modify: `gui/src/app_state.h`
- Modify: `gui/src/app_state.c`
- Modify: `gui/src/window.c`
- Modify: `gui/resources/style.css`

- [ ] **Step 1: Add status_level to app_state.h**

In `gui/src/app_state.h`, add the field to `f87_app_state_t`:

```c
typedef struct {
    f87_client *client;
    f87_gui_status_t status;
    char status_text[256];
    int status_level;  /* F87_LOG_INFO, F87_LOG_WARN, F87_LOG_ERROR */
    int current_effect_id;
    char current_category[16];
    char current_sensor_profile[64];
    bool device_connected;
} f87_app_state_t;
```

Add include:
```c
#include <f87/logger.h>
```

- [ ] **Step 2: Set status_level in app_state.c**

In `gui/src/app_state.c`, add include:
```c
#include <f87/logger.h>
```

Set `status_level` wherever `status_text` is updated:
- On error: `state->status_level = F87_LOG_ERROR;`
- On success/running: `state->status_level = F87_LOG_INFO;`
- On idle: `state->status_level = F87_LOG_INFO;`

For every `snprintf(state->status_text, ...)` where `state->status = F87_GUI_ERROR`, add:
```c
state->status_level = F87_LOG_ERROR;
```

For every `snprintf(state->status_text, ...)` where `state->status = F87_GUI_RUNNING` or `F87_GUI_IDLE`, add:
```c
state->status_level = F87_LOG_INFO;
```

- [ ] **Step 3: Update window.c status bar to apply CSS classes**

In `gui/src/window.c`, modify `on_status_update`:

```c
static void on_status_update(const char *text, gpointer user_data)
{
    F87Window *self = user_data;
    gtk_label_set_text(self->status_label, text);

    /* Update color based on status level */
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-error");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-warn");
    gtk_widget_remove_css_class(GTK_WIDGET(self->status_label), "status-ok");

    if (self->app_state.status == F87_GUI_ERROR) {
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-error");
    } else if (self->app_state.status == F87_GUI_RUNNING) {
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-ok");
    }
}
```

Also update the status label after init:

```c
    /* Initialize device connection */
    f87_app_state_init(&self->app_state);
    gtk_label_set_text(self->status_label, self->app_state.status_text);
    if (self->app_state.status == F87_GUI_ERROR)
        gtk_widget_add_css_class(GTK_WIDGET(self->status_label), "status-error");
```

- [ ] **Step 4: Add CSS classes for warn and ok states**

In `gui/resources/style.css`, add the warn and ok classes (error already exists):

```css
.status-warn {
    color: #e5a50a;
}

.status-ok {
    color: #2ec27e;
}
```

- [ ] **Step 5: Init logger in GUI main.c**

In `gui/src/main.c`, add:
```c
#include <f87/logger.h>
```

At the start of `main()`:
```c
    f87_log_init(F87_LOG_STDERR);
```

- [ ] **Step 6: Build and test**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_GUI=ON && make`
Expected: GUI builds with color-coded status bar.

- [ ] **Step 7: Commit**

```bash
git add gui/src/app_state.h gui/src/app_state.c gui/src/window.c gui/resources/style.css gui/src/main.c
git commit -m "feat(gui): color-coded status bar (green/yellow/red by severity)"
```

---

### Task 10: Full Integration Test

**Files:** None (verification only)

- [ ] **Step 1: Build everything**

Run: `cd /home/emrah/Projects/F87Control/build && cmake .. -DBUILD_GUI=ON -DBUILD_DAEMON=ON -DBUILD_CLI=ON && make`
Expected: Clean build, no warnings.

- [ ] **Step 2: Run all tests**

Run: `cd /home/emrah/Projects/F87Control/build && ctest --output-on-failure`
Expected: All tests pass including new logger_tests and error_history_tests.

- [ ] **Step 3: Test logger level filtering via env var**

Run: `F87_LOG_LEVEL=trace ./f87d 2>&1 | head -5`
Expected: TRACE-level messages visible in output.

Run: `F87_LOG_LEVEL=error ./f87d 2>&1 | head -5`
Expected: Only ERROR-level messages visible.

- [ ] **Step 4: Test D-Bus log level methods (if daemon running)**

Run: `busctl --user call org.f87.Control /org/f87/Control org.f87.Control GetLogLevel`
Expected: Returns current log level string.

Run: `busctl --user call org.f87.Control /org/f87/Control org.f87.Control SetLogLevel s trace`
Expected: Returns boolean true.

Run: `busctl --user call org.f87.Control /org/f87/Control org.f87.Control GetErrorHistory`
Expected: Returns (possibly empty) array of error entries.

- [ ] **Step 5: Commit final state**

If any fixes were needed during integration testing, commit them:
```bash
git add -u
git commit -m "fix: integration fixes for error collection system"
```
