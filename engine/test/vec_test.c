#include "vec.h"

#include "unity.h"
#include "test_helpers.h"

/* A small local struct to verify VEC_DECL/VEC_IMPL work for non-primitive types. */
typedef struct {
    int x_pos;
    int y_pos;
} Point;

VEC_DECL(point, Point)
VEC_IMPL(point, Point)

/* ---- Initial state ---- */

void test_vec_initial_state_count_is_zero(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_EQUAL_INT(0, vector.count);
}

void test_vec_initial_state_capacity_is_zero(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_EQUAL_INT(0, vector.capacity);
}

void test_vec_initial_state_data_is_null(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_NULL(vector.data);
}

/* ---- Push: count and value correctness ---- */

void test_vec_push_increments_count(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_EQUAL_INT(1, vector.count);
    vec_int_free(&vector);
}

void test_vec_push_single_value_readable(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 42));
    TEST_ASSERT_EQUAL_INT(42, vector.data[0]);
    vec_int_free(&vector);
}

void test_vec_push_multiple_values_in_order(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 10));
    TEST_ASSERT_TRUE(vec_int_push(&vector, 20));
    TEST_ASSERT_TRUE(vec_int_push(&vector, 30));
    TEST_ASSERT_EQUAL_INT(3, vector.count);
    TEST_ASSERT_EQUAL_INT(10, vector.data[0]);
    TEST_ASSERT_EQUAL_INT(20, vector.data[1]);
    TEST_ASSERT_EQUAL_INT(30, vector.data[2]);
    vec_int_free(&vector);
}

void test_vec_push_returns_true_on_success(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    bool result = vec_int_push(&vector, 7);
    TEST_ASSERT_TRUE(result);
    vec_int_free(&vector);
}

/* ---- Allocation on first push ---- */

void test_vec_first_push_allocates_data(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_NULL(vector.data);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_NOT_NULL(vector.data);
    vec_int_free(&vector);
}

void test_vec_first_push_sets_initial_capacity(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY, vector.capacity);
    vec_int_free(&vector);
}

/* ---- Growth / realloc ---- */

void test_vec_push_up_to_capacity_no_extra_alloc(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    for (int index = 0; index < VEC_INITIAL_CAPACITY; index++) {
        TEST_ASSERT_TRUE(vec_int_push(&vector, index));
    }
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY, vector.count);
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY, vector.capacity);
    vec_int_free(&vector);
}

void test_vec_push_one_past_capacity_triggers_growth(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    for (int index = 0; index <= VEC_INITIAL_CAPACITY; index++) {
        TEST_ASSERT_TRUE(vec_int_push(&vector, index));
    }
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY + 1, vector.count);
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY * VEC_GROWTH_FACTOR, vector.capacity);
    vec_int_free(&vector);
}

void test_vec_growth_preserves_existing_values(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    int push_count = VEC_INITIAL_CAPACITY + 4;
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_TRUE(vec_int_push(&vector, index * 10));
    }
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_EQUAL_INT(index * 10, vector.data[index]);
    }
    vec_int_free(&vector);
}

void test_vec_multiple_growths_all_values_intact(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    int push_count = (VEC_INITIAL_CAPACITY * VEC_GROWTH_FACTOR * VEC_GROWTH_FACTOR) + 1;
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_TRUE(vec_int_push(&vector, index));
    }
    TEST_ASSERT_EQUAL_INT(push_count, vector.count);
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_EQUAL_INT(index, vector.data[index]);
    }
    vec_int_free(&vector);
}

/* ---- clear ---- */

void test_vec_clear_resets_count_to_zero(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_TRUE(vec_int_push(&vector, 2));
    vec_int_clear(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.count);
    vec_int_free(&vector);
}

void test_vec_clear_preserves_allocation(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    int capacity_before = vector.capacity;
    vec_int_clear(&vector);
    TEST_ASSERT_NOT_NULL(vector.data);
    TEST_ASSERT_EQUAL_INT(capacity_before, vector.capacity);
    vec_int_free(&vector);
}

void test_vec_clear_allows_repush(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 99));
    vec_int_clear(&vector);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 77));
    TEST_ASSERT_EQUAL_INT(1, vector.count);
    TEST_ASSERT_EQUAL_INT(77, vector.data[0]);
    vec_int_free(&vector);
}

