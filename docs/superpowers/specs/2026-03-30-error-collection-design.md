# Error Collection System Design

## Overview

Centralized logging and error history system for F87Control. Replaces scattered `fprintf(stderr)` calls with a unified, leveled logging API. Daemon stores recent errors in a ring buffer queryable over D-Bus. GUI status bar shows color-coded error state.

## Log Levels

| Level | Value | Purpose |
|-------|-------|---------|
| TRACE | 0 | USB packet-level detail, frame timing |
| DEBUG | 1 | Internal state changes, config reads |
| INFO  | 2 | Normal operations (device connected, effect changed) |
| WARN  | 3 | Recoverable issues (USB retry, reconnect attempt) |
| ERROR | 4 | Failures (device lost, USB I/O error, audio fail) |

Default level: `INFO`. Configurable via `F87_LOG_LEVEL` env var at startup, `SetLogLevel` D-Bus method at runtime.

## Log Sources

```c
typedef enum {
    F87_SRC_USB,      /* USB transfers, protocol */
    F87_SRC_AUDIO,    /* PulseAudio capture, FFT */
    F87_SRC_DBUS,     /* D-Bus method handling */
    F87_SRC_DEVICE,   /* Device enumeration, hotplug */
    F87_SRC_EFFECT,   /* Effect lifecycle, animation */
    F87_SRC_GUI       /* GUI state, user interaction */
} f87_log_source_t;
```

## Logger API

### Public Header: `lib/include/f87/logger.h`

```c
/* Backend types */
typedef enum {
    F87_LOG_STDERR,    /* CLI, GUI — writes to stderr */
    F87_LOG_JOURNAL    /* Daemon — writes to sd_journal */
} f87_log_backend_t;

/* Initialize/shutdown */
int  f87_log_init(f87_log_backend_t backend);
void f87_log_shutdown(void);

/* Level control */
void f87_log_set_level(int level);
int  f87_log_get_level(void);
int  f87_log_level_from_string(const char *str);  /* "trace" -> 0 */
const char *f87_log_level_to_string(int level);    /* 0 -> "TRACE" */

/* Core log function */
void f87_log(int level, int source, int error_code, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* Convenience macros */
#define F87_TRACE(src, fmt, ...) f87_log(F87_LOG_TRACE, (src), 0, fmt, ##__VA_ARGS__)
#define F87_DEBUG(src, fmt, ...) f87_log(F87_LOG_DEBUG, (src), 0, fmt, ##__VA_ARGS__)
#define F87_INFO(src, fmt, ...)  f87_log(F87_LOG_INFO,  (src), 0, fmt, ##__VA_ARGS__)
#define F87_WARN(src, fmt, ...)  f87_log(F87_LOG_WARN,  (src), 0, fmt, ##__VA_ARGS__)
#define F87_ERROR(src, fmt, ...) f87_log(F87_LOG_ERROR, (src), 0, fmt, ##__VA_ARGS__)

/* Error-code variants (includes error_code in log entry) */
#define F87_WARN_EC(src, ec, fmt, ...)  f87_log(F87_LOG_WARN,  (src), (ec), fmt, ##__VA_ARGS__)
#define F87_ERROR_EC(src, ec, fmt, ...) f87_log(F87_LOG_ERROR, (src), (ec), fmt, ##__VA_ARGS__)
```

### Backend Behavior

**F87_LOG_STDERR:**
```
[2026-03-30 14:22:01.123] [ERROR] [USB] (-5) USB I/O error on transfer
```
Format: `[timestamp] [LEVEL] [SOURCE] (error_code) message`
Error code omitted when 0.

**F87_LOG_JOURNAL:**
- Uses `sd_journal_send()` with structured fields:
  - `MESSAGE=USB I/O error on transfer`
  - `PRIORITY=3` (mapped from log level)
  - `F87_SOURCE=USB`
  - `F87_ERROR_CODE=-5`
  - `F87_LEVEL=ERROR`
- Priority mapping: TRACE/DEBUG->7(debug), INFO->6(info), WARN->4(warning), ERROR->3(err)
- Queryable via: `journalctl --user -u f87d F87_SOURCE=USB F87_LEVEL=ERROR`

### Level Control

Startup: reads `F87_LOG_LEVEL` env var (case-insensitive: `trace`, `debug`, `info`, `warn`, `error`). Falls back to `INFO` if unset or invalid.

Runtime: `f87_log_set_level()` changes the atomic level variable. Thread-safe (atomic_int).

## Ring Buffer (Daemon Error History)

### Structure

```c
typedef struct {
    uint64_t timestamp_us;     /* Microseconds since epoch */
    int      level;            /* F87_LOG_WARN or F87_LOG_ERROR */
    int      source;           /* f87_log_source_t */
    int      error_code;       /* F87_ERR_* or 0 */
    char     message[256];     /* Human-readable message */
} f87_log_entry_t;

typedef struct {
    f87_log_entry_t entries[128];
    int head;                  /* Next write position */
    int count;                 /* Current entry count (max 128) */
    pthread_mutex_t lock;
} f87_error_ring_t;
```

### Behavior

- Only `WARN` and `ERROR` level messages are added to the ring buffer
- Circular: oldest entry overwritten when full
- Thread-safe: mutex-protected (effect threads + D-Bus handler thread)
- Lifetime: created in daemon, cleared on `ClearErrorHistory` call

