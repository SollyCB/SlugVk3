#ifndef SOL_BASIC_H_INCLUDE_GUARD_
#define SOL_BASIC_H_INCLUDE_GUARD_

#include "typedef.h"
#include "print.h"
#include "assert.h"
#include "allocator.hpp"

inline static void sort_backend(int *array, int start, int end) {
    if (start < end) {
        int x = start - 1;
        int tmp;
        for(int i = start; i <= end; ++i) {
            if (array[i] < array[end]) {
                x++;
                tmp      = array[i];
                array[i] = array[x];
                array[x] = tmp;
            }
        }
        x++;
        tmp        = array[end];
        array[end] = array[x];
        array[x]   = tmp;

        sort_backend(array, start, x - 1);
        sort_backend(array, x + 1, end);
    }
}
inline static void sort_indices(u32 count, u32 *indices) {
    sort_backend((int*)indices, 0, count - 1);
}

#endif
