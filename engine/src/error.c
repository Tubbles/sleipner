#include "error.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void error_set(ErrorState *err, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(err->error_buffer, ERROR_MSG_LEN, format, args);
    va_end(args);
}

void error_wrap(ErrorState *err, const char *format, ...)
{
    if (err->error_buffer[0] == '\0') {
        return;
    }

    char context[ERROR_MSG_LEN];
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(context, ERROR_MSG_LEN, format, args);
    va_end(args);

    char combined[ERROR_MSG_LEN];
    (void)snprintf(combined, ERROR_MSG_LEN, "%s: %s", context, err->error_buffer);
    memcpy(err->error_buffer, combined, ERROR_MSG_LEN);
}

const char *error_get(const ErrorState *err)
{
    if (err->error_buffer[0] == '\0') {
        return nullptr;
    }
    return err->error_buffer;
}

void error_clear(ErrorState *err)
{
    err->error_buffer[0] = '\0';
}
