#ifndef SOL_ARRAY_HPP_INCLUDE_GUARD_
#define SOL_ARRAY_HPP_INCLUDE_GUARD_

#include "basic.h"
#include "assert.h"

enum Array_Flag_Bits { // @Note This order must not change, functions rely on the order of these bits
    ARRAY_GROWABLE_BIT = 0x01,
    ARRAY_TEMP_BIT     = 0x02,
};
typedef u8 Array_Flags;

//
// I am trying out a bit of branching on this array. Normally I really do not like branching, especially on smtg as
// integral as an array, but where I really want straight optimisation I would just use a pointer and a u32 anyway,
// and I have a feeling the branching on this thing does not really matter, as I cannot see the branches being
// unpredictable. The typical use case where branching is expensive is in a loop where a new path is consistently
// randomly taken, but in such a loop where you are appending to this array, the branches will always be the same.
// It would only be a problem if you are using different arrays with different flags in the same loop. But then just
// avoid this use case... If you know it can branch for convenience elsewhere, then avoid doing stuff with it where
// the branches are unpredictable. Furthermore, if you want straight optimisation then you have a static array,
// which if it branches will just crash the program, so no overhead from a missed branch.
//
// PLUS! The branches are only hit if the array has overflowed, (which would have always been a missed prediction)
// so if you dont overflow it (as you would anywhere where you want to go fast) then all the convenience and no
// overhead.
//

template <typename T>
struct Array {
    u32  len;
    u32  cap;
    T   *data;
    Array_Flags flags;
};

template<typename T>
inline static Array<T> new_array_from_ptr(T *ptr, u32 cap) {
    Array<T> array = {};
    array.data  = ptr;
    array.flags = 0x0;
    array.cap   = cap;
    return array;
}

template<typename T>
inline static Array<T> new_array(u64 cap, bool growable, bool temp) {
    Array<T> ret = {};
    ret.cap = align(cap, 16);

    ret.flags  = 0x0;
    ret.flags |= growable ? ARRAY_GROWABLE_BIT : 0x0;
    ret.flags |= temp     ? ARRAY_TEMP_BIT     : 0x0;

    // OMFG!! I spent **hours** searching for 'malloc(cap)'
    if (temp)
        ret.data = (T*)malloc_t(cap * sizeof(T), 16);
    else
        ret.data = (T*)malloc_h(cap * sizeof(T), 16);

    return ret;
}

template<typename T>
inline static void free_array(Array<T> *array) {
    if ((array->flags & ARRAY_TEMP_BIT) == 0)
        free_h(array->data);
    *array = {};
}

template<typename T>
inline static void array_do_resize(Array<T> *array) {
    if (array->flags & ARRAY_GROWABLE_BIT) {
        array->cap  *= 2;
        if (array->flags & ARRAY_TEMP_BIT) {
            u8* old_mem = (u8*)array->data;
            array->data = (T*)malloc_t(array->cap * sizeof(T), 16);

            memcpy(array->data, old_mem, array->len * sizeof(T));
        } else {
            array->data = (T*)realloc_h(array->data, array->cap * sizeof(T));
        }
    } else {
        assert(false && "Array Overflow");
        *array = {}; // Make it clear that smtg failed even in release
        return;
    }
}

inline static void array_add(Array<u32> *array, u32 t) {
    if (array->cap <= array->len)
        array_do_resize(array);

    array->data[array->len] = t;
    array->len++;
}

template<typename T>
inline static void array_add(Array<T> *array, T *t) {
    if (array->cap <= array->len)
        array_do_resize(array);

    array->data[array->len] = *t;
    array->len++;
}

template<typename T>
inline static T* array_append(Array<T> *array) {
    if (array->cap <= array->len)
        array_do_resize(array);

    array->len++;
    return &array->data[array->len - 1];
}

template<typename T>
inline static T* array_last(Array<T> *array) {
    return &array->data[array->len - 1];
}

template<typename T>
inline static T* array_pop(Array<T> *array) {
    array->len--;
    return array->data[array->len];
}

template<typename T>
inline static T* array_get(Array<T> *array, u32 index) {
    if (index >= array->len) {
        assert(false && "Array Out of Bounds Access");
        *array = {};
        return NULL;
    }
    return &array->data[index];
}

#endif // include guard
