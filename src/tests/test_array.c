#include <glib.h>
#include <stdlib.h>
#include <stdint.h>

#include <src/fsearch_array.h>

static int32_t
sort_int_descending(void **a, void **b, void *data) {
    int32_t ia = GPOINTER_TO_INT(*a);
    int32_t ib = GPOINTER_TO_INT(*b);
    return ib - ia;
}

static int32_t
sort_int_ascending(void **a, void **b, void *data) {
    return -1 * sort_int_descending(a, b, data);
}

static void
test_main(void) {
    DynamicArray *array = darray_new(10);
    g_assert_true(darray_get_size(array) == 10);

    const int32_t upper_limit = 42;
    for (int32_t i = 0; i < upper_limit; ++i) {
        darray_add_item(array, GINT_TO_POINTER(i));
    }
    for (int32_t i = 0; i < upper_limit; ++i) {
        int32_t j = GPOINTER_TO_INT(darray_get_item(array, i));
        g_assert_true(i == j);
    }
    g_assert_true(darray_get_num_items(array) == upper_limit);

    darray_sort(array, (DynamicArrayCompareFunc)sort_int_descending);
    for (int32_t i = 0; i < upper_limit; ++i) {
        int32_t j = GPOINTER_TO_INT(darray_get_item(array, i));
        int32_t expected_val = upper_limit - i - 1;
        if (expected_val != j) {
            g_print("[sort] Expect %d at index %d. Result: %d\n", expected_val, i, j);
        }
        g_assert_true(expected_val == j);
        uint32_t matched_idx = 0;
        if (darray_binary_search_with_data(array,
                                           GINT_TO_POINTER(i),
                                           (DynamicArrayCompareDataFunc)sort_int_descending,
                                           NULL,
                                           &matched_idx)) {
            if (matched_idx != expected_val) {
                g_print("[bin_search] Expect %d to be at idx %d\n", i, expected_val);
            }
            g_assert_true(matched_idx == expected_val);
        }
        else {
            g_print("[bin_search] Didn't find %d!\n", i);
            g_assert_not_reached();
        }
    }

    darray_sort_multi_threaded(array, (DynamicArrayCompareFunc)sort_int_ascending);
    for (int32_t i = 0; i < upper_limit; ++i) {
        int32_t j = GPOINTER_TO_INT(darray_get_item(array, i));
        g_print("%d:%d\n", i, j);
    }
    for (int32_t i = 0; i < upper_limit; ++i) {
        int32_t j = GPOINTER_TO_INT(darray_get_item(array, i));
        int32_t expected_val = i;
        if (expected_val != j) {
            g_print("[threaded_sort] Expect %d at index %d. Result: %d\n", expected_val, i, j);
        }
        g_assert_true(expected_val == j);
        uint32_t matched_idx = 0;
        if (darray_binary_search_with_data(array,
                                           GINT_TO_POINTER(i),
                                           (DynamicArrayCompareDataFunc)sort_int_ascending,
                                           NULL,
                                           &matched_idx)) {
            if (expected_val != matched_idx) {
                g_print("[bin_search] Expect %d to be at idx %d\n", i, expected_val);
            }
            g_assert_true(matched_idx == expected_val);
        }
        else {
            g_print("[bin_search] Didn't find %d!\n", i);
            g_assert_not_reached();
        }
    }

    for (uint32_t i = 0; i < upper_limit - 1; ++i) {
        int32_t i1 = GPOINTER_TO_INT(darray_get_item(array, i));
        g_assert_true(i1 == i);
        uint32_t i2_idx = 0;
        int32_t i2 = GPOINTER_TO_INT(
            darray_get_item_next(array, GINT_TO_POINTER(i1), (DynamicArrayCompareDataFunc)sort_int_ascending, NULL, &i2_idx));
        g_assert_true(i2 == i1 + 1);
        g_assert_true(i2_idx == i1 + 1);
    }

    g_clear_pointer(&array, darray_unref);
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/array/main", test_main);
    return g_test_run();
}
