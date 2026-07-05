#include "fff.h"
#include "unity.h"

#include "../src/effect.c" // NOLINT(bugprone-suspicious-include)
#include "../src/strv.c"   // NOLINT(bugprone-suspicious-include)

DEFINE_FFF_GLOBALS;

#include "test_heap_alloc.h"

void setUp(void) {}
void tearDown(void) {}

/* Test-only teardown: production never frees an EffectQueue explicitly
 * (progression_arena is torn down wholesale in game_free), but this heap-
 * backed test allocator needs its blocks released to stay LSan-clean. */
static void free_all(EffectQueue *queue)
{
    vec_sound_effect_request_free(&queue->sounds);
    vec_camera_pan_request_free(&queue->camera_pans);
    vec_camera_shake_request_free(&queue->camera_shakes);
    vec_spawn_request_free(&queue->spawns);
    vec_toast_request_free(&queue->toasts);
}

void test_effect_queue_init_creates_empty_queue(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_EQUAL_INT(0, queue.sounds.count);
    TEST_ASSERT_EQUAL_INT(0, queue.camera_pans.count);
    TEST_ASSERT_EQUAL_INT(0, queue.camera_shakes.count);
    TEST_ASSERT_EQUAL_INT(0, queue.spawns.count);
    TEST_ASSERT_EQUAL_INT(0, queue.toasts.count);
}

void test_effect_queue_push_sound_stores_name(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_TRUE(effect_queue_push_sound(&queue, strv_from_cstr("chest_open")));

    TEST_ASSERT_EQUAL_INT(1, queue.sounds.count);
    TEST_ASSERT_TRUE(strv_eq_cstr(queue.sounds.data[0].name, "chest_open"));

    free_all(&queue);
}

void test_effect_queue_push_camera_pan_stores_target_and_duration(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_TRUE(effect_queue_push_camera_pan(&queue, (Vector2){100.0F, 200.0F}, 1.5F));

    TEST_ASSERT_EQUAL_INT(1, queue.camera_pans.count);
    TEST_ASSERT_EQUAL_FLOAT(100.0F, queue.camera_pans.data[0].target.x);
    TEST_ASSERT_EQUAL_FLOAT(200.0F, queue.camera_pans.data[0].target.y);
    TEST_ASSERT_EQUAL_FLOAT(1.5F, queue.camera_pans.data[0].duration);

    free_all(&queue);
}

void test_effect_queue_push_camera_shake_stores_magnitude_and_duration(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_TRUE(effect_queue_push_camera_shake(&queue, (CameraShakeRequest){.magnitude = 5.5F, .duration = 0.3F}));

    TEST_ASSERT_EQUAL_INT(1, queue.camera_shakes.count);
    TEST_ASSERT_EQUAL_FLOAT(5.5F, queue.camera_shakes.data[0].magnitude);
    TEST_ASSERT_EQUAL_FLOAT(0.3F, queue.camera_shakes.data[0].duration);

    free_all(&queue);
}

void test_effect_queue_push_spawn_stores_blueprint_and_position(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_TRUE(effect_queue_push_spawn(&queue, strv_from_cstr("goblin"), (Vector2){12.0F, 34.0F}));

    TEST_ASSERT_EQUAL_INT(1, queue.spawns.count);
    TEST_ASSERT_TRUE(strv_eq_cstr(queue.spawns.data[0].blueprint, "goblin"));
    TEST_ASSERT_EQUAL_FLOAT(12.0F, queue.spawns.data[0].x);
    TEST_ASSERT_EQUAL_FLOAT(34.0F, queue.spawns.data[0].y);

    free_all(&queue);
}

void test_effect_queue_push_toast_stores_text(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_TRUE(effect_queue_push_toast(&queue, strv_from_cstr("gem")));

    TEST_ASSERT_EQUAL_INT(1, queue.toasts.count);
    TEST_ASSERT_TRUE(strv_eq_cstr(queue.toasts.data[0].text, "gem"));

    free_all(&queue);
}

