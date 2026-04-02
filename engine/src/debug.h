#pragma once

#include <stdbool.h>
#include <stdio.h>

#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128

typedef struct {
    char log_lines[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LEN];
    int log_head;
    int log_count;
    FILE *trace_file;
} DebugState;

/* Initialize the debug logging system. Call once at startup.
 * If trace_path is non-nullptr, opens a trace file at that path. */
void debug_init(DebugState *dbg, const char *trace_path);

/* Shut down the debug logging system. Closes the trace file if open. */
void debug_shutdown(DebugState *dbg);

/* Log a message to stdout, the in-memory ring buffer, and the trace file. */
void debug_log(DebugState *dbg, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* Access the log ring buffer for overlay rendering. */
const char *debug_get_line(const DebugState *dbg, int index);
int debug_get_line_count(const DebugState *dbg);
