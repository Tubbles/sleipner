#pragma once

#include <stdio.h>
#include <stdlib.h>

/* Hard assert that always fires — never compiled out by NDEBUG.
 * Prints file:line and the failed expression, then aborts. */
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            (void)fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__);                            \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
