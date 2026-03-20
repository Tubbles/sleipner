#ifndef DEBUG_H
#define DEBUG_H

#include <stdbool.h>
#include <stdio.h>

#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128

/* Debug state - instantiated by owner (main.c), used by debug module */
typedef struct {
    char log_lines[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LEN];
    int log_head;
    int log_count;
    FILE *trace_file;
} DebugState;

/* Initialize the debug logging system. Call once at startup.
 * state: pointer to DebugState struct (owned by caller)
 * trace_path: if non-NULL, opens a trace file at this path */
void debug_init(DebugState *state, const char *trace_path);

/* Shut down the debug logging system. Closes the trace file if open. */
void debug_shutdown(void);

/* Log a message to stdout, the in-memory ring buffer, and the trace file. */
void debug_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Access the log ring buffer for overlay rendering. */
const char *debug_get_line(int index);
int debug_get_line_count(void);

#endif
