#include "unity.h"

#include "str.h"
#include "strv.h"

void test_str_from_cstr_len(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    TEST_ASSERT_EQUAL_size_t(5, str.len);
    str_free(NULL, &str);
}

void test_str_from_cstr_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    TEST_ASSERT_EQUAL_STRING("hello", str.ptr);
    str_free(NULL, &str);
}

void test_str_from_cstr_null_terminated(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[str.len]);
    str_free(NULL, &str);
}

void test_str_from_cstr_empty_nonnull_ptr(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, ""));
    TEST_ASSERT_EQUAL_size_t(0, str.len);
    TEST_ASSERT_NOT_NULL(str.ptr);
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[0]);
    str_free(NULL, &str);
}

void test_str_from_strv_len(void)
{
    Strv strv = strv_from_cstr("world");
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_strv(NULL, &str, strv));
    TEST_ASSERT_EQUAL_size_t(5, str.len);
    str_free(NULL, &str);
}

void test_str_from_strv_content(void)
{
    Strv strv = strv_from_cstr("world");
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_strv(NULL, &str, strv));
    TEST_ASSERT_EQUAL_STRING("world", str.ptr);
    str_free(NULL, &str);
}

void test_str_from_strv_subview(void)
{
    /* Construct from a sub-view — no null terminator in the source */
    Strv strv = strv_from_cstr("foo:bar");
    Strv head = strv_split(&strv, ':');
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_strv(NULL, &str, head));
    TEST_ASSERT_EQUAL_size_t(3, str.len);
    TEST_ASSERT_EQUAL_STRING("foo", str.ptr);
    str_free(NULL, &str);
}

void test_str_to_strv_len_excludes_null(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    Strv strv = str_to_strv(str);
    /* len must be 5, not 6 — null terminator is not counted */
    TEST_ASSERT_EQUAL_size_t(5, strv.len);
    str_free(NULL, &str);
}

void test_str_to_strv_ptr_matches(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    Strv strv = str_to_strv(str);
    TEST_ASSERT_EQUAL_PTR(str.ptr, strv.ptr);
    str_free(NULL, &str);
}

void test_str_push_char_len(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "ab"));
    TEST_ASSERT_TRUE(str_push_char(NULL, &str, 'c'));
    TEST_ASSERT_EQUAL_size_t(3, str.len);
    str_free(NULL, &str);
}

void test_str_push_char_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "ab"));
    TEST_ASSERT_TRUE(str_push_char(NULL, &str, 'c'));
    TEST_ASSERT_EQUAL_STRING("abc", str.ptr);
    str_free(NULL, &str);
}

void test_str_push_char_null_terminated(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "ab"));
    TEST_ASSERT_TRUE(str_push_char(NULL, &str, 'c'));
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[str.len]);
    str_free(NULL, &str);
}

void test_str_append_cstr_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "foo"));
    TEST_ASSERT_TRUE(str_append_cstr(NULL, &str, "bar"));
    TEST_ASSERT_EQUAL_size_t(6, str.len);
    TEST_ASSERT_EQUAL_STRING("foobar", str.ptr);
    str_free(NULL, &str);
}

void test_str_append_strv_content(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    Strv suffix = strv_from_cstr(", world");
    TEST_ASSERT_TRUE(str_append_strv(NULL, &str, suffix));
    TEST_ASSERT_EQUAL_size_t(12, str.len);
    TEST_ASSERT_EQUAL_STRING("hello, world", str.ptr);
    str_free(NULL, &str);
}

void test_str_append_strv_subview(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "key="));
    Strv rest = strv_from_cstr("type:value");
    (void)strv_split(&rest, ':');
    TEST_ASSERT_TRUE(str_append_strv(NULL, &str, rest));
    TEST_ASSERT_EQUAL_STRING("key=value", str.ptr);
    str_free(NULL, &str);
}

void test_str_append_grows_beyond_initial_cap(void)
{
    /* Appending past STR_INITIAL_CAP (16) must succeed and keep content correct */
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, ""));
    for (int index = 0; index < 32; index++) {
        TEST_ASSERT_TRUE(str_push_char(NULL, &str, 'x'));
    }
    TEST_ASSERT_EQUAL_size_t(32, str.len);
    TEST_ASSERT_EQUAL_CHAR('x', str.ptr[0]);
    TEST_ASSERT_EQUAL_CHAR('x', str.ptr[31]);
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[32]);
    str_free(NULL, &str);
}

void test_str_clear_resets_len(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    str_clear(&str);
    TEST_ASSERT_EQUAL_size_t(0, str.len);
    str_free(NULL, &str);
}

void test_str_clear_keeps_allocation(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    size_t cap_before = str.cap;
    str_clear(&str);
    TEST_ASSERT_EQUAL_size_t(cap_before, str.cap);
    TEST_ASSERT_NOT_NULL(str.ptr);
    str_free(NULL, &str);
}

void test_str_clear_null_terminates(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    str_clear(&str);
    TEST_ASSERT_EQUAL_CHAR('\0', str.ptr[0]);
    str_free(NULL, &str);
}

void test_str_free_zeros_struct(void)
{
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_cstr(NULL, &str, "hello"));
    str_free(NULL, &str);
    TEST_ASSERT_NULL(str.ptr);
    TEST_ASSERT_EQUAL_size_t(0, str.len);
    TEST_ASSERT_EQUAL_size_t(0, str.cap);
}

void test_str_free_on_zero_is_safe(void)
{
    Str str = {0};
    str_free(NULL, &str); /* must not crash */
}
