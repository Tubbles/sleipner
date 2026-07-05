#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

/* raylib filesystem fakes — platform_paths probes these. MakeDirectory's
 * arg0 is captured into a heap-owned copy because the implementation
 * frees the working Str on return; the default fff arg history would
 * be a dangling pointer by the time the assert reads it. */
FAKE_VALUE_FUNC(const char *, GetApplicationDirectory);
FAKE_VALUE_FUNC(bool, FileExists, const char *);
FAKE_VALUE_FUNC(bool, DirectoryExists, const char *);
FAKE_VALUE_FUNC(int, MakeDirectory, const char *);

static char captured_make_directory_arg[256];
static int captured_make_directory_arg_present;
static int capture_make_directory(const char *arg)
{
    captured_make_directory_arg_present = 1;
    if (arg != NULL) {
        size_t length = strlen(arg);
        if (length >= sizeof(captured_make_directory_arg)) {
            length = sizeof(captured_make_directory_arg) - 1;
        }
        memcpy(captured_make_directory_arg, arg, length);
        captured_make_directory_arg[length] = '\0';
    } else {
        captured_make_directory_arg[0] = '\0';
    }
    return MakeDirectory_fake.return_val;
}

#include "test_heap_alloc.h"

#include "../src/strv.c"           // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"            // NOLINT(bugprone-suspicious-include)
#include "../src/error.c"          // NOLINT(bugprone-suspicious-include)
#include "../src/platform_paths.c" // NOLINT(bugprone-suspicious-include)

#include <stdlib.h>
#include <string.h>

static ErrorState err;

void setUp(void)
{
    test_helpers_init();
    err = (ErrorState){0};
    /* Reset every fake. */
    RESET_FAKE(GetApplicationDirectory);
    RESET_FAKE(FileExists);
    RESET_FAKE(DirectoryExists);
    RESET_FAKE(MakeDirectory);
    /* Default: no binary-adjacent override. */
    GetApplicationDirectory_fake.return_val = "/usr/local/bin/";
    FileExists_fake.return_val = false;
    DirectoryExists_fake.return_val = false;
    MakeDirectory_fake.return_val = 0;
    MakeDirectory_fake.custom_fake = capture_make_directory;
    captured_make_directory_arg[0] = '\0';
    captured_make_directory_arg_present = 0;
    /* Wipe env vars so each test sets exactly what it needs. */
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
    unsetenv("HOME");
    unsetenv("APPDATA");
}

void tearDown(void) {}

#if !defined(_WIN32) && !defined(__ANDROID__)

