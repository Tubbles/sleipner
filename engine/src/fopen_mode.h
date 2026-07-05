#pragma once

/* Portable fopen mode strings. The trailing "e" (POSIX/glibc close-on-exec
 * extension) keeps file descriptors from leaking into child processes on
 * Linux; Windows has no equivalent so plain mode strings are used there.
 * Shared by every module that opens files directly (main.c, file_backup.c,
 * save.c) so the platform switch lives in exactly one place. */
#ifdef _WIN32
#define FOPEN_READ "r"
#define FOPEN_WRITE "w"
#define FOPEN_APPEND "a"
#else
#define FOPEN_READ "re"
#define FOPEN_WRITE "we"
#define FOPEN_APPEND "ae"
#endif
