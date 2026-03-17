#ifndef DEBUG_H
#define DEBUG_H

#include <stdbool.h>

#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128

/* Initialize the debug logging system. Call once at startup.
 * If trace_path is non-NULL, opens a trace file at that path. */
void debug_init(const char *trace_path);

/* Shut down the debug logging system. Closes the trace file if open. */
void debug_shutdown(void);

/* Log a message to stdout, the in-memory ring buffer, and the trace file. */
void debug_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Access the log ring buffer for overlay rendering. */
const char *debug_get_line(int index);
int debug_get_line_count(void);

#endif
