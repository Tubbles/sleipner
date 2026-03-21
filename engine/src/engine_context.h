#ifndef ENGINE_CONTEXT_H
#define ENGINE_CONTEXT_H

#include "game.h"
#include "audio.h"
#include <stdio.h>

#define ERROR_MSG_LEN 512
#define DEBUG_LOG_LINES 20
#define DEBUG_LOG_LINE_LEN 128

typedef struct {
    char error_buffer[ERROR_MSG_LEN];
} ErrorState;

typedef struct {
    char log_lines[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LEN];
    int log_head;
    int log_count;
    FILE *trace_file;
} DebugState;

/* The root engine context holding all state to avoid static variables. */
typedef struct EngineContext {
    GameState game;
    ErrorState error;
    DebugState debug;
} EngineContext;

#endif
