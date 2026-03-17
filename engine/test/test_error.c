#include "unity.h"
#include "error.h"

#include <string.h>

void test_error_initially_null(void)
{
    error_clear();
    TEST_ASSERT_NULL(error_get());
}

void test_error_set_and_get(void)
{
    error_clear();
    error_set("something broke");
    TEST_ASSERT_EQUAL_STRING("something broke", error_get());
}

void test_error_set_with_format(void)
{
    error_clear();
    error_set("fopen(%s): %s", "/tmp/test", "No such file");
    TEST_ASSERT_EQUAL_STRING("fopen(/tmp/test): No such file", error_get());
}

void test_error_wrap_prepends_context(void)
{
    error_clear();
    error_set("permission denied");
    error_wrap("fopen(%s)", "/data/file");
    error_wrap("load_level");
    TEST_ASSERT_EQUAL_STRING("load_level: fopen(/data/file): permission denied", error_get());
}

void test_error_wrap_on_empty_is_noop(void)
{
    error_clear();
    error_wrap("should not appear");
    TEST_ASSERT_NULL(error_get());
}

void test_error_clear_resets(void)
{
    error_set("some error");
    TEST_ASSERT_NOT_NULL(error_get());
    error_clear();
    TEST_ASSERT_NULL(error_get());
}

void test_error_set_overwrites_previous(void)
{
    error_clear();
    error_set("first error");
    error_set("second error");
    TEST_ASSERT_EQUAL_STRING("second error", error_get());
}
