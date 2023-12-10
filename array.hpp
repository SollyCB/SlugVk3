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
inline static Array<T> new_array(u64 cap, bool growable, bool temp) {
    Array<T> ret = {};
    ret.cap = cap;

    ret.flags = (growable & ARRAY_GROWABLE_BIT) | ((temp << 1) & ARRAY_TEMP_BIT);

    if (ret.flags & ARRAY_TEMP_BIT)
        ret.data = malloc_t(cap, 16);
    else
        ret.data = malloc_h(cap, 16);

    return ret;
}

template<typename T>
inline static void free_array(Array<T> *array) {
    if ((array->flags & ARRAY_TEMP_BIT) == 0)
        free_h(array->data);
    *array = {};
}

template<typename T>
inline static void array_add(Array<T> *array, T *t) {
    if (array->cap == array->len) {
        if (array->flags & ARRAY_GROWABLE_BIT) {

            array->cap  *= 2;
            if (array->flags & ARRAY_TEMP_BIT) {
                u8* old_mem = (u8*)array->data;
                array->data = (T*)malloc_t(sizeof(T) * array->cap, 16);

                memcpy(array->data, old_mem, array->len * sizeof(T));
            } else {
                array->data = (T*)realloc_h(array->data, array->cap);
            }
        } else {
            assert(false && "Array Overflow");
            *array = {}; // Make it clear that smtg failed even in release
            return;
        }
    }

    array->data[array->len] = *t;
    array->len++;
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

/*
   Old Interface: Ancient History Arrays - Literally not used anywhere except in my scuffed, 'just get a working,
   useful impl' test suite (eventually I will update it)
*/

// @Note Both of these could probably use u32s to measure length, as 4 billion items seems enough?? smtg to test

// Uses the heap allocator, and will grow on overflow. Data pointer aligned to 8 bytes.
template<typename T>
struct Dyn_Array {
    u64 len;
    u64 cap;
    T *data;
};

template<typename T>
inline void init_dyn_array(Dyn_Array<T> *array, u64 item_count) {
    array->cap = item_count;
    array->len = 0;
    array->data = (T*)malloc_h(item_count * sizeof(T), 8);
}
template<typename T>
inline void kill_dyn_array(Dyn_Array<T> *array) {
    free_h((void*)array->data);
}

template<typename T>
inline Dyn_Array<T> get_dyn_array(u64 size) {
    Dyn_Array<T> ret;
    init_dyn_array<T>(&ret, size);
    return ret;
}

template<typename T>
inline void grow_dyn_array(Dyn_Array<T> *array, u64 item_count) {
    T* old_data = array->data;

    array->cap += item_count;
    array->data = (T*)malloc_h(array->cap * sizeof(T), 8);

    memcpy(array->data, old_data, array->len * sizeof(T));
    free_h(old_data);
}

template<typename T>
inline T* append_to_dyn_array(Dyn_Array<T> *array) {
    if (array->len == array->cap)
        grow_dyn_array<T>(array, array->cap);

    array->len++;
    return array->data + array->len - 1;
}

template<typename T>
inline T* pop_last_dyn_array(Dyn_Array<T> *array) {
    array->len--;
    return &array->data[array->len];
}
template<typename T>
inline void copy_to_dyn_array(Dyn_Array<T> *array, T *from, u64 item_count) {
    // @Note how much to grow by?
    if (array->len + item_count > array->cap)
        grow_dyn_array(array, item_count);

    memcpy(array->data + array->len, from, item_count * sizeof(T));
    array->len += item_count;
}

template<typename T>
inline T* index_array(Dyn_Array<T> *array, u64 index) {
    assert(index < array->len && "Dyn Array Out of Bounds Access");
    return array->data + index;
}

// macro helpers
#define INIT_DYN_ARRAY(array, item_count) \
    init_dyn_array(&array, item_count);
#define KILL_DYN_ARRAY(array) \
    kill_dyn_array(&array);
#define GET_DYN_ARRAY(item_count) \
    get_dyn_array(size);
#define GROW_DYN_ARRAY(array, item_count) \
    grow_dyn_array(&array, item_count);
#define APPEND_TO_DYN_ARRAY(array) \
    append_to_dyn_array(&array);
#define COPY_TO_DYN_ARRAY(array, from, item_count) \
    copy_to_dyn_array(&array, &from, item_count);
#define INDEX_DYN_ARRAY(array, index) \
    index_dyn_array(&array, index);

#endif
