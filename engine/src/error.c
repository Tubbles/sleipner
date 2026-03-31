#include "error.h"
#include "engine_context.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void error_set(struct EngineContext *ctx, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(ctx->error.error_buffer, ERROR_MSG_LEN, format, args);
    va_end(args);
}

void error_wrap(struct EngineContext *ctx, const char *format, ...)
{
    if (ctx->error.error_buffer[0] == '\0') {
        return;
    }

    char context[ERROR_MSG_LEN];
    va_list args;
    va_start(args, format);
    // NOLINTNEXTLINE(clang-analyzer-security.VAList) false positive, LLVM #40656
    (void)vsnprintf(context, ERROR_MSG_LEN, format, args);
    va_end(args);

    char combined[ERROR_MSG_LEN];
    (void)snprintf(combined, ERROR_MSG_LEN, "%s: %s", context, ctx->error.error_buffer);
    memcpy(ctx->error.error_buffer, combined, ERROR_MSG_LEN);
}

const char *error_get(struct EngineContext *ctx)
{
    if (ctx->error.error_buffer[0] == '\0') {
        return nullptr;
    }
    return ctx->error.error_buffer;
}

void error_clear(struct EngineContext *ctx)
{
    ctx->error.error_buffer[0] = '\0';
}
