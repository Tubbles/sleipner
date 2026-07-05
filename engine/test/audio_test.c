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

/* Per-level music crossfade (S6.13b, D32). music_crossfade_gain,
 * music_on_level_changed, and music_state_tick are pure string/float
 * state -- no raylib calls -- so they're testable the same way
 * sfx_alias_pool_next_slot is above. The impure stream-driving half
 * (main.c's music_streams_update) is production-only. */

void test_music_crossfade_gain_at_full_timer_favors_outgoing(void)
{
    MusicCrossfadeGain gain = music_crossfade_gain(MUSIC_CROSSFADE_SECONDS, MUSIC_CROSSFADE_SECONDS);
    TEST_ASSERT_EQUAL_FLOAT(1.0F, gain.outgoing_gain);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, gain.incoming_gain);
}

void test_music_crossfade_gain_at_half_timer_is_even(void)
{
    MusicCrossfadeGain gain = music_crossfade_gain(MUSIC_CROSSFADE_SECONDS / 2.0F, MUSIC_CROSSFADE_SECONDS);
    TEST_ASSERT_EQUAL_FLOAT(0.5F, gain.outgoing_gain);
    TEST_ASSERT_EQUAL_FLOAT(0.5F, gain.incoming_gain);
}

void test_music_crossfade_gain_at_or_before_zero_favors_incoming(void)
{
    MusicCrossfadeGain at_zero = music_crossfade_gain(0.0F, MUSIC_CROSSFADE_SECONDS);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, at_zero.outgoing_gain);
    TEST_ASSERT_EQUAL_FLOAT(1.0F, at_zero.incoming_gain);

    MusicCrossfadeGain past_zero = music_crossfade_gain(-0.25F, MUSIC_CROSSFADE_SECONDS);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, past_zero.outgoing_gain);
    TEST_ASSERT_EQUAL_FLOAT(1.0F, past_zero.incoming_gain);
}

void test_music_crossfade_gain_clamps_timer_beyond_total(void)
{
    MusicCrossfadeGain gain = music_crossfade_gain(MUSIC_CROSSFADE_SECONDS * 5.0F, MUSIC_CROSSFADE_SECONDS);
    TEST_ASSERT_EQUAL_FLOAT(1.0F, gain.outgoing_gain);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, gain.incoming_gain);
}

void test_music_on_level_changed_first_load_has_no_crossfade(void)
{
    MusicState music = {0};

    music_on_level_changed(&music, "a.mp3");

    TEST_ASSERT_EQUAL_STRING("a.mp3", music.current_track_name);
    TEST_ASSERT_EQUAL_STRING("", music.outgoing_track_name);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, music.crossfade_timer);
}

void test_music_on_level_changed_no_track_is_a_no_op(void)
{
    MusicState music = {0};

    music_on_level_changed(&music, nullptr);

    TEST_ASSERT_EQUAL_STRING("", music.current_track_name);
    TEST_ASSERT_EQUAL_FLOAT(0.0F, music.crossfade_timer);
}

void test_music_on_level_changed_different_track_starts_crossfade(void)
{
    MusicState music = {0};
    music_on_level_changed(&music, "a.mp3");

    music_on_level_changed(&music, "b.mp3");

    TEST_ASSERT_EQUAL_STRING("b.mp3", music.current_track_name);
    TEST_ASSERT_EQUAL_STRING("a.mp3", music.outgoing_track_name);
    TEST_ASSERT_EQUAL_FLOAT(MUSIC_CROSSFADE_SECONDS, music.crossfade_timer);
}

void test_music_on_level_changed_same_track_does_not_restart_crossfade(void)
{
    MusicState music = {0};
    music_on_level_changed(&music, "a.mp3");
    music_on_level_changed(&music, "b.mp3");
    music.crossfade_timer = 0.2F; /* partway through the a -> b crossfade */

    music_on_level_changed(&music, "b.mp3"); /* same as current: no restart */

    TEST_ASSERT_EQUAL_STRING("b.mp3", music.current_track_name);
    TEST_ASSERT_EQUAL_STRING("a.mp3", music.outgoing_track_name);
    TEST_ASSERT_EQUAL_FLOAT(0.2F, music.crossfade_timer);
}

void test_music_state_tick_ends_crossfade_past_total(void)
{
    MusicState music = {0};
    music_on_level_changed(&music, "a.mp3");
    music_on_level_changed(&music, "b.mp3");

    music_state_tick(&music, MUSIC_CROSSFADE_SECONDS + 0.5F);

    TEST_ASSERT_EQUAL_FLOAT(0.0F, music.crossfade_timer);
    TEST_ASSERT_EQUAL_STRING("", music.outgoing_track_name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sfx_alias_pool_next_slot_fills_virgin_slots_in_order);
    RUN_TEST(test_sfx_alias_pool_next_slot_wraps_and_drops_oldest);
    RUN_TEST(test_music_crossfade_gain_at_full_timer_favors_outgoing);
    RUN_TEST(test_music_crossfade_gain_at_half_timer_is_even);
    RUN_TEST(test_music_crossfade_gain_at_or_before_zero_favors_incoming);
    RUN_TEST(test_music_crossfade_gain_clamps_timer_beyond_total);
    RUN_TEST(test_music_on_level_changed_first_load_has_no_crossfade);
    RUN_TEST(test_music_on_level_changed_no_track_is_a_no_op);
    RUN_TEST(test_music_on_level_changed_different_track_starts_crossfade);
    RUN_TEST(test_music_on_level_changed_same_track_does_not_restart_crossfade);
    RUN_TEST(test_music_state_tick_ends_crossfade_past_total);
    return UNITY_END();
}
