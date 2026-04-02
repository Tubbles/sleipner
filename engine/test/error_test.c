#include "fff.h"
#include "unity.h"

#include "../src/error.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

static ErrorState ctx;

void setUp(void) {}
void tearDown(void) {}

void test_error_initially_null(void)
{
    error_clear(&ctx);
    TEST_ASSERT_NULL(error_get(&ctx));
}

void test_error_set_and_get(void)
{
    error_clear(&ctx);
    error_set(&ctx, "something broke");
    TEST_ASSERT_EQUAL_STRING("something broke", error_get(&ctx));
}

void test_error_set_with_format(void)
{
    error_clear(&ctx);
    error_set(&ctx, "fopen(%s): %s", "/tmp/test", "No such file");
    TEST_ASSERT_EQUAL_STRING("fopen(/tmp/test): No such file", error_get(&ctx));
}

void test_error_wrap_prepends_context(void)
{
    error_clear(&ctx);
    error_set(&ctx, "permission denied");
    error_wrap(&ctx, "fopen(%s)", "/data/file");
    error_wrap(&ctx, "load_level");
    TEST_ASSERT_EQUAL_STRING("load_level: fopen(/data/file): permission denied", error_get(&ctx));
}

void test_error_wrap_on_empty_is_noop(void)
{
    error_clear(&ctx);
    error_wrap(&ctx, "should not appear");
    TEST_ASSERT_NULL(error_get(&ctx));
}

void test_error_clear_resets(void)
{
    error_set(&ctx, "some error");
    TEST_ASSERT_NOT_NULL(error_get(&ctx));
    error_clear(&ctx);
    TEST_ASSERT_NULL(error_get(&ctx));
}

void test_error_set_overwrites_previous(void)
{
    error_clear(&ctx);
    error_set(&ctx, "first error");
    error_set(&ctx, "second error");
    TEST_ASSERT_EQUAL_STRING("second error", error_get(&ctx));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_error_initially_null);
    RUN_TEST(test_error_set_and_get);
    RUN_TEST(test_error_set_with_format);
    RUN_TEST(test_error_wrap_prepends_context);
    RUN_TEST(test_error_wrap_on_empty_is_noop);
    RUN_TEST(test_error_clear_resets);
    RUN_TEST(test_error_set_overwrites_previous);
    return UNITY_END();
}
