#include "unity.h"

#include "attribute.h"
#include "test_helpers.h"

void test_attr_set_and_get_float(void)
{
    AttrSet set = {0};
    TEST_ASSERT_TRUE(attr_set_float(NULL, &set, "speed", 80.0F));
    TEST_ASSERT_EQUAL_INT(1, set.entries.count);

    const Attribute *entry = attr_get(&set, "speed");
    TEST_ASSERT_NOT_NULL(entry);
    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, entry->type);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 80.0F, entry->value.f);
    test_attr_set_free(&set);
}

void test_attr_set_and_get_int(void)
{
    AttrSet set = {0};
    TEST_ASSERT_TRUE(attr_set_int(NULL, &set, "health", 10));

    TEST_ASSERT_EQUAL_INT(10, attr_get_int(&set, "health", 0));
    test_attr_set_free(&set);
}

void test_attr_set_and_get_bool(void)
{
    AttrSet set = {0};
    TEST_ASSERT_TRUE(attr_set_bool(NULL, &set, "is_locked", true));

    TEST_ASSERT_TRUE(attr_get_bool(&set, "is_locked", false));
    test_attr_set_free(&set);
}

void test_attr_set_and_get_string(void)
{
    AttrSet set = {0};
    TEST_ASSERT_TRUE(attr_set_string(NULL, &set, (AttrStringPair){.name = "loot_table", .value = "common"}));

    TEST_ASSERT_EQUAL_STRING("common", attr_get_string(&set, "loot_table"));
    test_attr_set_free(&set);
}

void test_attr_overwrite_existing(void)
{
    AttrSet set = {0};
    TEST_ASSERT_TRUE(attr_set_int(NULL, &set, "health", 10));
    TEST_ASSERT_TRUE(attr_set_int(NULL, &set, "health", 5));

    TEST_ASSERT_EQUAL_INT(1, set.entries.count);
    TEST_ASSERT_EQUAL_INT(5, attr_get_int(&set, "health", 0));
    test_attr_set_free(&set);
}

void test_attr_get_missing_returns_fallback(void)
{
    AttrSet set = {0};

    TEST_ASSERT_FLOAT_WITHIN(0.01F, 99.0F, attr_get_float(&set, "missing", 99.0F));
    TEST_ASSERT_EQUAL_INT(42, attr_get_int(&set, "missing", 42));
    TEST_ASSERT_FALSE(attr_get_bool(&set, "missing", false));
    TEST_ASSERT_NULL(attr_get_string(&set, "missing"));
    test_attr_set_free(&set);
}

void test_attr_push_many_entries(void)
{
    AttrSet set = {0};
    char name[64];

    for (int index = 0; index < 64; index++) {
        snprintf(name, sizeof(name), "attr_%d", index);
        TEST_ASSERT_TRUE(attr_set_int(NULL, &set, name, index));
    }

    TEST_ASSERT_EQUAL_INT(64, set.entries.count);
    for (int index = 0; index < 64; index++) {
        snprintf(name, sizeof(name), "attr_%d", index);
        TEST_ASSERT_EQUAL_INT(index, attr_get_int(&set, name, -1));
    }
    test_attr_set_free(&set);
}

void test_attr_type_change(void)
{
    AttrSet set = {0};
    TEST_ASSERT_TRUE(attr_set_int(NULL, &set, "value", 42));
    TEST_ASSERT_TRUE(attr_set_float(NULL, &set, "value", 3.14F));

    TEST_ASSERT_EQUAL_INT(1, set.entries.count);
    const Attribute *entry = attr_get(&set, "value");
    TEST_ASSERT_EQUAL_INT(ATTR_FLOAT, entry->type);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 3.14F, entry->value.f);
    test_attr_set_free(&set);
}

void test_attr_scoped_instance_overrides_blueprint(void)
{
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_bool(NULL, &blueprint, "is_locked", true));
    TEST_ASSERT_TRUE(attr_set_string(NULL, &blueprint, (AttrStringPair){.name = "loot_table", .value = "common"}));

    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_bool(NULL, &instance, "is_locked", false));

    /* Instance overrides blueprint */
    const Attribute *locked = attr_get_scoped(&instance, &blueprint, "is_locked");
    TEST_ASSERT_NOT_NULL(locked);
    TEST_ASSERT_FALSE(locked->value.b);

    /* Falls back to blueprint */
    const Attribute *loot = attr_get_scoped(&instance, &blueprint, "loot_table");
    TEST_ASSERT_NOT_NULL(loot);
    TEST_ASSERT_EQUAL_STRING("common", loot->value.str.ptr);

    /* Missing in both */
    const Attribute *missing = attr_get_scoped(&instance, &blueprint, "nonexistent");
    TEST_ASSERT_NULL(missing);
    test_attr_set_free(&blueprint);
    test_attr_set_free(&instance);
}

