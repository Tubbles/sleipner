#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

#include "../src/strv.c"        // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"         // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"       // NOLINT(bugprone-suspicious-include)
#include "../src/preferences.c" // NOLINT(bugprone-suspicious-include)

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static ErrorState err;
static Preferences prefs;
static char tmp_path[256];

void setUp(void)
{
    test_helpers_init();
    err = (ErrorState){0};
    preferences_init_defaults(&prefs, test_heap_alloc);
    /* Per-test temp file path, named after the test runner pid so
     * parallel test runs don't collide. */
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/sleipner_preferences_test_%d.toml", (int)getpid());
}

void tearDown(void)
{
    str_free(&prefs.data_dir);
    (void)unlink(tmp_path);
}

void test_defaults_have_data_dir_set(void)
{
    TEST_ASSERT_NOT_NULL(prefs.data_dir.ptr);
    TEST_ASSERT_TRUE(prefs.data_dir.len > 0);
}

void test_load_missing_file_keeps_defaults(void)
{
    /* tmp_path is guaranteed not to exist (tearDown deletes it). */
    TEST_ASSERT_TRUE(preferences_load(&prefs, &err, tmp_path));
    TEST_ASSERT_NOT_NULL(prefs.data_dir.ptr);
    TEST_ASSERT_TRUE(prefs.data_dir.len > 0);
}

void test_load_overrides_data_dir(void)
{
    FILE *file = fopen(tmp_path, "we");
    TEST_ASSERT_NOT_NULL(file);
    fprintf(file, "[paths]\ndata_dir = \"/custom/path/\"\n");
    fclose(file);

    TEST_ASSERT_TRUE(preferences_load(&prefs, &err, tmp_path));
    TEST_ASSERT_EQUAL_STRING("/custom/path/", prefs.data_dir.ptr);
}

void test_load_appends_trailing_slash_when_missing(void)
{
    FILE *file = fopen(tmp_path, "we");
    TEST_ASSERT_NOT_NULL(file);
    fprintf(file, "[paths]\ndata_dir = \"data\"\n");
    fclose(file);

    TEST_ASSERT_TRUE(preferences_load(&prefs, &err, tmp_path));
    TEST_ASSERT_EQUAL_STRING("data/", prefs.data_dir.ptr);
}

void test_load_keeps_default_when_field_absent(void)
{
    FILE *file = fopen(tmp_path, "we");
    TEST_ASSERT_NOT_NULL(file);
    fprintf(file, "[paths]\n# data_dir intentionally omitted\n");
    fclose(file);

    /* Snapshot the default before loading. */
    Str saved_default = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_append_cstr(&saved_default, prefs.data_dir.ptr));

    TEST_ASSERT_TRUE(preferences_load(&prefs, &err, tmp_path));
    TEST_ASSERT_EQUAL_STRING(saved_default.ptr, prefs.data_dir.ptr);
    str_free(&saved_default);
}

void test_load_propagates_parse_error(void)
{
    FILE *file = fopen(tmp_path, "we");
    TEST_ASSERT_NOT_NULL(file);
    fprintf(file, "[paths\nbroken garbage\n");
    fclose(file);

    TEST_ASSERT_FALSE(preferences_load(&prefs, &err, tmp_path));
    TEST_ASSERT_NOT_NULL(strstr(error_get(&err), "toml_parse_file"));
}

void test_save_then_load_round_trip(void)
{
    str_clear(&prefs.data_dir);
    TEST_ASSERT_TRUE(str_append_cstr(&prefs.data_dir, "/round/trip/"));

    TEST_ASSERT_TRUE(preferences_save(&prefs, &err, tmp_path));

    Preferences reloaded = {0};
    preferences_init_defaults(&reloaded, test_heap_alloc);
    TEST_ASSERT_TRUE(preferences_load(&reloaded, &err, tmp_path));
    TEST_ASSERT_EQUAL_STRING("/round/trip/", reloaded.data_dir.ptr);
    str_free(&reloaded.data_dir);
}

void test_save_to_unwritable_path_fails(void)
{
    /* /proc is read-only on Linux; opening for write fails. */
    TEST_ASSERT_FALSE(preferences_save(&prefs, &err, "/proc/sleipner_preferences_unwritable.toml"));
    TEST_ASSERT_NOT_NULL(strstr(error_get(&err), "fopen"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_have_data_dir_set);
    RUN_TEST(test_load_missing_file_keeps_defaults);
    RUN_TEST(test_load_overrides_data_dir);
    RUN_TEST(test_load_appends_trailing_slash_when_missing);
    RUN_TEST(test_load_keeps_default_when_field_absent);
    RUN_TEST(test_load_propagates_parse_error);
    RUN_TEST(test_save_then_load_round_trip);
    RUN_TEST(test_save_to_unwritable_path_fails);
    return UNITY_END();
}
