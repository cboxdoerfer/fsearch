#include <glib.h>
#include <stdint.h>
#include <stdlib.h>

#include <src/fsearch_array.h>

typedef struct Version {
    int major;
    int minor;
} Version;

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

    const int32_t upper_limit = 128;
    for (int32_t i = 0; i < upper_limit; ++i) {
        darray_add_item(array, GINT_TO_POINTER(i));
    }
    for (int32_t i = 0; i < upper_limit; ++i) {
        int32_t j = GPOINTER_TO_INT(darray_get_item(array, i));
        g_assert_true(i == j);
    }
    g_assert_true(darray_get_num_items(array) == upper_limit);

    darray_sort(array, (DynamicArrayCompareDataFunc)sort_int_descending, NULL, NULL);
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

    darray_sort_multi_threaded(array, (DynamicArrayCompareDataFunc)sort_int_ascending, NULL, NULL);
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

static void
same_elements(void) {
    DynamicArray *array = darray_new(10);

    uint32_t element = 42;
    for (int32_t i = 0; i < 10; ++i) {
        darray_add_item(array, GINT_TO_POINTER(element));
    }

    for (uint32_t i = 0; i < element * 2; ++i) {
        if (i == element) {
            continue;
        }
        uint32_t matched_idx = 0;
        if (darray_binary_search_with_data(array,
                                           GINT_TO_POINTER(i),
                                           (DynamicArrayCompareDataFunc)sort_int_ascending,
                                           NULL,
                                           &matched_idx)) {
            g_assert_not_reached();
        }
    }

    g_clear_pointer(&array, darray_unref);
}

static int32_t
sort_version(void **a, void **b, void *data) {
    Version *v1 = *a;
    Version *v2 = *b;
    int res = v1->major - v2->major;
    if (res != 0) {
        return res;
    }
    return v1->minor - v2->minor;
}

static void
test_single_and_multi_threaded_sort(DynamicArray *array, DynamicArrayCompareDataFunc comp_func) {
    DynamicArray *a1 = darray_copy(array);
    DynamicArray *a2 = darray_copy(array);
    darray_sort(a1, (DynamicArrayCompareDataFunc)sort_version, NULL, NULL);
    darray_sort_multi_threaded(a2, (DynamicArrayCompareDataFunc)sort_version, NULL, NULL);
    for (uint32_t i = 0; i < darray_get_num_items(a1); ++i) {
        Version *v1 = darray_get_item(a1, i);
        Version *v2 = darray_get_item(a2, i);
        g_print("%d.%d / %d.%d\n", v1->major, v1->minor, v2->major, v2->minor);
        g_assert(v1 == v2);
    }
}

static void
test_sort(void) {
    Version versions[] = {
        {3, 0},
        {4, 1},
        {4, 3},
        {1, 5},
        {1, 4},
        {2, 6},
        {0, 7},
        {2, 8},
        {1, 9},
        {0, 9},
        {0, 9},
        {0, 9},
        {4, 2},
        {0, 9},
        {0, 9},
        {0, 9},
    };

    DynamicArray *array = darray_new(10);
    for (uint32_t i = 0; i < G_N_ELEMENTS(versions); i++) {
        darray_add_item(array, &versions[i]);
    }
    test_single_and_multi_threaded_sort(array, (DynamicArrayCompareDataFunc)sort_version);
}

static void
test_search(void) {
    same_elements();
}

