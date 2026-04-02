#pragma once

#define ERROR_MSG_LEN 512

typedef struct {
    char error_buffer[ERROR_MSG_LEN];
} ErrorState;

/* Set the root error (clears any previous chain). */
void error_set(ErrorState *err, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* Prepend context to the existing error. Produces chains like:
 * "load_gamedata: level_load: fopen(/path): Permission denied" */
void error_wrap(ErrorState *err, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* Return the current error string, or nullptr if no error is set. */
[[nodiscard]] const char *error_get(const ErrorState *err);

/* Clear the error state. Call after logging at the top-level boundary. */
void error_clear(ErrorState *err);
