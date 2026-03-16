/*
 * Minimal zlog stub for Android — routes all logging to logcat.
 * Replaces the full zlog library which depends on POSIX APIs
 * (glob, memfd_create, etc.) unavailable on Android.
 */
#ifdef __ANDROID__

#include <android/log.h>
#include <stdarg.h>
#include <stddef.h>

#define TAG "sleipner"

int zlog_init(const char *config)
{
    (void)config;
    return 0;
}

int zlog_init_from_string(const char *config_string)
{
    (void)config_string;
    return 0;
}

void zlog_fini(void) {}

int dzlog_set_category(const char *cname)
{
    (void)cname;
    return 0;
}

void dzlog(const char *file, size_t filelen, const char *func, size_t funclen, long line, int level, const char *format,
           ...)
{
    (void)file;
    (void)filelen;
    (void)func;
    (void)funclen;
    (void)line;

    int prio;
    if (level <= 20) {
        prio = ANDROID_LOG_FATAL;
    } else if (level <= 40) {
        prio = ANDROID_LOG_ERROR;
    } else if (level <= 60) {
        prio = ANDROID_LOG_WARN;
    } else if (level <= 80) {
        prio = ANDROID_LOG_INFO;
    } else {
        prio = ANDROID_LOG_DEBUG;
    }

    va_list args;
    va_start(args, format);
    __android_log_vprint(prio, TAG, format, args);
    va_end(args);
}

#endif
