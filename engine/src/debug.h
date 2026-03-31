#ifndef DEBUG_H
#define DEBUG_H

#include <stdbool.h>

#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128

/* Initialize the debug logging system. Call once at startup.
 * If trace_path is non-nullptr, opens a trace file at that path. */
struct EngineContext;

void debug_init(struct EngineContext *ctx, const char *trace_path);

/* Shut down the debug logging system. Closes the trace file if open. */
void debug_shutdown(struct EngineContext *ctx);

/* Log a message to stdout, the in-memory ring buffer, and the trace file. */
void debug_log(struct EngineContext *ctx, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* Access the log ring buffer for overlay rendering. */
const char *debug_get_line(struct EngineContext *ctx, int index);
int debug_get_line_count(struct EngineContext *ctx);

#endif