void test_effect_queue_clear_resets_counts_and_allows_reuse(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_TRUE(effect_queue_push_sound(&queue, strv_from_cstr("a")));
    TEST_ASSERT_TRUE(effect_queue_push_camera_pan(&queue, (Vector2){0.0F, 0.0F}, 1.0F));
    TEST_ASSERT_TRUE(effect_queue_push_camera_shake(&queue, (CameraShakeRequest){.magnitude = 1.0F, .duration = 1.0F}));
    TEST_ASSERT_TRUE(effect_queue_push_spawn(&queue, strv_from_cstr("b"), (Vector2){0.0F, 0.0F}));
    TEST_ASSERT_TRUE(effect_queue_push_toast(&queue, strv_from_cstr("c")));

    int capacity_before = queue.sounds.capacity;
    effect_queue_clear(&queue);

    TEST_ASSERT_EQUAL_INT(0, queue.sounds.count);
    TEST_ASSERT_EQUAL_INT(0, queue.camera_pans.count);
    TEST_ASSERT_EQUAL_INT(0, queue.camera_shakes.count);
    TEST_ASSERT_EQUAL_INT(0, queue.spawns.count);
    TEST_ASSERT_EQUAL_INT(0, queue.toasts.count);
    TEST_ASSERT_EQUAL_INT(capacity_before, queue.sounds.capacity);

    /* Reusable: pushing again after clear grows count back to 1 without
     * re-initializing the queue. */
    TEST_ASSERT_TRUE(effect_queue_push_sound(&queue, strv_from_cstr("d")));
    TEST_ASSERT_EQUAL_INT(1, queue.sounds.count);

    free_all(&queue);
}

/* effect_queue_drain used to own this assertion (push all four, drain,
 * assert all four empty) before S6.4 moved the per-frame apply/handler
 * pass out of effect.c and into frame.c (apply_effect_queue), leaving
 * effect.c a pure push/clear channel with no Diag/GameState dependency.
 * Retargeted to effect_queue_clear, the pure function that still lives
 * here and still backs frame.c's apply pass. Toast (S6.8b) made it five. */
void test_effect_queue_clear_empties_all_five_after_full_push(void)
{
    EffectQueue queue;
    effect_queue_init(&queue, test_heap_alloc);

    TEST_ASSERT_TRUE(effect_queue_push_sound(&queue, strv_from_cstr("a")));
    TEST_ASSERT_TRUE(effect_queue_push_camera_pan(&queue, (Vector2){0.0F, 0.0F}, 1.0F));
    TEST_ASSERT_TRUE(effect_queue_push_camera_shake(&queue, (CameraShakeRequest){.magnitude = 1.0F, .duration = 1.0F}));
    TEST_ASSERT_TRUE(effect_queue_push_spawn(&queue, strv_from_cstr("b"), (Vector2){0.0F, 0.0F}));
    TEST_ASSERT_TRUE(effect_queue_push_toast(&queue, strv_from_cstr("c")));

    effect_queue_clear(&queue);

    TEST_ASSERT_EQUAL_INT(0, queue.sounds.count);
    TEST_ASSERT_EQUAL_INT(0, queue.camera_pans.count);
    TEST_ASSERT_EQUAL_INT(0, queue.camera_shakes.count);
    TEST_ASSERT_EQUAL_INT(0, queue.spawns.count);
    TEST_ASSERT_EQUAL_INT(0, queue.toasts.count);

    free_all(&queue);
}

int main(void)
{
    test_helpers_init();
    UNITY_BEGIN();
    RUN_TEST(test_effect_queue_init_creates_empty_queue);
    RUN_TEST(test_effect_queue_push_sound_stores_name);
    RUN_TEST(test_effect_queue_push_camera_pan_stores_target_and_duration);
    RUN_TEST(test_effect_queue_push_camera_shake_stores_magnitude_and_duration);
    RUN_TEST(test_effect_queue_push_spawn_stores_blueprint_and_position);
    RUN_TEST(test_effect_queue_push_toast_stores_text);
    RUN_TEST(test_effect_queue_clear_resets_counts_and_allows_reuse);
    RUN_TEST(test_effect_queue_clear_empties_all_five_after_full_push);
    return UNITY_END();
}
