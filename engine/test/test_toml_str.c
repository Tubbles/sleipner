#include "unity.h"

#include "engine_context.h"
#include "str.h"
#include "toml.h"
#include "toml_str.h"

static struct EngineContext ctx;

/* Parse a tiny TOML table and return the string datum for key "val".
 * Caller must toml_free(table) after the test. */
static toml_table_t *make_table_with_string(const char *value, toml_datum_t *out_datum)
{
    char src[128];
    snprintf(src, sizeof(src), "val = \"%s\"\n", value);
    char errbuf[64] = {0};
    toml_table_t *table = toml_parse(src, errbuf, sizeof(errbuf));
    if (!table) {
        return nullptr;
    }
    *out_datum = toml_string_in(table, "val");
    return table;
}

void test_toml_str_content_is_copied(void)
{
    toml_datum_t datum = {0};
    toml_table_t *table = make_table_with_string("hello", &datum);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE((bool)datum.ok);
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_toml_datum(&ctx, &str, &datum));
    TEST_ASSERT_EQUAL_STRING("hello", str.ptr);
    TEST_ASSERT_EQUAL_size_t(5, str.len);
    str_free(&ctx, &str);
    toml_free(table);
}

void test_toml_str_datum_nulled_after_call(void)
{
    toml_datum_t datum = {0};
    toml_table_t *table = make_table_with_string("world", &datum);
    TEST_ASSERT_NOT_NULL(table);
    TEST_ASSERT_TRUE((bool)datum.ok);
    Str str = {0};
    TEST_ASSERT_TRUE(str_from_toml_datum(&ctx, &str, &datum));
    TEST_ASSERT_NULL(datum.u.s);
    str_free(&ctx, &str);
    toml_free(table);
}