void test_attr_multiple_types(void)
{
    AttrSet set = {0};
    TEST_ASSERT_TRUE(attr_set_float(NULL, &set, "speed", 80.0F));
    TEST_ASSERT_TRUE(attr_set_int(NULL, &set, "health", 10));
    TEST_ASSERT_TRUE(attr_set_bool(NULL, &set, "visible", true));
    TEST_ASSERT_TRUE(attr_set_string(NULL, &set, (AttrStringPair){.name = "name", .value = "chest"}));

    TEST_ASSERT_EQUAL_INT(4, set.entries.count);
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 80.0F, attr_get_float(&set, "speed", 0));
    TEST_ASSERT_EQUAL_INT(10, attr_get_int(&set, "health", 0));
    TEST_ASSERT_TRUE(attr_get_bool(&set, "visible", false));
    TEST_ASSERT_EQUAL_STRING("chest", attr_get_string(&set, "name"));
    test_attr_set_free(&set);
}

void test_attr_get_scoped_float(void)
{
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_float(NULL, &blueprint, "speed", 80.0F));
    TEST_ASSERT_TRUE(attr_set_int(NULL, &blueprint, "damage", 5));

    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_float(NULL, &instance, "speed", 120.0F));

    /* Instance overrides blueprint */
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 120.0F, attr_get_scoped_float(&instance, &blueprint, "speed", 0.0F));

    /* Falls back to blueprint */
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 80.0F, attr_get_scoped_float(&instance, &blueprint, "hp", 80.0F));

    /* Int-to-float coercion from blueprint */
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 5.0F, attr_get_scoped_float(&instance, &blueprint, "damage", 0.0F));

    /* Missing in both returns fallback */
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 99.0F, attr_get_scoped_float(&instance, &blueprint, "missing", 99.0F));

    test_attr_set_free(&blueprint);
    test_attr_set_free(&instance);
}

void test_attr_get_scoped_int(void)
{
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_int(NULL, &blueprint, "health", 100));
    TEST_ASSERT_TRUE(attr_set_float(NULL, &blueprint, "level", 3.0F));

    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_int(NULL, &instance, "health", 50));

    /* Instance overrides */
    TEST_ASSERT_EQUAL_INT(50, attr_get_scoped_int(&instance, &blueprint, "health", 0));

    /* Falls back to blueprint */
    TEST_ASSERT_EQUAL_INT(100, attr_get_scoped_int(&instance, &blueprint, "mana", 100));

    /* Float-to-int coercion from blueprint */
    TEST_ASSERT_EQUAL_INT(3, attr_get_scoped_int(&instance, &blueprint, "level", 0));

    test_attr_set_free(&blueprint);
    test_attr_set_free(&instance);
}

void test_attr_get_scoped_bool(void)
{
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_bool(NULL, &blueprint, "visible", false));
    TEST_ASSERT_TRUE(attr_set_bool(NULL, &blueprint, "solid", true));

    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_bool(NULL, &instance, "visible", true));

    /* Instance overrides */
    TEST_ASSERT_TRUE(attr_get_scoped_bool(&instance, &blueprint, "visible", false));

    /* Falls back to blueprint */
    TEST_ASSERT_TRUE(attr_get_scoped_bool(&instance, &blueprint, "solid", false));

    /* Missing returns fallback */
    TEST_ASSERT_FALSE(attr_get_scoped_bool(&instance, &blueprint, "active", false));

    test_attr_set_free(&blueprint);
    test_attr_set_free(&instance);
}

void test_attr_get_scoped_string(void)
{
    AttrSet blueprint = {0};
    TEST_ASSERT_TRUE(attr_set_string(NULL, &blueprint, (AttrStringPair){.name = "behavior", .value = "npc"}));
    TEST_ASSERT_TRUE(attr_set_string(NULL, &blueprint, (AttrStringPair){.name = "loot", .value = "common"}));

    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_string(NULL, &instance, (AttrStringPair){.name = "behavior", .value = "player"}));

    /* Instance overrides */
    TEST_ASSERT_EQUAL_STRING("player", attr_get_scoped_string(&instance, &blueprint, "behavior"));

    /* Falls back to blueprint */
    TEST_ASSERT_EQUAL_STRING("common", attr_get_scoped_string(&instance, &blueprint, "loot"));

    /* Missing returns NULL */
    TEST_ASSERT_NULL(attr_get_scoped_string(&instance, &blueprint, "missing"));

    test_attr_set_free(&blueprint);
    test_attr_set_free(&instance);
}

void test_attr_get_scoped_null_blueprint(void)
{
    AttrSet instance = {0};
    TEST_ASSERT_TRUE(attr_set_float(NULL, &instance, "speed", 50.0F));

    /* NULL blueprint — instance-only lookup */
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 50.0F, attr_get_scoped_float(&instance, NULL, "speed", 0.0F));
    TEST_ASSERT_FLOAT_WITHIN(0.01F, 99.0F, attr_get_scoped_float(&instance, NULL, "missing", 99.0F));
    TEST_ASSERT_NULL(attr_get_scoped_string(&instance, NULL, "missing"));

    /* Raw attr_get_scoped with NULL blueprint */
    const Attribute *found = attr_get_scoped(&instance, NULL, "speed");
    TEST_ASSERT_NOT_NULL(found);
    const Attribute *not_found = attr_get_scoped(&instance, NULL, "missing");
    TEST_ASSERT_NULL(not_found);

    test_attr_set_free(&instance);
}
