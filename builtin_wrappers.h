#ifndef SOL_BUILTIN_WRAPPERS_H_INCLUDE_GUARD_
#define SOL_BUILTIN_WRAPPERS_H_INCLUDE_GUARD_

#include <immintrin.h>
#include "typedef.h"

// builtin wrappers (why the fuck do they differ between compilers!!! the world is retarded)
#ifndef _WIN32
    /* bit manipulation */
inline static int count_trailing_zeros_u16(unsigned short a) {
    // @Note This has to be copied between compiler directives because gcc will not compile
    // tzcnt16 with only one leading undescore. I assume this is a compiler bug, because tzcnt32
    // and 64 both want one leading underscore...
    return (int)__tzcnt_u16(a);
}
inline static int count_trailing_zeros_u32(unsigned int a) {
    return (int)_tzcnt_u32(a);
}
inline static int count_trailing_zeros_u64(u64 a) {
    return (int)_tzcnt_u64(a);
}
inline static int count_leading_zeros_u16(u16 mask) {
    return __builtin_clzs(mask);
}
inline static int count_leading_zeros_u32(u32 mask) {
    return __builtin_clzl(mask);
}
inline static int count_leading_zeros_u64(u64 mask) {
    return __builtin_clzll(mask);
}
inline static int pop_count16(u16 num) {
    u32 tmp = num;
    tmp &= 0x0000ffff; // just to be sure;
    return (int)__builtin_popcount(tmp);
}
inline static int pop_count32(u32 num) {
    return (int)__builtin_popcountl(num);
}
inline static int pop_count64(u64 num) {
    return (int)__builtin_popcountll(num);
}

    /* math */
//
// Suddenly complains about multiply defined functions, but it didnt before. I guess there is some new include chain
// since I updated to some of my newer implementations. This is annoying as now I do not know how to wrap builtins,
// but I assume that the math functions are calling them anyway. Idk, cba to check rn, as long as they work whatever.
// I have more important shit to work on. - Sol 21 Nov 2023
//
//inline static float sqrtf(float num) {
//    return __builtin_sqrtf(num); // @Todo add for windows (I cannot find builtin for windows)
//}
//inline static float sinf(float x) {
//    return __builtin_sinf(x);
//}
//inline static float asinf(float x) {
//    return __builtin_asinf(x);
//}
//inline static float cosf(float x) {
//    return __builtin_cosf(x);
//}
//inline static float acosf(float x) {
//    return __builtin_acosf(x);
//}
#else
inline static int count_trailing_zeros_u16(unsigned short a) {
    return (int)_tzcnt_u16(a);
}
inline static int count_trailing_zeros_u32(unsigned int a) {
    return (int)_tzcnt_u32(a);
}
inline static int count_trailing_zeros_u64(u64 a) {
    return (int)_tzcnt_u64(a);
}
inline static int count_leading_zeros_u16(u16 mask) {
    return __lzcnt16(mask);
}
inline static int count_leading_zeros_u32(u32 mask) {
    return __lzcnt(mask);
}
inline static int count_leading_zeros_u64(u64 mask) {
    return __lzcnt64(mask);
}
inline static int pop_count16(u16 num) {
    return (int)__popcnt16(num);
}
inline static int pop_count32(u32 num) {
    return (int)__popcnt(num);
}
inline static int pop_count64(u64 num) {
    return (int)__popcnt64(num);
}

// math
inline static float sinf(float x) {
    return sinf(x);
}
inline static float asinf(float x) {
    return asinf(x);
}
inline static float cosf(float x) {
    return cosf(x);
}
inline static float acosf(float x) {
    return acosf(x);
}
#endif // WIN32 or not
#endif // include guard
