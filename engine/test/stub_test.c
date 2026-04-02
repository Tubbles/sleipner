#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

void test_stub_passes(void)
{
    TEST_ASSERT_TRUE(1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_stub_passes);
    return UNITY_END();
}
