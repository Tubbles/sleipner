#include "unity.h"
#include "assets.h"

/* D18: ASSET() must resolve from engine-lib code and, critically, from
 * this test binary — before the sleipner_assets OBJECT library existed,
 * embed_all_assets only attached the .incbin objects to the sleipner
 * executable, so any engine-lib .c file (e.g. blur.c) reaching for
 * ASSET() would fail to LINK inside engine_tests. This test compiling
 * and passing is the proof; it would have failed to link, not just
 * failed to assert, before the OBJECT library existed. */
void test_asset_blur_fs_is_embedded_glsl(void)
{
    EmbeddedAsset shader = ASSET(blur_fs);
    TEST_ASSERT_GREATER_THAN(0, shader.size);
    TEST_ASSERT_EQUAL_STRING_LEN("#version", (const char *)shader.data, 8);
}