static void
test_remove(void) {
    const int32_t upper_limit = 10;
    g_autoptr(DynamicArray) array = darray_new(upper_limit);
    g_assert_true(darray_get_size(array) == upper_limit);

    g_assert_true(darray_get_num_items(array) == 0);
    darray_remove(array, 0, upper_limit);
    g_assert_true(darray_get_num_items(array) == 0);
    darray_remove(array, 1, 1);
    g_assert_true(darray_get_num_items(array) == 0);

    for (int32_t i = 0; i < upper_limit; ++i) {
        darray_add_item(array, GINT_TO_POINTER(i));
    }
    darray_remove(array, 1, 0);
    g_assert_true(darray_get_num_items(array) == upper_limit);

    darray_remove(array, 4, 2);
    g_assert_true(darray_get_num_items(array) == upper_limit - 2);
    g_assert_true(GPOINTER_TO_INT(darray_get_item(array, 3)) == 3);
    g_assert_true(GPOINTER_TO_INT(darray_get_item(array, 4)) == 6);

    darray_remove(array, 1, upper_limit);
    g_print("%d\n", darray_get_num_items(array));
    g_assert_true(darray_get_num_items(array) == 1);
}

static void
test_insert(void) {
    const int32_t upper_limit = 10;
    g_autoptr(DynamicArray) array = darray_new(upper_limit);
    for (int32_t i = 0; i < upper_limit; ++i) {
        darray_insert_item(array, GINT_TO_POINTER(i), i);
    }
    g_assert_cmpint(upper_limit, ==, darray_get_num_items(array));
    for (int32_t i = 0; i < upper_limit; ++i) {
        int32_t j = GPOINTER_TO_INT(darray_get_item(array, i));
        g_assert_cmpint(i, ==, j);
    }

    darray_insert_item(array, GINT_TO_POINTER(42), 0);
    g_assert_cmpint(42, ==, GPOINTER_TO_INT(darray_get_item(array, 0)));
    g_assert_cmpint(upper_limit + 1, ==, darray_get_num_items(array));

    darray_insert_item(array, GINT_TO_POINTER(21), darray_get_num_items(array));
    g_assert_cmpint(21, ==, GPOINTER_TO_INT(darray_get_item(array, darray_get_num_items(array) - 1)));
    g_assert_cmpint(upper_limit + 2, ==, darray_get_num_items(array));
}

static void
test_insert_sorted(void) {
    Version versions[] = {
        {3, 0},
        {4, 1},
        {4, 3},
        {1, 5},
        {1, 4},
        {2, 6},
        {0, 7},
        {2, 8},
        {1, 9},
        {0, 9},
        {0, 9},
        {0, 9},
        {4, 2},
        {0, 9},
        {0, 9},
        {0, 9},
    };

    g_autoptr(DynamicArray) array_sorted_once = darray_new(10);
    for (uint32_t i = 0; i < G_N_ELEMENTS(versions); i++) {
        darray_add_item(array_sorted_once, &versions[i]);
    }
    darray_sort(array_sorted_once, (DynamicArrayCompareDataFunc)sort_version, NULL, NULL);

    g_autoptr(DynamicArray) array_insert_sorted = darray_new(10);
    for (uint32_t i = 0; i < G_N_ELEMENTS(versions); i++) {
        darray_insert_item_sorted(array_insert_sorted, &versions[i], (DynamicArrayCompareDataFunc)sort_version, NULL);
    }

    for (uint32_t i = 0; i < G_N_ELEMENTS(versions); i++) {
        Version *v1 = darray_get_item(array_sorted_once, i);
        Version *v2 = darray_get_item(array_insert_sorted, i);
        g_assert_cmpint(v1->major, ==, v2->major);
        g_assert_cmpint(v1->minor, ==, v2->minor);
    }
}

