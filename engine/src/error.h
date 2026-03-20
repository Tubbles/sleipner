#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>

#define ERROR_MSG_LEN 512

/* Error state - instantiated by owner, used by error module */
typedef struct {
    char buffer[ERROR_MSG_LEN];
} ErrorState;

/* Initialize the error system. Call once at startup.
 * state: pointer to ErrorState struct (owned by caller) */
void error_init(ErrorState *state);

/* Shut down the error system. */
void error_shutdown(void);

/* Set the root error (clears any previous chain). */
void error_set(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Prepend context to the existing error. Produces chains like:
 * "load_gamedata: level_load: fopen(/path): Permission denied" */
void error_wrap(const char *format, ...) __attribute__((format(printf, 1, 2)));

/* Return the current error string, or NULL if no error is set. */
[[nodiscard]] const char *error_get(void);

/* Clear the error state. Call after logging at the top-level boundary. */
void error_clear(void);

#endif