void test_xdg_config_home_resolves_first(void)
{
    setenv("XDG_CONFIG_HOME", "/tmp/xdg", 1);
    setenv("HOME", "/home/should-be-ignored", 1);
    Str path = {0};
    TEST_ASSERT_TRUE(platform_preferences_path(&path, test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_STRING("/tmp/xdg/sleipner/preferences.toml", path.ptr);
    str_free(&path);
}

void test_home_dot_config_falls_back_when_xdg_unset(void)
{
    setenv("HOME", "/home/joe", 1);
    Str path = {0};
    TEST_ASSERT_TRUE(platform_preferences_path(&path, test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_STRING("/home/joe/.config/sleipner/preferences.toml", path.ptr);
    str_free(&path);
}

void test_no_xdg_no_home_returns_error(void)
{
    Str path = {0};
    TEST_ASSERT_FALSE(platform_preferences_path(&path, test_heap_alloc, &err));
    TEST_ASSERT_NOT_NULL(strstr(error_get(&err), "HOME"));
}

void test_binary_adjacent_override_wins(void)
{
    setenv("HOME", "/home/joe", 1);
    GetApplicationDirectory_fake.return_val = "/opt/sleipner/";
    /* The first FileExists call probes <bin_dir>/preferences.toml. */
    FileExists_fake.return_val = true;
    Str path = {0};
    TEST_ASSERT_TRUE(platform_preferences_path(&path, test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_STRING("/opt/sleipner/preferences.toml", path.ptr);
    str_free(&path);
}

void test_ensure_parent_dir_makes_parent(void)
{
    DirectoryExists_fake.return_val = false;
    MakeDirectory_fake.return_val = 0;
    TEST_ASSERT_TRUE(platform_ensure_parent_dir("/tmp/foo/bar/preferences.toml", test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_INT(1, MakeDirectory_fake.call_count);
    TEST_ASSERT_TRUE(captured_make_directory_arg_present);
    TEST_ASSERT_EQUAL_STRING("/tmp/foo/bar", captured_make_directory_arg);
}

void test_ensure_parent_dir_skips_when_exists(void)
{
    DirectoryExists_fake.return_val = true;
    TEST_ASSERT_TRUE(platform_ensure_parent_dir("/tmp/foo/bar/preferences.toml", test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_INT(0, MakeDirectory_fake.call_count);
}

void test_ensure_parent_dir_propagates_failure(void)
{
    DirectoryExists_fake.return_val = false;
    MakeDirectory_fake.return_val = -1;
    TEST_ASSERT_FALSE(platform_ensure_parent_dir("/tmp/foo/bar/preferences.toml", test_heap_alloc, &err));
    TEST_ASSERT_NOT_NULL(strstr(error_get(&err), "MakeDirectory"));
}

void test_saves_dir_xdg_data_home_resolves_first(void)
{
    setenv("XDG_DATA_HOME", "/tmp/xdg-data", 1);
    setenv("HOME", "/home/should-be-ignored", 1);
    Str path = {0};
    TEST_ASSERT_TRUE(platform_saves_dir(&path, test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_STRING("/tmp/xdg-data/sleipner/saves/", path.ptr);
    str_free(&path);
}

void test_saves_dir_home_local_share_falls_back_when_xdg_unset(void)
{
    setenv("HOME", "/home/joe", 1);
    Str path = {0};
    TEST_ASSERT_TRUE(platform_saves_dir(&path, test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_STRING("/home/joe/.local/share/sleipner/saves/", path.ptr);
    str_free(&path);
}

void test_saves_dir_no_xdg_no_home_returns_error(void)
{
    Str path = {0};
    TEST_ASSERT_FALSE(platform_saves_dir(&path, test_heap_alloc, &err));
    TEST_ASSERT_NOT_NULL(strstr(error_get(&err), "HOME"));
}

void test_saves_dir_ignores_binary_adjacent_files(void)
{
    /* Unlike platform_preferences_path, platform_saves_dir has no
     * binary-adjacent override, so FileExists must not even be
     * consulted. */
    setenv("HOME", "/home/joe", 1);
    FileExists_fake.return_val = true;
    Str path = {0};
    TEST_ASSERT_TRUE(platform_saves_dir(&path, test_heap_alloc, &err));
    TEST_ASSERT_EQUAL_STRING("/home/joe/.local/share/sleipner/saves/", path.ptr);
    TEST_ASSERT_EQUAL_INT(0, FileExists_fake.call_count);
    str_free(&path);
}

void test_ensure_saves_dir_makes_dir(void)
{
    DirectoryExists_fake.return_val = false;
    MakeDirectory_fake.return_val = 0;
    TEST_ASSERT_TRUE(platform_ensure_saves_dir("/tmp/xdg-data/sleipner/saves", &err));
    TEST_ASSERT_EQUAL_INT(1, MakeDirectory_fake.call_count);
    TEST_ASSERT_TRUE(captured_make_directory_arg_present);
    TEST_ASSERT_EQUAL_STRING("/tmp/xdg-data/sleipner/saves", captured_make_directory_arg);
}

void test_ensure_saves_dir_is_idempotent_when_it_already_exists(void)
{
    DirectoryExists_fake.return_val = true;
    TEST_ASSERT_TRUE(platform_ensure_saves_dir("/tmp/xdg-data/sleipner/saves", &err));
    TEST_ASSERT_EQUAL_INT(0, MakeDirectory_fake.call_count);
    /* Calling a second time (still "exists") stays a no-op — proves
     * idempotency rather than just a single successful call. */
    TEST_ASSERT_TRUE(platform_ensure_saves_dir("/tmp/xdg-data/sleipner/saves", &err));
    TEST_ASSERT_EQUAL_INT(0, MakeDirectory_fake.call_count);
}

void test_ensure_saves_dir_propagates_failure(void)
{
    DirectoryExists_fake.return_val = false;
    MakeDirectory_fake.return_val = -1;
    TEST_ASSERT_FALSE(platform_ensure_saves_dir("/tmp/xdg-data/sleipner/saves", &err));
    TEST_ASSERT_NOT_NULL(strstr(error_get(&err), "MakeDirectory"));
}

#endif

int main(void)
{
    UNITY_BEGIN();
#if !defined(_WIN32) && !defined(__ANDROID__)
    RUN_TEST(test_xdg_config_home_resolves_first);
    RUN_TEST(test_home_dot_config_falls_back_when_xdg_unset);
    RUN_TEST(test_no_xdg_no_home_returns_error);
    RUN_TEST(test_binary_adjacent_override_wins);
    RUN_TEST(test_ensure_parent_dir_makes_parent);
    RUN_TEST(test_ensure_parent_dir_skips_when_exists);
    RUN_TEST(test_ensure_parent_dir_propagates_failure);
    RUN_TEST(test_saves_dir_xdg_data_home_resolves_first);
    RUN_TEST(test_saves_dir_home_local_share_falls_back_when_xdg_unset);
    RUN_TEST(test_saves_dir_no_xdg_no_home_returns_error);
    RUN_TEST(test_saves_dir_ignores_binary_adjacent_files);
    RUN_TEST(test_ensure_saves_dir_makes_dir);
    RUN_TEST(test_ensure_saves_dir_is_idempotent_when_it_already_exists);
    RUN_TEST(test_ensure_saves_dir_propagates_failure);
#endif
    return UNITY_END();
}
