#include "fff.h"
#include "unity.h"

#include "../src/audio.c" // NOLINT(bugprone-suspicious-include)
#include "../src/strv.c"  // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

/* raylib audio calls audio.c references. Not exercised by these tests
 * (they only drive the pure sfx_alias_pool_next_slot cursor), but the
 * whole file is compiled in via the white-box include above, so every
 * symbol it references must resolve — same pattern as
 * editor_draw_test.c's raylib draw-call fakes. */
FAKE_VOID_FUNC(InitAudioDevice);
FAKE_VOID_FUNC(CloseAudioDevice);
FAKE_VALUE_FUNC(Sound, LoadSoundFromWave, Wave);
FAKE_VALUE_FUNC(Sound, LoadSoundAlias, Sound);
FAKE_VOID_FUNC(UnloadSound, Sound);
FAKE_VOID_FUNC(UnloadSoundAlias, Sound);
FAKE_VOID_FUNC(PlaySound, Sound);
FAKE_VOID_FUNC(StopSound, Sound);
FAKE_VOID_FUNC(SetSoundVolume, Sound, float);

void setUp(void) {}
void tearDown(void) {}

/* D32: raylib sound aliases, N=SFX_MAX_CONCURRENT_ALIASES=8, drop-oldest.
 * sfx_alias_pool_next_slot is the pure cursor-bump half of the
 * concurrency cap -- no raylib calls, so this is testable without an
 * audio device. The impure half (sfx_alias_pool_play: stop + unload the
 * evicted alias, load + play the new one) is production-only. */

void test_sfx_alias_pool_next_slot_fills_virgin_slots_in_order(void)
{
    SfxAliasPool pool = {0};

    for (int expected_slot = 0; expected_slot < SFX_MAX_CONCURRENT_ALIASES; expected_slot++) {
        TEST_ASSERT_EQUAL_INT(expected_slot, sfx_alias_pool_next_slot(&pool));
    }
}

void test_sfx_alias_pool_next_slot_wraps_and_drops_oldest(void)
{
    SfxAliasPool pool = {0};
    for (int play = 0; play < SFX_MAX_CONCURRENT_ALIASES; play++) {
        (void)sfx_alias_pool_next_slot(&pool);
    }

    /* Plays 9 and 10 must reclaim slots 0 and 1 -- the two
     * least-recently-claimed slots -- not any other index. */
    TEST_ASSERT_EQUAL_INT(0, sfx_alias_pool_next_slot(&pool));
    TEST_ASSERT_EQUAL_INT(1, sfx_alias_pool_next_slot(&pool));
    TEST_ASSERT_EQUAL_INT(2, pool.next_slot);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sfx_alias_pool_next_slot_fills_virgin_slots_in_order);
    RUN_TEST(test_sfx_alias_pool_next_slot_wraps_and_drops_oldest);
    return UNITY_END();
}
