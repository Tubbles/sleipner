#include "toml_emitter.h"
#include "blueprint.h"
#include "entity.h"
#include "level.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int emit_append(char *buffer, int capacity, int offset, const char *format, ...)
    __attribute__((format(printf, 4, 5)));

static int emit_append(char *buffer, int capacity, int offset, const char *format, ...)
{
    if (offset < 0) {
        return offset;
    }

    va_list args;
    va_start(args, format);
    int remaining = capacity - offset;
    int written = vsnprintf(buffer + offset, (size_t)(remaining > 0 ? remaining : 0), format, args); // NOLINT(clang-analyzer-security.VAList) false positive, LLVM #40656
    va_end(args);

    if (written < 0 || offset + written >= capacity) {
        return -1;
    }
    return offset + written;
}

static int emit_blueprints(char *buffer, int capacity, int offset, const BlueprintTable *blueprints)
{
    for (int index = 0; index < blueprints->count; index++) {
        const Blueprint *blueprint = &blueprints->entries[index];

        offset = emit_append(buffer, capacity, offset, "[[blueprint]]\n");
        offset = emit_append(buffer, capacity, offset, "name = \"%s\"\n", blueprint->name);
        offset = emit_append(buffer, capacity, offset, "texture = \"%s\"\n", blueprint->texture_name);
        offset = emit_append(buffer, capacity, offset, "src = [%d, %d, %d, %d]\n", (int)blueprint->source.x,
                             (int)blueprint->source.y, (int)blueprint->source.width, (int)blueprint->source.height);
        offset = emit_append(buffer, capacity, offset, "collision_offset = [%d, %d]\n",
                             (int)blueprint->collision_offset.x, (int)blueprint->collision_offset.y);
        offset = emit_append(buffer, capacity, offset, "collision_size = [%d, %d]\n", (int)blueprint->collision_size.x,
                             (int)blueprint->collision_size.y);
        offset = emit_append(buffer, capacity, offset, "\n");
    }
    return offset;
}

static int emit_levels(char *buffer, int capacity, int offset, const Level *levels, int level_count)
{
    for (int level_index = 0; level_index < level_count; level_index++) {
        const Level *level = &levels[level_index];

        offset = emit_append(buffer, capacity, offset, "[[level]]\n");
        offset = emit_append(buffer, capacity, offset, "name = \"%s\"\n", level->name);
        offset = emit_append(buffer, capacity, offset, "size = [%d, %d]\n", level->width, level->height);

        if (level->music_name[0] != '\0') {
            offset = emit_append(buffer, capacity, offset, "music = \"%s\"\n", level->music_name);
        }
        offset = emit_append(buffer, capacity, offset, "\n");

        for (int entity_index = 0; entity_index < level->entity_count; entity_index++) {
            const Entity *entity = &level->entities[entity_index];

            offset = emit_append(buffer, capacity, offset, "[[level.entity]]\n");
            offset = emit_append(buffer, capacity, offset, "blueprint = \"%s\"\n", entity->blueprint_name);
            offset = emit_append(buffer, capacity, offset, "pos = [%d, %d]\n", (int)entity->position.x,
                                 (int)entity->position.y);
            offset = emit_append(buffer, capacity, offset, "\n");
        }
    }
    return offset;
}

int toml_emit_gamedata(
    char *buffer, int capacity, const BlueprintTable *blueprints, const Level *levels, int level_count)
{
    int offset = 0;

    offset = emit_blueprints(buffer, capacity, offset, blueprints);
    offset = emit_levels(buffer, capacity, offset, levels, level_count);

    return offset;
}
