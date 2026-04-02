#pragma once

#include "debug.h"
#include "error.h"

typedef struct {
    ErrorState *error;
    DebugState *debug;
} Diag;