### Integration

Logger calls a registered callback when level >= WARN. Daemon registers a callback that inserts into the ring buffer. CLI/GUI don't register a callback (no ring buffer needed).

```c
/* Callback type */
typedef void (*f87_log_callback_t)(const f87_log_entry_t *entry, void *userdata);

/* Register callback (daemon only) */
void f87_log_set_callback(f87_log_callback_t cb, void *userdata);
```

## D-Bus Interface Extensions

### New Methods on `org.f87.Control`

**GetErrorHistory() -> a(tsissi)**
- Returns array of structs: (timestamp_us, level_str, source_str, error_code, message)
- Ordered oldest to newest
- Returns up to 128 entries

**ClearErrorHistory()**
- Clears the ring buffer
- Returns nothing

**SetLogLevel(s level)**
- Accepts: "trace", "debug", "info", "warn", "error"
- Returns: nothing
- Error: `org.f87.Error.InvalidParam` if invalid level string

**GetLogLevel() -> s**
- Returns current log level as string

### D-Bus Property

- `LogLevel` (readwrite, string) — current log level

## GUI Status Bar Enhancement

### Color Coding

Current `status_text[256]` in `app_state` gets a `status_level` field:

```c
int status_level;  /* F87_LOG_INFO, F87_LOG_WARN, F87_LOG_ERROR */
```

CSS classes applied to status label:
- `status-error` — red text (#e01b24)
- `status-warn` — yellow/amber text (#e5a50a)
- `status-ok` — green text (#2ec27e) or default

### Popup Detail

Clicking the status bar label shows a `GtkPopover` with:
- Timestamp
- Source component
- Error code (if non-zero) with `f87_strerror()` translation
- Full message

No scrollable history list — just last error detail. Full history available via `journalctl`.

## Migration Plan

### Existing Code Changes

All `fprintf(stderr, ...)` and `printf()` calls replaced with appropriate log macros:

| Current Pattern | Replacement |
|----------------|-------------|
| `fprintf(stderr, "f87: PulseAudio error: %s\n", ...)` | `F87_ERROR(F87_SRC_AUDIO, "PulseAudio error: %s", ...)` |
| `fprintf(stderr, "Error setting effect: %d\n", rc)` | `F87_ERROR_EC(F87_SRC_EFFECT, rc, "Failed to set effect")` |
| `printf("Device connected: %s\n", ...)` | `F87_INFO(F87_SRC_DEVICE, "Device connected: %s", ...)` |
| `fprintf(stderr, "USB transfer: %d bytes\n", ...)` | `F87_TRACE(F87_SRC_USB, "USB transfer: %d bytes", ...)` |

### Initialization

- **Daemon (`daemon/src/main.c`):** `f87_log_init(F87_LOG_JOURNAL)` + register ring buffer callback
- **CLI (`cli/src/main.c`):** `f87_log_init(F87_LOG_STDERR)`
- **GUI (`gui/src/main.c`):** `f87_log_init(F87_LOG_STDERR)`

## New Files

| File | Purpose |
|------|---------|
| `lib/include/f87/logger.h` | Public logging API |
| `lib/src/logger.c` | Logger implementation (stderr + journal backends, level control, callback) |
| `daemon/src/error_history.h` | Ring buffer type and API |
| `daemon/src/error_history.c` | Ring buffer implementation |

## Modified Files

| File | Changes |
|------|---------|
| `lib/src/protocol.c` | Replace fprintf with F87_TRACE/F87_DEBUG |
| `lib/src/device.c` | Replace fprintf with F87_INFO/F87_ERROR |
| `lib/src/audio.c` | Replace fprintf with F87_ERROR/F87_DEBUG |
| `lib/src/animate.c` | Replace error stores with F87_ERROR |
| `lib/src/lighting.c` | Add F87_TRACE for USB transfers |
| `lib/src/effects.c` | Add F87_DEBUG for effect changes |
| `lib/src/client.c` | Replace fprintf with F87_WARN/F87_ERROR |
| `daemon/src/main.c` | Init journal backend, register ring buffer |
| `daemon/src/dbus_interface.c` | Add new methods, replace fprintf |
| `daemon/src/device_manager.c` | Replace printf/fprintf with log macros |
| `daemon/src/effect_manager.c` | Add F87_DEBUG/F87_ERROR |
| `daemon/src/idle_monitor.c` | Add F87_DEBUG |
| `daemon/src/profile_manager.c` | Replace fprintf with F87_WARN/F87_ERROR |
| `cli/src/main.c` | Init stderr backend, replace fprintf |
| `gui/src/app_state.c` | Add status_level, color coding |
| `gui/src/window.c` | Status bar popover, CSS classes |
| `lib/CMakeLists.txt` | Add logger.c |
| `daemon/CMakeLists.txt` | Add error_history.c |

## Dependencies

- `libsystemd` (already linked in daemon for sd-bus) — adds `sd_journal_send()` usage
- No new external dependencies

## Thread Safety

- `f87_log_set_level()` / `f87_log_get_level()`: atomic_int, lock-free
- `f87_log()`: no shared mutable state (writes directly to stderr or journal)
- Ring buffer: pthread_mutex for insert and read operations
- Callback pointer: set once at init, never changed — no synchronization needed
