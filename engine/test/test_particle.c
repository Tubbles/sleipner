#include "unity.h"
#include "particle.h"
#include <stdlib.h>

static ParticlePool pool;

void test_particle_init(void)
{
    particles_init(&pool);
    TEST_ASSERT_EQUAL_INT(0, pool.count);
    TEST_ASSERT_EQUAL_INT(PARTICLE_INITIAL_CAPACITY, pool.capacity);
    TEST_ASSERT_NOT_NULL(pool.items);
    particles_free(&pool);
}

void test_particle_spawn_increases_count(void)
{
    particles_init(&pool);
    particles_spawn(&pool, (Vector2){100, 100}, RED, 10);
    TEST_ASSERT_EQUAL_INT(10, pool.count);
    particles_free(&pool);
}

void test_particle_lifetime_expiry(void)
{
    particles_init(&pool);
    particles_spawn(&pool, (Vector2){100, 100}, RED, 5);

    /* Update with a very large dt to expire all particles */
    particles_update(&pool, 100.0f);
    TEST_ASSERT_EQUAL_INT(0, pool.count);
    particles_free(&pool);
}

void test_particle_position_updates(void)
{
    particles_init(&pool);
    srand(42);
    particles_spawn(&pool, (Vector2){0, 0}, RED, 1);

    Vector2 initial_pos = pool.items[0].position;
    particles_update(&pool, 0.1f);

    /* Position should have changed (particle has velocity) */
    bool moved = (pool.items[0].position.x != initial_pos.x) || (pool.items[0].position.y != initial_pos.y);
    TEST_ASSERT_TRUE(moved);
    particles_free(&pool);
}

void test_particle_capacity_grows(void)
{
    particles_init(&pool);
    int big_count = PARTICLE_INITIAL_CAPACITY + 100;
    particles_spawn(&pool, (Vector2){0, 0}, RED, big_count);
    TEST_ASSERT_EQUAL_INT(big_count, pool.count);
    TEST_ASSERT_TRUE(pool.capacity >= big_count);
    particles_free(&pool);
}

void test_particle_free_cleans_up(void)
{
    particles_init(&pool);
    particles_spawn(&pool, (Vector2){0, 0}, RED, 10);
    particles_free(&pool);
    TEST_ASSERT_NULL(pool.items);
    TEST_ASSERT_EQUAL_INT(0, pool.count);
    TEST_ASSERT_EQUAL_INT(0, pool.capacity);
}
