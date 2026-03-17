#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char error_buffer[ERROR_MSG_LEN];

void error_set(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(error_buffer, ERROR_MSG_LEN, format, args);
    va_end(args);
}

void error_wrap(const char *format, ...)
{
    if (error_buffer[0] == '\0') {
        return;
    }

    char context[ERROR_MSG_LEN];
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(context, ERROR_MSG_LEN, format, args);
    va_end(args);

    char combined[ERROR_MSG_LEN];
    (void)snprintf(combined, ERROR_MSG_LEN, "%s: %s", context, error_buffer);
    memcpy(error_buffer, combined, ERROR_MSG_LEN);
}

const char *error_get(void)
{
    if (error_buffer[0] == '\0') {
        return NULL;
    }
    return error_buffer;
}

void error_clear(void)
{
    error_buffer[0] = '\0';
}
