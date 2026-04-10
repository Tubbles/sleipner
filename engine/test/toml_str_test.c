#include "fff.h"
#include "unity.h"

#include "../src/strv.c"     // NOLINT(bugprone-suspicious-include)
#include "../src/str.c"      // NOLINT(bugprone-suspicious-include)
#include "../src/toml_str.c" // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Build a toml_datum_t with .ok=1 and .u.s = strdup(value).
 * This mirrors what toml_string_in() returns — a malloc'd copy. */
static toml_datum_t make_string_datum(const char *value)
{
    toml_datum_t datum = {0};
    datum.ok = 1;
    datum.u.s = strdup(value);
    return datum;
}

void test_toml_str_content_is_copied(void)
{
    toml_datum_t datum = make_string_datum("hello");
    Str str = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_toml_datum(&str, &datum));
    TEST_ASSERT_EQUAL_STRING("hello", str.ptr);
    TEST_ASSERT_EQUAL_size_t(5, str.len);
    str_free(&str);
}

void test_toml_str_datum_nulled_after_call(void)
{
    toml_datum_t datum = make_string_datum("world");
    Str str = str_new(test_heap_alloc);
    TEST_ASSERT_TRUE(str_from_toml_datum(&str, &datum));
    TEST_ASSERT_NULL(datum.u.s);
    str_free(&str);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_toml_str_content_is_copied);
    RUN_TEST(test_toml_str_datum_nulled_after_call);
    return UNITY_END();
}
