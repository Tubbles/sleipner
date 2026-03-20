#include "debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NSEC_PER_MSEC 1000000L
#define TM_YEAR_OFFSET 1900

/* Static pointer to externally owned DebugState */
static DebugState *debug_state = NULL;

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

void debug_init(DebugState *state, const char *trace_path)
{
    /* Store pointer to externally owned state */
    debug_state = state;

    /* Initialize the state */
    debug_state->log_head = 0;
    debug_state->log_count = 0;
    memset(debug_state->log_lines, 0, sizeof(debug_state->log_lines));

    if (trace_path) {
        debug_state->trace_file = fopen(trace_path, "ae");
        if (debug_state->trace_file) {
            debug_log("trace file opened: %s", trace_path);
        } else {
            debug_log("trace file FAILED to open: %s", trace_path);
        }
    }
}

void debug_shutdown(void)
{
    if (debug_state && debug_state->trace_file) {
        (void)fclose(debug_state->trace_file);
        debug_state->trace_file = NULL;
    }
    /* Clear pointer to avoid dangling reference */
    debug_state = NULL;
}

void debug_log(const char *format, ...)
{
    va_list args;

    /* Write to ring buffer */
    va_start(args, format);
    /* NOLINTNEXTLINE(clang-analyzer-security.VAList) -- va_start is called above */
    (void)vsnprintf(debug_state->log_lines[debug_state->log_head], DEBUG_LOG_LINE_LEN, format, args);
    va_end(args);

    /* Write to stdout with timestamp */
    write_timestamp(stdout);
    va_start(args, format);
    (void)vfprintf(stdout, format, args);
    va_end(args);
    (void)fputc('\n', stdout);

    /* Write to trace file with timestamp */
    if (debug_state->trace_file) {
        write_timestamp(debug_state->trace_file);
        va_start(args, format);
        (void)vfprintf(debug_state->trace_file, format, args);
        va_end(args);
        (void)fputc('\n', debug_state->trace_file);
        (void)fflush(debug_state->trace_file);
    }

    debug_state->log_head = (debug_state->log_head + 1) % DEBUG_LOG_LINES;
    if (debug_state->log_count < DEBUG_LOG_LINES) {
        debug_state->log_count++;
    }
}

const char *debug_get_line(int index)
{
    int actual = (debug_state->log_head - debug_state->log_count + index + DEBUG_LOG_LINES) % DEBUG_LOG_LINES;
    return debug_state->log_lines[actual];
}

int debug_get_line_count(void)
{
    return debug_state->log_count;
}
