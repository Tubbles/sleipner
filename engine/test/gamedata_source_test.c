#include "unity.h"

#include "diag.h"
#include "error.h"
#include "game.h"
#include "gamedata_source.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* Real rendering never runs headlessly; this stands in for the texture
 * registry lookup game_load_gamedata needs, mirroring test_helpers.c's
 * test_dummy_texture_lookup. */
static Texture2D gamedata_source_test_dummy_texture;

static Texture2D *gamedata_source_test_texture_lookup(const char *texture_name, void *user_data)
{
    (void)texture_name;
    (void)user_data;
    return &gamedata_source_test_dummy_texture;
}

#if defined(SLEIPNER_EMBED_GAMEDATA)
/* D40: with no file at the resolved gamedata path, a build compiled with
 * SLEIPNER_EMBED_GAMEDATA must still load real gamedata -- from the copy
 * of data/gamedata.toml embedded at build time via the asset pipeline
 * (gamedata_source.c). Feeds the resolved source through game_load_gamedata,
 * the same as production's load_gamedata (main.c) does, so the test proves
 * the fallback content actually parses into blueprints/a level, not just
 * that gamedata_source_read returns a non-null pointer. Only compiled in
 * when the CMake option is ON (true under the linux preset -- see
 * CMakePresets.json), matching how the option gates ASSET(gamedata_toml)
 * everywhere else. */
void test_gamedata_source_falls_back_to_embedded_when_file_absent(void)
{
    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));

    const char *missing_path = "/nonexistent/sleipner_gamedata_source_test/gamedata.toml";
    GamedataSource source = gamedata_source_read(&state, missing_path);
    TEST_ASSERT_NOT_NULL_MESSAGE(source.toml_string, error_get(&state.error));
    TEST_ASSERT_FALSE(source.from_file);

    TEST_ASSERT_TRUE(game_load_gamedata(&diag, &state,
                                        (GamedataParams){.toml_string = source.toml_string,
                                                         .level_name = nullptr,
                                                         .texture_lookup = gamedata_source_test_texture_lookup}));
    TEST_ASSERT_GREATER_THAN(0, state.gamedata.blueprints.entries.count);
    TEST_ASSERT_TRUE(state.gamedata.player_index >= 0);
    TEST_ASSERT_GREATER_THAN(0, state.gamedata.current_level.entities.count);

    game_free(&diag, &state);
}
#else
/* Mirrors the guard on assets.h's DECLARE_ASSET(gamedata_toml): a build
 * with the option OFF has no embedded copy to fall back to, so there is
 * nothing to exercise here. Ignored rather than omitted so the suite's
 * test count/name doesn't shift between ON/OFF builds. */
void test_gamedata_source_falls_back_to_embedded_when_file_absent(void)
{
    TEST_IGNORE_MESSAGE("SLEIPNER_EMBED_GAMEDATA is OFF in this build; no embedded fallback to exercise");
}
#endif

/* D40: file-first. When a gamedata.toml file IS present at the resolved
 * path, gamedata_source_read must use its content rather than any
 * embedded copy -- regardless of whether SLEIPNER_EMBED_GAMEDATA is
 * compiled in, so this test runs unconditionally. The fixture is a
 * minimal TOML with names distinct from the real data/gamedata.toml (and
 * from the embedded copy, when present), so a false pass from the wrong
 * source being loaded would be caught by the string/level-name asserts. */
void test_gamedata_source_prefers_file_when_present(void)
{
    char tmp_path[256];
    (void)snprintf(tmp_path, sizeof(tmp_path), "/tmp/sleipner_gamedata_source_test_%d.toml", (int)getpid());

    const char *distinct_gamedata = "[[blueprint]]\n"
                                    "name = \"gamedata_source_test_player\"\n"
                                    "texture = \"player.png\"\n"
                                    "src = [0, 0, 32, 32]\n"
                                    "behavior = \"player\"\n"
                                    "\n"
                                    "[[level]]\n"
                                    "name = \"gamedata_source_test_level\"\n"
                                    "size = [320, 240]\n"
                                    "\n"
                                    "[[level.entity]]\n"
                                    "blueprint = \"gamedata_source_test_player\"\n"
                                    "pos = [160, 120]\n";

    FILE *file = fopen(tmp_path, "we");
    TEST_ASSERT_NOT_NULL(file);
    (void)fprintf(file, "%s", distinct_gamedata);
    (void)fclose(file);

    GameState state = {0};
    Diag diag = {&state.error, &state.debug};
    TEST_ASSERT_TRUE(game_init(&diag, &state, (RectU32){320, 240}));

    GamedataSource source = gamedata_source_read(&state, tmp_path);
    TEST_ASSERT_NOT_NULL_MESSAGE(source.toml_string, error_get(&state.error));
    TEST_ASSERT_TRUE(source.from_file);
    TEST_ASSERT_NOT_NULL(strstr(source.toml_string, "gamedata_source_test_level"));

    TEST_ASSERT_TRUE(game_load_gamedata(&diag, &state,
                                        (GamedataParams){.toml_string = source.toml_string,
                                                         .level_name = nullptr,
                                                         .texture_lookup = gamedata_source_test_texture_lookup}));
    TEST_ASSERT_EQUAL_STRING("gamedata_source_test_level", state.gamedata.current_level.name.ptr);

    game_free(&diag, &state);
    (void)remove(tmp_path);
}
