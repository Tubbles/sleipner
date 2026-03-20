#include "vec.h"

#include "unity.h"

/* A small local struct to verify VEC_DECL/VEC_IMPL work for non-primitive types. */
typedef struct {
    int x_pos;
    int y_pos;
} Point;

VEC_DECL(point, Point)
VEC_IMPL(point, Point)

/* Named test values — avoids readability-magic-numbers warnings. */
#define PUSH_SINGLE_VALUE 42
#define PUSH_RETURN_VALUE 7
#define PUSH_ORDERED_FIRST 10
#define PUSH_ORDERED_SECOND 20
#define PUSH_ORDERED_THIRD 30
#define GROWTH_SCALE 10
#define CLEAR_FIRST_VAL 99
#define CLEAR_SECOND_VAL 77
#define FREE_REPUSH_VAL 55
#define CONTIGUOUS_A 100
#define CONTIGUOUS_B 200
#define CONTIGUOUS_C 300
#define FLOAT_VAL_A 1.5F
#define FLOAT_VAL_B 2.5F
#define I64_LARGE_VAL 9000000000LL
#define U32_BIT_PATTERN 0xDEADBEEFU
#define POINT1_Y 7
#define POINT2_X 9

/* ---- Initial state ---- */

void test_vec_initial_state_count_is_zero(void)
{
    vec_int vector = {0};
    TEST_ASSERT_EQUAL_INT(0, vector.count);
}

void test_vec_initial_state_capacity_is_zero(void)
{
    vec_int vector = {0};
    TEST_ASSERT_EQUAL_INT(0, vector.capacity);
}

void test_vec_initial_state_data_is_null(void)
{
    vec_int vector = {0};
    TEST_ASSERT_NULL(vector.data);
}

/* ---- Push: count and value correctness ---- */

void test_vec_push_increments_count(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_EQUAL_INT(1, vector.count);
    vec_int_free(&vector);
}

void test_vec_push_single_value_readable(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, PUSH_SINGLE_VALUE));
    TEST_ASSERT_EQUAL_INT(PUSH_SINGLE_VALUE, vector.data[0]);
    vec_int_free(&vector);
}

void test_vec_push_multiple_values_in_order(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, PUSH_ORDERED_FIRST));
    TEST_ASSERT_TRUE(vec_int_push(&vector, PUSH_ORDERED_SECOND));
    TEST_ASSERT_TRUE(vec_int_push(&vector, PUSH_ORDERED_THIRD));
    TEST_ASSERT_EQUAL_INT(3, vector.count);
    TEST_ASSERT_EQUAL_INT(PUSH_ORDERED_FIRST, vector.data[0]);
    TEST_ASSERT_EQUAL_INT(PUSH_ORDERED_SECOND, vector.data[1]);
    TEST_ASSERT_EQUAL_INT(PUSH_ORDERED_THIRD, vector.data[2]);
    vec_int_free(&vector);
}

void test_vec_push_returns_true_on_success(void)
{
    vec_int vector = {0};
    bool result = vec_int_push(&vector, PUSH_RETURN_VALUE);
    TEST_ASSERT_TRUE(result);
    vec_int_free(&vector);
}

/* ---- Allocation on first push ---- */

void test_vec_first_push_allocates_data(void)
{
    vec_int vector = {0};
    TEST_ASSERT_NULL(vector.data);
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_NOT_NULL(vector.data);
    vec_int_free(&vector);
}

void test_vec_first_push_sets_initial_capacity(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY, vector.capacity);
    vec_int_free(&vector);
}

/* ---- Growth / realloc ---- */

void test_vec_push_up_to_capacity_no_extra_alloc(void)
{
    vec_int vector = {0};
    for (int index = 0; index < VEC_INITIAL_CAPACITY; index++) {
        TEST_ASSERT_TRUE(vec_int_push(&vector, index));
    }
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY, vector.count);
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY, vector.capacity);
    vec_int_free(&vector);
}

void test_vec_push_one_past_capacity_triggers_growth(void)
{
    vec_int vector = {0};
    for (int index = 0; index <= VEC_INITIAL_CAPACITY; index++) {
        TEST_ASSERT_TRUE(vec_int_push(&vector, index));
    }
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY + 1, vector.count);
    TEST_ASSERT_EQUAL_INT(VEC_INITIAL_CAPACITY * VEC_GROWTH_FACTOR, vector.capacity);
    vec_int_free(&vector);
}

void test_vec_growth_preserves_existing_values(void)
{
    vec_int vector = {0};
    int push_count = VEC_INITIAL_CAPACITY + 4;
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_TRUE(vec_int_push(&vector, index * GROWTH_SCALE));
    }
    for (int index = 0; index < push_count; index++) {
        TEST_ASSERT_EQUAL_INT(index * GROWTH_SCALE, vector.data[index]);
    }
    vec_int_free(&vector);
}

void test_vec_multiple_growths_all_values_intact(void)
{
    vec_int vector = {0};
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
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    TEST_ASSERT_TRUE(vec_int_push(&vector, 2));
    vec_int_clear(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.count);
    vec_int_free(&vector);
}

