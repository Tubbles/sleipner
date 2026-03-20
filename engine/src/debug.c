#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NSEC_PER_MSEC 1000000L
#define TM_YEAR_OFFSET 1900

/* Static pointers to externally owned state */
static char (*log_lines)[DEBUG_LOG_LINE_LEN] = NULL;
static int *log_head = NULL;
static int *log_count = NULL;
static FILE **trace_file = NULL;

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

void debug_init(char (*external_log_lines)[DEBUG_LOG_LINE_LEN], int *external_log_head, int *external_log_count, FILE **external_trace_file, const char *trace_path)
{
    /* Store pointers to externally owned state */
    log_lines = external_log_lines;
    log_head = external_log_head;
    log_count = external_log_count;
    trace_file = external_trace_file;

    /* Initialize the state */
    *log_head = 0;
    *log_count = 0;
    memset(log_lines, 0, sizeof(char) * DEBUG_LOG_LINES * DEBUG_LOG_LINE_LEN);

    if (trace_path) {
        *trace_file = fopen(trace_path, "ae");
        if (*trace_file) {
            debug_log("trace file opened: %s", trace_path);
        } else {
            debug_log("trace file FAILED to open: %s", trace_path);
        }
    }
}

void debug_shutdown(void)
{
    if (trace_file && *trace_file) {
        (void)fclose(*trace_file);
        *trace_file = NULL;
    }
    /* Clear pointers to avoid dangling references */
    log_lines = NULL;
    log_head = NULL;
    log_count = NULL;
    trace_file = NULL;
}

void debug_log(const char *format, ...)
{
    va_list args;

    /* Write to ring buffer */
    va_start(args, format);
    /* NOLINTNEXTLINE(clang-analyzer-security.VAList) -- va_start is called above */
    (void)vsnprintf(log_lines[*log_head], DEBUG_LOG_LINE_LEN, format, args);
    va_end(args);

    /* Write to stdout with timestamp */
    write_timestamp(stdout);
    va_start(args, format);
    (void)vfprintf(stdout, format, args);
    va_end(args);
    (void)fputc('\n', stdout);

    /* Write to trace file with timestamp */
    if (trace_file && *trace_file) {
        write_timestamp(*trace_file);
        va_start(args, format);
        (void)vfprintf(*trace_file, format, args);
        va_end(args);
        (void)fputc('\n', *trace_file);
        (void)fflush(*trace_file);
    }

    *log_head = (*log_head + 1) % DEBUG_LOG_LINES;
    if (*log_count < DEBUG_LOG_LINES) {
        (*log_count)++;
    }
}

const char *debug_get_line(int index)
{
    int actual = (*log_head - *log_count + index + DEBUG_LOG_LINES) % DEBUG_LOG_LINES;
    return log_lines[actual];
}

int debug_get_line_count(void)
{
    return *log_count;
}
