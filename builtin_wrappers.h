#pragma once
#include "typedef.h"
#include <immintrin.h>


// builtin wrappers (why the fuck do they differ between compilers!!! the world is retarded)
#ifndef _WIN32
    /* bit manipulation */
inline int count_trailing_zeros_u16(unsigned short a) {
    // @Note This has to be copied between compiler directives because gcc will not compile
    // tzcnt16 with only one leading undescore. I assume this is a compiler bug, because tzcnt32
    // and 64 both want one leading underscore...
    return (int)__tzcnt_u16(a);
}
inline int count_trailing_zeros_u32(unsigned int a) {
    return (int)_tzcnt_u32(a);
}
inline int count_trailing_zeros_u64(u64 a) {
    return (int)_tzcnt_u64(a);
}
inline int count_leading_zeros_u16(u16 mask) {
    return __builtin_clzs(mask);
}
inline int count_leading_zeros_u32(u32 mask) {
    return __builtin_clzl(mask);
}
inline int count_leading_zeros_u64(u64 mask) {
    return __builtin_clzll(mask);
}
inline int pop_count16(u16 num) {
    u32 tmp = num;
    tmp &= 0x0000ffff; // just to be sure;
    return (int)__builtin_popcount(tmp);
}
inline int pop_count32(u32 num) {
    return (int)__builtin_popcountl(num);
}
inline int pop_count64(u64 num) {
    return (int)__builtin_popcountll(num);
}

    /* math */
inline float sqrtf(float num) {
    return __builtin_sqrtf(num); // @Todo add for windows (I cannot find builtin for windows)
}
inline float sinf(float x) {
    return __builtin_sinf(x);
}
inline float asinf(float x) {
    return __builtin_asinf(x);
}
inline float cosf(float x) {
    return __builtin_cosf(x);
}
inline float acosf(float x) {
    return __builtin_acosf(x);
}
#else
inline int count_trailing_zeros_u16(unsigned short a) {
    return (int)_tzcnt_u16(a);
}
inline int count_trailing_zeros_u32(unsigned int a) {
    return (int)_tzcnt_u32(a);
}
inline int count_trailing_zeros_u64(u64 a) {
    return (int)_tzcnt_u64(a);
}
inline int count_leading_zeros_u16(u16 mask) {
    return __lzcnt16(mask);
}
inline int count_leading_zeros_u32(u32 mask) {
    return __lzcnt(mask);
}
inline int count_leading_zeros_u64(u64 mask) {
    return __lzcnt64(mask);
}
inline int pop_count16(u16 num) {
    return (int)__popcnt16(num);
}
inline int pop_count32(u32 num) {
    return (int)__popcnt(num);
}
inline int pop_count64(u64 num) {
    return (int)__popcnt64(num);
}

// math
inline float sinf(float x) {
    return sinf(x);
}
inline float asinf(float x) {
    return asinf(x);
}
inline float cosf(float x) {
    return cosf(x);
}
inline float acosf(float x) {
    return acosf(x);
}
#endif
