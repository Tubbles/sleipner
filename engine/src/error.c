#include "error.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Static pointer to externally owned ErrorState */
static ErrorState *error_state = nullptr;

void error_init(ErrorState *state)
{
    /* Store pointer to externally owned state */
    error_state = state;
    /* Initialize the state */
    error_state->buffer[0] = '\0';
}

void error_shutdown(void)
{
    /* Clear pointer to avoid dangling reference */
    error_state = nullptr;
}

void error_set(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(error_state->buffer, ERROR_MSG_LEN, format, args);
    va_end(args);
}

void error_wrap(const char *format, ...)
{
    if (error_state->buffer[0] == '\0') {
        return;
    }

    char context[ERROR_MSG_LEN];
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(context, ERROR_MSG_LEN, format, args);
    va_end(args);

    char combined[ERROR_MSG_LEN];
    (void)snprintf(combined, ERROR_MSG_LEN, "%s: %s", context, error_state->buffer);
    memcpy(error_state->buffer, combined, ERROR_MSG_LEN);
}

const char *error_get(void)
{
    if (error_state->buffer[0] == '\0') {
        return NULL;
    }
    return error_state->buffer;
}

void error_clear(void)
{
    error_state->buffer[0] = '\0';
}
