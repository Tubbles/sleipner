#include "fff.h"
#include "unity.h"

#include "../src/strv.c" // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"  // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

void setUp(void) {}
void tearDown(void) {}

void test_str_from_cstr_len(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    TEST_ASSERT_EQUAL_size_t(5, str.len);
    str_free(&test_heap_alloc, &str);
}

void test_str_from_cstr_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    TEST_ASSERT_EQUAL_STRING("hello", str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_from_cstr_null_terminated(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[str.len]);
    str_free(&test_heap_alloc, &str);
}

void test_str_from_cstr_empty_nonnull_ptr(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, ""));
    TEST_ASSERT_EQUAL_size_t(0, str.len);
    TEST_ASSERT_NOT_NULL(str.ptr);
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[0]);
    str_free(&test_heap_alloc, &str);
}

void test_str_from_strv_len(void)
{
    Strv strv = strv_from_cstr("world");
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_strv(&test_heap_alloc, &str, strv));
    TEST_ASSERT_EQUAL_size_t(5, str.len);
    str_free(&test_heap_alloc, &str);
}

void test_str_from_strv_content(void)
{
    Strv strv = strv_from_cstr("world");
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_strv(&test_heap_alloc, &str, strv));
    TEST_ASSERT_EQUAL_STRING("world", str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_from_strv_subview(void)
{
    /* Construct from a sub-view — no null terminator in the source */
    Strv strv = strv_from_cstr("foo:bar");
    Strv head = strv_split(&strv, ':');
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_strv(&test_heap_alloc, &str, head));
    TEST_ASSERT_EQUAL_size_t(3, str.len);
    TEST_ASSERT_EQUAL_STRING("foo", str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_to_strv_len_excludes_null(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    Strv strv = str_to_strv(str);
    /* len must be 5, not 6 — null terminator is not counted */
    TEST_ASSERT_EQUAL_size_t(5, strv.len);
    str_free(&test_heap_alloc, &str);
}

void test_str_to_strv_ptr_matches(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    Strv strv = str_to_strv(str);
    TEST_ASSERT_EQUAL_PTR(str.ptr, strv.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_push_char_len(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "ab"));
    TEST_ASSERT_TRUE(str_push_char(&test_heap_alloc, &str, 'c'));
    TEST_ASSERT_EQUAL_size_t(3, str.len);
    str_free(&test_heap_alloc, &str);
}

void test_str_push_char_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "ab"));
    TEST_ASSERT_TRUE(str_push_char(&test_heap_alloc, &str, 'c'));
    TEST_ASSERT_EQUAL_STRING("abc", str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_push_char_null_terminated(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "ab"));
    TEST_ASSERT_TRUE(str_push_char(&test_heap_alloc, &str, 'c'));
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[str.len]);
    str_free(&test_heap_alloc, &str);
}

void test_str_append_cstr_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "foo"));
    TEST_ASSERT_TRUE(str_append_cstr(&test_heap_alloc, &str, "bar"));
    TEST_ASSERT_EQUAL_size_t(6, str.len);
    TEST_ASSERT_EQUAL_STRING("foobar", str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_append_strv_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    Strv suffix = strv_from_cstr(", world");
    TEST_ASSERT_TRUE(str_append_strv(&test_heap_alloc, &str, suffix));
    TEST_ASSERT_EQUAL_size_t(12, str.len);
    TEST_ASSERT_EQUAL_STRING("hello, world", str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_append_strv_subview(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "key="));
    Strv rest = strv_from_cstr("type:value");
    (void)strv_split(&rest, ':');
    TEST_ASSERT_TRUE(str_append_strv(&test_heap_alloc, &str, rest));
    TEST_ASSERT_EQUAL_STRING("key=value", str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_append_grows_beyond_initial_cap(void)
{
    /* Appending past STR_INITIAL_CAP (16) must succeed and keep content correct */
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, ""));
    for (int index = 0; index < 32; index++) {
        TEST_ASSERT_TRUE(str_push_char(&test_heap_alloc, &str, 'x'));
    }
    TEST_ASSERT_EQUAL_size_t(32, str.len);
    TEST_ASSERT_EQUAL_CHAR('x', str.ptr[0]);
    TEST_ASSERT_EQUAL_CHAR('x', str.ptr[31]);
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[32]);
    str_free(&test_heap_alloc, &str);
}

void test_str_clear_resets_len(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    str_clear(&str);
    TEST_ASSERT_EQUAL_size_t(0, str.len);
    str_free(&test_heap_alloc, &str);
}

void test_str_clear_keeps_allocation(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    size_t cap_before = str.cap;
    str_clear(&str);
    TEST_ASSERT_EQUAL_size_t(cap_before, str.cap);
    TEST_ASSERT_NOT_NULL(str.ptr);
    str_free(&test_heap_alloc, &str);
}

void test_str_clear_null_terminates(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    str_clear(&str);
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[0]);
    str_free(&test_heap_alloc, &str);
}

void test_str_free_zeros_struct(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(&test_heap_alloc, &str, "hello"));
    str_free(&test_heap_alloc, &str);
    TEST_ASSERT_NULL(str.ptr);
    TEST_ASSERT_EQUAL_size_t(0, str.len);
    TEST_ASSERT_EQUAL_size_t(0, str.cap);
}

void test_str_free_on_zero_is_safe(void)
{
    Str str = {0};
    str_free(&test_heap_alloc, &str); /* must not crash */
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_str_from_cstr_len);
    RUN_TEST(test_str_from_cstr_content);
    RUN_TEST(test_str_from_cstr_null_terminated);
    RUN_TEST(test_str_from_cstr_empty_nonnull_ptr);
    RUN_TEST(test_str_from_strv_len);
    RUN_TEST(test_str_from_strv_content);
    RUN_TEST(test_str_from_strv_subview);
    RUN_TEST(test_str_to_strv_len_excludes_null);
    RUN_TEST(test_str_to_strv_ptr_matches);
    RUN_TEST(test_str_push_char_len);
    RUN_TEST(test_str_push_char_content);
    RUN_TEST(test_str_push_char_null_terminated);
    RUN_TEST(test_str_append_cstr_content);
    RUN_TEST(test_str_append_strv_content);
    RUN_TEST(test_str_append_strv_subview);
    RUN_TEST(test_str_append_grows_beyond_initial_cap);
    RUN_TEST(test_str_clear_resets_len);
    RUN_TEST(test_str_clear_keeps_allocation);
    RUN_TEST(test_str_clear_null_terminates);
    RUN_TEST(test_str_free_zeros_struct);
    RUN_TEST(test_str_free_on_zero_is_safe);
    return UNITY_END();
}
