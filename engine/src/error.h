#ifndef ERROR_H
#define ERROR_H

#define ERROR_MSG_LEN 512

/* Set the root error (clears any previous chain). */
struct EngineContext;

void error_set(struct EngineContext *ctx, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* Prepend context to the existing error. Produces chains like:
 * "load_gamedata: level_load: fopen(/path): Permission denied" */
void error_wrap(struct EngineContext *ctx, const char *format, ...) __attribute__((format(printf, 2, 3)));

/* Return the current error string, or NULL if no error is set. */
[[nodiscard]] const char *error_get(struct EngineContext *ctx);

/* Clear the error state. Call after logging at the top-level boundary. */
void error_clear(struct EngineContext *ctx);

#endif
