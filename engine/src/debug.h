#ifndef DEBUG_H
#define DEBUG_H

#include <stdbool.h>
#include <stdio.h>

#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128

/* Initialize the debug logging system with external state pointers. Call once at startup.
 * log_lines: pointer to DEBUG_LOG_LINES x DEBUG_LOG_LINE_LEN char array
 * log_head: pointer to int for ring buffer head index
 * log_count: pointer to int for current line count
 * trace_file: pointer to FILE* for trace file handle
 * trace_path: if non-NULL, opens a trace file at this path */
void debug_init(char (*log_lines)[DEBUG_LOG_LINE_LEN], int *log_head, int *log_count, FILE **trace_file, const char *trace_path);

/* Shut down the debug logging system. Closes the trace file if open. */
void debug_shutdown(void);

/* Log a message to stdout, the in-memory ring buffer, and the trace file. */
void debug_log(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Access the log ring buffer for overlay rendering. */
const char *debug_get_line(int index);
int debug_get_line_count(void);

#endif