void test_vec_clear_on_empty_is_safe(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    vec_int_clear(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.count);
    TEST_ASSERT_NULL(vector.data);
}

/* ---- free ---- */

void test_vec_free_resets_count_to_zero(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.count);
}

void test_vec_free_resets_capacity_to_zero(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.capacity);
}

void test_vec_free_resets_data_to_null(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_NULL(vector.data);
}

void test_vec_free_on_empty_is_safe(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    vec_int_free(&vector);
    TEST_ASSERT_NULL(vector.data);
}

void test_vec_free_allows_repush(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 55));
    TEST_ASSERT_EQUAL_INT(1, vector.count);
    TEST_ASSERT_EQUAL_INT(55, vector.data[0]);
    vec_int_free(&vector);
}

/* ---- Data contiguity ---- */

void test_vec_data_is_contiguous_in_memory(void)
{
    vec_int vector = vec_int_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 100));
    TEST_ASSERT_TRUE(vec_int_push(&vector, 200));
    TEST_ASSERT_TRUE(vec_int_push(&vector, 300));
    /* Pointer arithmetic on .data must match indexed access */
    TEST_ASSERT_EQUAL_INT(100, *(vector.data + 0));
    TEST_ASSERT_EQUAL_INT(200, *(vector.data + 1));
    TEST_ASSERT_EQUAL_INT(300, *(vector.data + 2));
    vec_int_free(&vector);
}

/* ---- Type coverage ---- */

void test_vec_float_push_and_read(void)
{
    vec_float vector = vec_float_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_float_push(&vector, 1.5F));
    TEST_ASSERT_TRUE(vec_float_push(&vector, 2.5F));
    TEST_ASSERT_EQUAL_FLOAT(1.5F, vector.data[0]);
    TEST_ASSERT_EQUAL_FLOAT(2.5F, vector.data[1]);
    vec_float_free(&vector);
}

void test_vec_bool_push_and_read(void)
{
    vec_bool vector = vec_bool_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_bool_push(&vector, true));
    TEST_ASSERT_TRUE(vec_bool_push(&vector, false));
    TEST_ASSERT_TRUE(vector.data[0]);
    TEST_ASSERT_FALSE(vector.data[1]);
    vec_bool_free(&vector);
}

void test_vec_i64_push_and_read(void)
{
    vec_i64 vector = vec_i64_new(test_heap_alloc);
    int64_t large_value = 9000000000LL;
    TEST_ASSERT_TRUE(vec_i64_push(&vector, large_value));
    TEST_ASSERT_EQUAL_INT64(large_value, vector.data[0]);
    vec_i64_free(&vector);
}

void test_vec_u32_push_and_read(void)
{
    vec_u32 vector = vec_u32_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_u32_push(&vector, 0xDEADBEEFU));
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFU, vector.data[0]);
    vec_u32_free(&vector);
}

/* ---- Custom struct type ---- */

void test_vec_struct_push_and_read(void)
{
    vec_point vector = vec_point_new(test_heap_alloc);
    TEST_ASSERT_TRUE(vec_point_push(&vector, (Point){.x_pos = 3, .y_pos = 7}));
    TEST_ASSERT_TRUE(vec_point_push(&vector, (Point){.x_pos = 9, .y_pos = 1}));
    TEST_ASSERT_EQUAL_INT(2, vector.count);
    TEST_ASSERT_EQUAL_INT(3, vector.data[0].x_pos);
    TEST_ASSERT_EQUAL_INT(7, vector.data[0].y_pos);
    TEST_ASSERT_EQUAL_INT(9, vector.data[1].x_pos);
    TEST_ASSERT_EQUAL_INT(1, vector.data[1].y_pos);
    vec_point_free(&vector);
}

void test_vec_struct_growth_preserves_fields(void)
{
    vec_point vector = vec_point_new(test_heap_alloc);
    int push_count = VEC_INITIAL_CAPACITY + 1;
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_TRUE(vec_point_push(&vector, (Point){.x_pos = index, .y_pos = index * 2}));
    }
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_EQUAL_INT(index, vector.data[index].x_pos);
        TEST_ASSERT_EQUAL_INT(index * 2, vector.data[index].y_pos);
    }
    vec_point_free(&vector);
}
