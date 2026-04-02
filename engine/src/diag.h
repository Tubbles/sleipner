#ifndef DIAG_H
#define DIAG_H

#include "debug.h"
#include "error.h"

typedef struct {
    ErrorState *error;
    DebugState *debug;
} Diag;

#endif
