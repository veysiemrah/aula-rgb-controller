#ifndef F87_LOGGER_H
#define F87_LOGGER_H

#include <stdint.h>

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
