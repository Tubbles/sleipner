#include "unity.h"

#include "strv.h"

#include <stddef.h>

void test_strv_from_cstr(void)
{
    Strv strv = strv_from_cstr("hello");
    TEST_ASSERT_EQUAL_size_t(5, strv.len);
    TEST_ASSERT_EQUAL_PTR("hello", strv.ptr);
}

void test_strv_shrink_left(void)
{
    Strv strv = strv_from_cstr("hello");
    strv_shrink_left(&strv, 2);
    TEST_ASSERT_EQUAL_size_t(3, strv.len);
    TEST_ASSERT_EQUAL_CHAR('l', strv.ptr[0]);
}

void test_strv_shrink_left_clamp(void)
{
    Strv strv = strv_from_cstr("hi");
    strv_shrink_left(&strv, 100);
    TEST_ASSERT_EQUAL_size_t(0, strv.len);
}

void test_strv_shrink_right(void)
{
    Strv strv = strv_from_cstr("hello");
    strv_shrink_right(&strv, 2);
    TEST_ASSERT_EQUAL_size_t(3, strv.len);
    TEST_ASSERT_EQUAL_CHAR('h', strv.ptr[0]);
}

void test_strv_shrink_right_clamp(void)
{
    Strv strv = strv_from_cstr("hi");
    strv_shrink_right(&strv, 100);
    TEST_ASSERT_EQUAL_size_t(0, strv.len);
}

void test_strv_trim_left(void)
{
    Strv strv = strv_from_cstr("  hello");
    strv_trim_left(&strv);
    TEST_ASSERT_EQUAL_size_t(5, strv.len);
    TEST_ASSERT_EQUAL_CHAR('h', strv.ptr[0]);
}

void test_strv_trim_right(void)
{
    Strv strv = strv_from_cstr("hello  ");
    strv_trim_right(&strv);
    TEST_ASSERT_EQUAL_size_t(5, strv.len);
    TEST_ASSERT_EQUAL_CHAR('o', strv.ptr[strv.len - 1]);
}

void test_strv_trim(void)
{
    Strv strv = strv_from_cstr("  hello  ");
    strv_trim(&strv);
    TEST_ASSERT_EQUAL_size_t(5, strv.len);
    TEST_ASSERT_EQUAL_CHAR('h', strv.ptr[0]);
    TEST_ASSERT_EQUAL_CHAR('o', strv.ptr[strv.len - 1]);
}

void test_strv_trim_all_whitespace(void)
{
    Strv strv = strv_from_cstr("   ");
    strv_trim(&strv);
    TEST_ASSERT_EQUAL_size_t(0, strv.len);
}

void test_strv_split_found(void)
{
    Strv strv = strv_from_cstr("foo:bar");
    Strv head = strv_split(&strv, ':');
    TEST_ASSERT_EQUAL_size_t(3, head.len);
    TEST_ASSERT_EQUAL_CHAR('f', head.ptr[0]);
    TEST_ASSERT_EQUAL_size_t(3, strv.len);
    TEST_ASSERT_EQUAL_CHAR('b', strv.ptr[0]);
}

void test_strv_split_not_found(void)
{
    Strv strv = strv_from_cstr("foobar");
    Strv head = strv_split(&strv, ':');
    TEST_ASSERT_EQUAL_size_t(6, head.len);
    TEST_ASSERT_NULL(strv.ptr);
    TEST_ASSERT_EQUAL_size_t(0, strv.len);
}

void test_strv_split_delim_first(void)
{
    Strv strv = strv_from_cstr(":bar");
    Strv head = strv_split(&strv, ':');
    TEST_ASSERT_EQUAL_size_t(0, head.len);
    TEST_ASSERT_EQUAL_size_t(3, strv.len);
    TEST_ASSERT_EQUAL_CHAR('b', strv.ptr[0]);
}

void test_strv_split_delim_last(void)
{
    Strv strv = strv_from_cstr("foo:");
    Strv head = strv_split(&strv, ':');
    TEST_ASSERT_EQUAL_size_t(3, head.len);
    TEST_ASSERT_EQUAL_size_t(0, strv.len);
}

void test_strv_split_multi(void)
{
    Strv strv = strv_from_cstr("a:b:c");

    Strv first = strv_split(&strv, ':');
    TEST_ASSERT_EQUAL_size_t(1, first.len);
    TEST_ASSERT_EQUAL_CHAR('a', first.ptr[0]);

    Strv second = strv_split(&strv, ':');
    TEST_ASSERT_EQUAL_size_t(1, second.len);
    TEST_ASSERT_EQUAL_CHAR('b', second.ptr[0]);

    Strv third = strv_split(&strv, ':');
    TEST_ASSERT_EQUAL_size_t(1, third.len);
    TEST_ASSERT_EQUAL_CHAR('c', third.ptr[0]);
    TEST_ASSERT_NULL(strv.ptr);
}
