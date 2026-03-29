#include "f87/logger.h"
#include <stdio.h>
#include <stdlib.h>
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

    if (g_backend == F87_LOG_STDERR) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm;
        localtime_r(&ts.tv_sec, &tm);

        if (error_code != 0) {
            fprintf(stderr,
                    "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] [%s] (%d) %s\n",
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                    tm.tm_hour, tm.tm_min, tm.tm_sec,
                    ts.tv_nsec / 1000000L,
                    f87_log_level_to_string(level),
                    f87_log_source_to_string(source),
                    error_code, message);
        } else {
            fprintf(stderr,
                    "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] [%s] [%s] %s\n",
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
        int priority;
        switch (level) {
        case F87_LOG_TRACE: priority = 7; break;
        case F87_LOG_DEBUG: priority = 7; break;
        case F87_LOG_INFO:  priority = 6; break;
        case F87_LOG_WARN:  priority = 4; break;
        case F87_LOG_ERROR: priority = 3; break;
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