void test_vec_clear_preserves_allocation(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    int capacity_before = vector.capacity;
    vec_int_clear(&vector);
    TEST_ASSERT_NOT_NULL(vector.data);
    TEST_ASSERT_EQUAL_INT(capacity_before, vector.capacity);
    vec_int_free(&vector);
}

void test_vec_clear_allows_repush(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, CLEAR_FIRST_VAL));
    vec_int_clear(&vector);
    TEST_ASSERT_TRUE(vec_int_push(&vector, CLEAR_SECOND_VAL));
    TEST_ASSERT_EQUAL_INT(1, vector.count);
    TEST_ASSERT_EQUAL_INT(CLEAR_SECOND_VAL, vector.data[0]);
    vec_int_free(&vector);
}

void test_vec_clear_on_empty_is_safe(void)
{
    vec_int vector = {0};
    vec_int_clear(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.count);
    TEST_ASSERT_NULL(vector.data);
}

/* ---- free ---- */

void test_vec_free_resets_count_to_zero(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.count);
}

void test_vec_free_resets_capacity_to_zero(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_EQUAL_INT(0, vector.capacity);
}

void test_vec_free_resets_data_to_null(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_NULL(vector.data);
}

void test_vec_free_on_empty_is_safe(void)
{
    vec_int vector = {0};
    vec_int_free(&vector);
    TEST_ASSERT_NULL(vector.data);
}

void test_vec_free_allows_repush(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, 1));
    vec_int_free(&vector);
    TEST_ASSERT_TRUE(vec_int_push(&vector, FREE_REPUSH_VAL));
    TEST_ASSERT_EQUAL_INT(1, vector.count);
    TEST_ASSERT_EQUAL_INT(FREE_REPUSH_VAL, vector.data[0]);
    vec_int_free(&vector);
}

/* ---- Data contiguity ---- */

void test_vec_data_is_contiguous_in_memory(void)
{
    vec_int vector = {0};
    TEST_ASSERT_TRUE(vec_int_push(&vector, CONTIGUOUS_A));
    TEST_ASSERT_TRUE(vec_int_push(&vector, CONTIGUOUS_B));
    TEST_ASSERT_TRUE(vec_int_push(&vector, CONTIGUOUS_C));
    /* Pointer arithmetic on .data must match indexed access */
    TEST_ASSERT_EQUAL_INT(CONTIGUOUS_A, *(vector.data + 0));
    TEST_ASSERT_EQUAL_INT(CONTIGUOUS_B, *(vector.data + 1));
    TEST_ASSERT_EQUAL_INT(CONTIGUOUS_C, *(vector.data + 2));
    vec_int_free(&vector);
}

/* ---- Type coverage ---- */

void test_vec_float_push_and_read(void)
{
    vec_float vector = {0};
    TEST_ASSERT_TRUE(vec_float_push(&vector, FLOAT_VAL_A));
    TEST_ASSERT_TRUE(vec_float_push(&vector, FLOAT_VAL_B));
    TEST_ASSERT_EQUAL_FLOAT(FLOAT_VAL_A, vector.data[0]);
    TEST_ASSERT_EQUAL_FLOAT(FLOAT_VAL_B, vector.data[1]);
    vec_float_free(&vector);
}

void test_vec_bool_push_and_read(void)
{
    vec_bool vector = {0};
    TEST_ASSERT_TRUE(vec_bool_push(&vector, true));
    TEST_ASSERT_TRUE(vec_bool_push(&vector, false));
    TEST_ASSERT_TRUE(vector.data[0]);
    TEST_ASSERT_FALSE(vector.data[1]);
    vec_bool_free(&vector);
}

void test_vec_i64_push_and_read(void)
{
    vec_i64 vector = {0};
    int64_t large_value = I64_LARGE_VAL;
    TEST_ASSERT_TRUE(vec_i64_push(&vector, large_value));
    TEST_ASSERT_EQUAL_INT64(large_value, vector.data[0]);
    vec_i64_free(&vector);
}

void test_vec_u32_push_and_read(void)
{
    vec_u32 vector = {0};
    TEST_ASSERT_TRUE(vec_u32_push(&vector, U32_BIT_PATTERN));
    TEST_ASSERT_EQUAL_UINT32(U32_BIT_PATTERN, vector.data[0]);
    vec_u32_free(&vector);
}

/* ---- Custom struct type ---- */

void test_vec_struct_push_and_read(void)
{
    vec_point vector = {0};
    TEST_ASSERT_TRUE(vec_point_push(&vector, (Point){.x_pos = 3, .y_pos = POINT1_Y}));
    TEST_ASSERT_TRUE(vec_point_push(&vector, (Point){.x_pos = POINT2_X, .y_pos = 1}));
    TEST_ASSERT_EQUAL_INT(2, vector.count);
    TEST_ASSERT_EQUAL_INT(3, vector.data[0].x_pos);
    TEST_ASSERT_EQUAL_INT(POINT1_Y, vector.data[0].y_pos);
    TEST_ASSERT_EQUAL_INT(POINT2_X, vector.data[1].x_pos);
    TEST_ASSERT_EQUAL_INT(1, vector.data[1].y_pos);
    vec_point_free(&vector);
}

void test_vec_struct_growth_preserves_fields(void)
{
    vec_point vector = {0};
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