static void
test_steal(void) {
    const uint32_t count = 20;
    g_autoptr(DynamicArray) source = darray_new(count);
    g_autoptr(DynamicArray) dest = darray_new(0);

    for (uint32_t i = 0; i < count; i++) {
        darray_add_item(source, GINT_TO_POINTER(i));
    }

    // Steal middle 5 items (indices 5 to 9)
    const uint32_t n_steal = 6;
    const uint32_t i_steal = 7;
    uint32_t stolen = darray_steal(source, i_steal, n_steal, dest);
    g_assert_cmpuint(stolen, ==, n_steal);
    g_assert_cmpuint(darray_get_num_items(source), ==, count - n_steal);
    g_assert_cmpuint(darray_get_num_items(dest), ==, n_steal);

    // Verify content of dest
    for (uint32_t i = 0; i < darray_get_num_items(dest); i++) {
        g_assert_cmpint(GPOINTER_TO_INT(darray_get_item(dest, i)), ==, i + i_steal);
    }

    // Verify content of source (indices shifted)
    g_assert_cmpint(GPOINTER_TO_INT(darray_get_item(source, i_steal - 1)), ==, i_steal - 1);
    g_assert_cmpint(GPOINTER_TO_INT(darray_get_item(source, i_steal)), ==, i_steal + n_steal);
    g_assert_cmpint(GPOINTER_TO_INT(darray_get_item(source, darray_get_num_items(source) - 1)), ==, count - 1);
}

static bool
is_even(void *item, void *data) {
    return (GPOINTER_TO_INT(item) % 2) == 0;
}

static void
test_steal_items_func(void) {
    const uint32_t count = 10;
    g_autoptr(DynamicArray) source = darray_new(count);

    for (uint32_t i = 0; i < count; i++) {
        darray_add_item(source, GINT_TO_POINTER(i + 1));
    }

    g_autoptr(DynamicArray) evens = darray_steal_items(source, is_even, NULL);
    g_assert_cmpuint(darray_get_num_items(evens), ==, count / 2);
    g_assert_cmpuint(darray_get_num_items(source), ==, count / 2);

    for (uint32_t i = 0; i < darray_get_num_items(evens); i++) {
        g_assert_true(is_even(darray_get_item(evens, i), NULL));
    }
    for (uint32_t i = 0; i < darray_get_num_items(source); i++) {
        g_assert_false(is_even(darray_get_item(source, i), NULL));
    }
}

static void
test_range(void) {
    const uint32_t count = 10;
    g_autoptr(DynamicArray) array = darray_new(count);
    for (int i = 0; i < count; i++) {
        darray_add_item(array, GINT_TO_POINTER(i));
    }

    const uint32_t n_range = 4;
    const uint32_t i_range = 3;
    g_autoptr(DynamicArray) range = darray_get_range(array, i_range, n_range);
    g_assert_cmpuint(darray_get_num_items(range), ==, n_range);
    g_assert_cmpint(GPOINTER_TO_INT(darray_get_item(range, 0)), ==, i_range);
    g_assert_cmpint(GPOINTER_TO_INT(darray_get_item(range, n_range - 1)), ==, i_range + n_range - 1);
}

static void
test_copy_ref(void) {
    const int32_t val = 100;
    DynamicArray *a1 = darray_new(5);
    darray_add_item(a1, GINT_TO_POINTER(val));

    // Test ref
    DynamicArray *a2 = darray_ref(a1);
    g_assert(a1 == a2);
    darray_unref(a2);

    // Test copy
    DynamicArray *a3 = darray_copy(a1);
    g_assert(a1 != a3);
    g_assert_cmpuint(darray_get_num_items(a3), ==, 1);
    g_assert_cmpint(GPOINTER_TO_INT(darray_get_item(a3, 0)), ==, val);

    darray_unref(a3);
    darray_unref(a1);
}

int
main(int argc, char *argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/FSearch/array/main", test_main);
    g_test_add_func("/FSearch/array/insert", test_insert);
    g_test_add_func("/FSearch/array/insert_sorted", test_insert_sorted);
    g_test_add_func("/FSearch/array/remove", test_remove);
    g_test_add_func("/FSearch/array/steal", test_steal);
    g_test_add_func("/FSearch/array/steal_items_func", test_steal_items_func);
    g_test_add_func("/FSearch/array/range", test_range);
    g_test_add_func("/FSearch/array/copy_ref", test_copy_ref);
    g_test_add_func("/FSearch/array/sort", test_sort);
    g_test_add_func("/FSearch/array/search", test_search);
    return g_test_run();
}
