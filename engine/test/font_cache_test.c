#include "fff.h"
#include "unity.h"

#include "raylib.h"

DEFINE_FFF_GLOBALS;

#include "../src/font_cache.c" // NOLINT(bugprone-suspicious-include)

/* font_cache_get / font_cache_cleanup are not exercised by these tests
 * (font_cache_find is pure and headless-safe; the raylib calls are
 * not), but font_cache.c references these symbols, so stubs are
 * needed to link the test binary. The NOLINT covers the
 * easily-swappable-parameters warning that fires on raylib's own API
 * shape — we cannot change the upstream signature. */
/* NOLINTBEGIN(bugprone-easily-swappable-parameters) */
Font LoadFontFromMemory(const char *fileType,
                        const unsigned char *fileData,
                        int dataSize,
                        int fontSize,
                        const int *codepoints,
                        int codepointCount)
{
    (void)fileType;
    (void)fileData;
    (void)dataSize;
    (void)fontSize;
    (void)codepoints;
    (void)codepointCount;
    return (Font){0};
}
/* NOLINTEND(bugprone-easily-swappable-parameters) */

bool IsFontValid(Font font)
{
    (void)font;
    return false;
}

void UnloadFont(Font font)
{
    (void)font;
}

static const unsigned char sentinel_asset_a[] = {0xAA};
static const unsigned char sentinel_asset_b[] = {0xBB};

void setUp(void) {}
void tearDown(void) {}

void test_font_cache_find_returns_index_on_exact_match(void)
{
    FontCacheEntry entries[] = {
        {.data = sentinel_asset_a, .size = 32, .font = (Font){0}},
        {.data = sentinel_asset_b, .size = 64, .font = (Font){0}},
    };
    vec_font_cache cache = {.data = entries, .count = 2};
    TEST_ASSERT_EQUAL_INT(0, font_cache_find(&cache, sentinel_asset_a, 32));
    TEST_ASSERT_EQUAL_INT(1, font_cache_find(&cache, sentinel_asset_b, 64));
}

void test_font_cache_find_returns_negative_one_for_same_data_different_size(void)
{
    FontCacheEntry entries[] = {
        {.data = sentinel_asset_a, .size = 32, .font = (Font){0}},
    };
    vec_font_cache cache = {.data = entries, .count = 1};
    TEST_ASSERT_EQUAL_INT(-1, font_cache_find(&cache, sentinel_asset_a, 64));
}

void test_font_cache_find_returns_negative_one_for_different_data(void)
{
    FontCacheEntry entries[] = {
        {.data = sentinel_asset_a, .size = 32, .font = (Font){0}},
    };
    vec_font_cache cache = {.data = entries, .count = 1};
    TEST_ASSERT_EQUAL_INT(-1, font_cache_find(&cache, sentinel_asset_b, 32));
}

void test_font_cache_find_returns_negative_one_for_empty_cache(void)
{
    vec_font_cache cache = {0};
    TEST_ASSERT_EQUAL_INT(-1, font_cache_find(&cache, sentinel_asset_a, 32));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_font_cache_find_returns_index_on_exact_match);
    RUN_TEST(test_font_cache_find_returns_negative_one_for_same_data_different_size);
    RUN_TEST(test_font_cache_find_returns_negative_one_for_different_data);
    RUN_TEST(test_font_cache_find_returns_negative_one_for_empty_cache);
    return UNITY_END();
}
