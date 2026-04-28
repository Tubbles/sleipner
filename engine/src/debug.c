#include "debug.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define FOPEN_APPEND "a"
#else
#define FOPEN_APPEND "ae"
#endif

#define NSEC_PER_MSEC 1000000L
#define TM_YEAR_OFFSET 1900

#ifdef _WIN32
#include <windows.h>

#define WIN32_EPOCH_OFFSET 116444736000000000ULL
#define FILETIME_TICKS_PER_SEC 10000000ULL
#define FILETIME_TICKS_PER_MSEC 10000ULL

static void write_timestamp(FILE *output)
{
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER ull = {.LowPart = ft.dwLowDateTime, .HighPart = ft.dwHighDateTime};
    uint64_t unix_ticks = ull.QuadPart - WIN32_EPOCH_OFFSET;
    time_t sec = (time_t)(unix_ticks / FILETIME_TICKS_PER_SEC);
    long msec = (long)((unix_ticks % FILETIME_TICKS_PER_SEC) / FILETIME_TICKS_PER_MSEC);
    struct tm local = {0};
    localtime_s(&local, &sec);
    (void)fprintf(output, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ", local.tm_year + TM_YEAR_OFFSET, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, msec);
}
#else
static void write_timestamp(FILE *output)
{
    struct timespec now;
    /* NOLINTNEXTLINE(misc-include-cleaner) -- CLOCK_REALTIME comes from time.h via _POSIX_C_SOURCE */
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm local;
    localtime_r(&now.tv_sec, &local);
    (void)fprintf(output, "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ", local.tm_year + TM_YEAR_OFFSET, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, now.tv_nsec / NSEC_PER_MSEC);
}
#endif

void debug_init(DebugState *dbg, const char *trace_path)
{
    dbg->log_head = 0;
    dbg->log_count = 0;
    memset(dbg->log_lines, 0, sizeof(dbg->log_lines));
    dbg->trace_file = nullptr;

    if (trace_path) {
        dbg->trace_file = fopen(trace_path, FOPEN_APPEND);
        if (dbg->trace_file) {
            debug_log(dbg, "trace file opened: %s", trace_path);
        } else {
            debug_log(dbg, "trace file FAILED to open: %s", trace_path);
        }
    }
}

void debug_reopen_trace(DebugState *dbg, const char *trace_path)
{
    if (trace_path == nullptr) {
        return;
    }
    if (dbg->trace_file) {
        (void)fclose(dbg->trace_file);
        dbg->trace_file = nullptr;
    }
    dbg->trace_file = fopen(trace_path, FOPEN_APPEND);
    if (dbg->trace_file) {
        debug_log(dbg, "trace file reopened: %s", trace_path);
    } else {
        debug_log(dbg, "trace file FAILED to reopen: %s", trace_path);
    }
}

void debug_shutdown(DebugState *dbg)
{
    if (dbg->trace_file) {
        (void)fclose(dbg->trace_file);
        dbg->trace_file = nullptr;
    }
}

void debug_log(DebugState *dbg, const char *format, ...)
{
    va_list args;

    /* Write to ring buffer */
    va_start(args, format);
    /* NOLINTNEXTLINE(clang-analyzer-security.VAList) -- va_start is called above */
    (void)vsnprintf(dbg->log_lines[dbg->log_head], DEBUG_LOG_LINE_LEN, format, args);
    va_end(args);

    /* Write to stdout with timestamp */
    write_timestamp(stdout);
    va_start(args, format);
    (void)vfprintf(stdout, format, args);
    va_end(args);
    (void)fputc('\n', stdout);

    /* Write to trace file with timestamp */
    if (dbg->trace_file) {
        write_timestamp(dbg->trace_file);
        va_start(args, format);
        (void)vfprintf(dbg->trace_file, format, args);
        va_end(args);
        (void)fputc('\n', dbg->trace_file);
        (void)fflush(dbg->trace_file);
    }

    dbg->log_head = (dbg->log_head + 1) % DEBUG_LOG_LINES;
    if (dbg->log_count < DEBUG_LOG_LINES) {
        dbg->log_count++;
    }
}

const char *debug_get_line(const DebugState *dbg, int index)
{
    int actual = (dbg->log_head - dbg->log_count + index + DEBUG_LOG_LINES) % DEBUG_LOG_LINES;
    return dbg->log_lines[actual];
}

int debug_get_line_count(const DebugState *dbg)
{
    return dbg->log_count;
}
