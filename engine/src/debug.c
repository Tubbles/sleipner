#include "debug.h"
#include "engine_context.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NSEC_PER_MSEC 1000000L
#define TM_YEAR_OFFSET 1900

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

void debug_init(struct EngineContext *ctx, const char *trace_path)
{
    ctx->debug.log_head = 0;
    ctx->debug.log_count = 0;
    memset(ctx->debug.log_lines, 0, sizeof(ctx->debug.log_lines));
    ctx->debug.trace_file = nullptr;

    if (trace_path) {
        ctx->debug.trace_file = fopen(trace_path, "ae");
        if (ctx->debug.trace_file) {
            debug_log(ctx, "trace file opened: %s", trace_path);
        } else {
            debug_log(ctx, "trace file FAILED to open: %s", trace_path);
        }
    }
}

void debug_shutdown(struct EngineContext *ctx)
{
    if (ctx->debug.trace_file) {
        (void)fclose(ctx->debug.trace_file);
        ctx->debug.trace_file = nullptr;
    }
}

void debug_log(struct EngineContext *ctx, const char *format, ...)
{
    va_list args;

    /* Write to ring buffer */
    va_start(args, format);
    /* NOLINTNEXTLINE(clang-analyzer-security.VAList) -- va_start is called above */
    (void)vsnprintf(ctx->debug.log_lines[ctx->debug.log_head], DEBUG_LOG_LINE_LEN, format, args);
    va_end(args);

    /* Write to stdout with timestamp */
    write_timestamp(stdout);
    va_start(args, format);
    (void)vfprintf(stdout, format, args);
    va_end(args);
    (void)fputc('\n', stdout);

    /* Write to trace file with timestamp */
    if (ctx->debug.trace_file) {
        write_timestamp(ctx->debug.trace_file);
        va_start(args, format);
        (void)vfprintf(ctx->debug.trace_file, format, args);
        va_end(args);
        (void)fputc('\n', ctx->debug.trace_file);
        (void)fflush(ctx->debug.trace_file);
    }

    ctx->debug.log_head = (ctx->debug.log_head + 1) % DEBUG_LOG_LINES;
    if (ctx->debug.log_count < DEBUG_LOG_LINES) {
        ctx->debug.log_count++;
    }
}

const char *debug_get_line(struct EngineContext *ctx, int index)
{
    int actual = (ctx->debug.log_head - ctx->debug.log_count + index + DEBUG_LOG_LINES) % DEBUG_LOG_LINES;
    return ctx->debug.log_lines[actual];
}

int debug_get_line_count(struct EngineContext *ctx)
{
    return ctx->debug.log_count;
}
