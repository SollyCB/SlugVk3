#ifndef SOL_SIMD_HPP_INCLUDE_GUARD_
#define SOL_SIMD_HPP_INCLUDE_GUARD_

#include <nmmintrin.h>
#include <emmintrin.h>
#include <immintrin.h>
#include "basic.h"
#include "builtin_wrappers.h"

/*
   *********************************************************

    WARNING! These functions have not been used enough to count as full stress tested. Normally I would be confident
    enough not to warn, some these do relatively complex things, so I will put a little alert here to catch my eye
    in case of weird errors.

   *********************************************************
*/

//
//  @Note Lots of (if not all) of these functions are inlined despite looking too long for inlining.
//  But in reality they only *look* long, because the number of instructions is very low, because of the intrinsics
//

//
// @Note some of the if statements look a little weird as they wrap while loops.
// The point of this is that in the Intel Optimization Manual, it states that fall through
// paths are predicted as being taken if there is not info in the branch buffer. Idk if this
// actually helps optimisation, as it adds a comparison, but this comparison is always the same
// as the next comparison (the while loop test)? I am trying it out anyway. One day I will test it,
// but I imagine it is completely negligible...
//

// @Todo Where possible, convert older methodologies to use this updated version I somehow only just realised:
/*
    while(inc < alloc->allocation_count) {
        for(u32 i = 0; i < pop_count16(mask); ++i) {
            tz = count_trailing_zeros_u16(mask);
            indices[idx] = inc + tz;
            mask ^= 1 << tz;
        }
        inc += 16;
        a = _mm_load_si128((__m128i*)(alloc->allocations + inc));
        a = _mm_and_si128(a, b);
        a = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(a);
    }
*/

// @Todo better document what these functions are doing. Some of the operations might look confusing
// if I come to them after not using simd for a while or smtg

// @Todo come up with more aesthetic, general string compares

// Whyyyyyy can't I turn on SVML intrinsics?
/* inline static Vec4 simd_sin_vec4(Vec4 *vec4) {
    __m128i a = _mm_loadu_si128((__m128i*)vec4);
    a = _mm_sin_ps(a);
    Vec4 *sin = (Vec4*)&a;
    return *sin;
}
inline static Vec4 simd_asin_vec4(Vec4 *vec4) {
    __m128i a = _mm_loadu_si128((__m128i*)vec4);
    a = _mm_asin_ps(a);
    Vec4 *asin = (Vec4*)&a;
    return *asin;
}
inline static Vec4 simd_cos_vec4(Vec4 *vec4) {
    __m128i a = _mm_loadu_si128((__m128i*)vec4);
    a = _mm_cos_ps(a);
    Vec4 *cos = (Vec4*)&a;
    return *cos;
}
inline static Vec4 simd_acos_vec4(Vec4 *vec4) {
    __m128i a = _mm_loadu_si128((__m128i*)vec4);
    a = _mm_acos_ps(a);
    Vec4 *acos = (Vec4*)&a;
    return *acos;
} */

// assumes safe to deref data up to 16 bytes, counts bytes up to a closing char
inline static int simd_strlen(const char *string, char close) {
    __m128i a = _mm_loadu_si128((__m128i*)string);
    __m128i b = _mm_set1_epi8(close);
    a = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    int count = pop_count16(mask);
    int inc = 0;
    if (!mask) {
        while(!mask) {
            inc += 16;
            a = _mm_loadu_si128((__m128i*)(string + inc));
            a = _mm_cmpeq_epi8(a, b);
            mask = _mm_movemask_epi8(a);
        }
    }
    return count_trailing_zeros_u16(mask) + inc;
}

inline static void simd_skip_passed_char_count(const char *string, char skip, int skip_count, u64 *pos) {
    __m128i a = _mm_loadu_si128((__m128i*)string);
    __m128i b = _mm_set1_epi8(skip);
    a = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    int count = pop_count16(mask);

    u64 inc = 0;
    while(count < skip_count) {
        inc += 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        a = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(a);
        count += pop_count16(mask);
    }

    // if count > skip_count,
    // find the index of the last matched char in the group which when added to count makes count == skip_count
    int lz = count_leading_zeros_u16(mask);
    if (count > skip_count) {
        int temp = count - pop_count16(mask);
        while(count > skip_count) {
            count = temp;
            mask ^= 0x8000 >> lz; // xor off the matched char at the highest index in the string
            count += pop_count16(mask);
            lz = count_leading_zeros_u16(mask);
        }
    }

    // when count == skip_count,
    // position in file += total increment + the distance to the last matched char in the group
    *pos += inc + (16 - lz);
}

inline static bool simd_find_char_interrupted(const char *string, char find, char interrupt, u64 *pos) {
    __m128i a = _mm_loadu_si128((__m128i*)string);
    __m128i b = _mm_set1_epi8(find);
    __m128i c = _mm_set1_epi8(interrupt);
    __m128i d = _mm_cmpeq_epi8(a, b);
    u16 mask1 = _mm_movemask_epi8(d);
    d = _mm_cmpeq_epi8(a, c);
    u16 mask2 = _mm_movemask_epi8(d);

    // @Note @Branching There is probably some smart way to do this to cut down branching...
    // Idk how to deal with the UB for mask == 0x0 other than branching... Maybe the compiler
    // will figure out smtg smart, @Todo check godbolt for this
    u64 inc = 0;
    while(!mask1 && !mask2) {
        inc += 16;

        // @HOLY FUCKING ANNOYING
        // Before, this function looked like this: _mm_loadu_si128((__m128i*)string + inc);
        // the cast was not wrapped... C++ casting rules / operator precedence is incredible.
        // Like at least there should be a warning for this for ambiguous pointer arithmetic.
        // In reality, why is pointer math not done in consistent units!!

        a = _mm_loadu_si128((__m128i*)(string + inc));
        d = _mm_cmpeq_epi8(a, b);
        mask1 = _mm_movemask_epi8(d);
        d = _mm_cmpeq_epi8(a, c);
        mask2 = _mm_movemask_epi8(d);
    }

    // Intel optimisation manual: fallthrough condition chosen if nothing in btb
    if (mask1 && mask2) {
        int tz1 = count_trailing_zeros_u16(mask1);
        int tz2 = count_trailing_zeros_u16(mask2);
        *pos += count_trailing_zeros_u16(mask2 | mask1) + inc;
        return tz1 < tz2;
    } else if (!mask1 && mask2) {
        *pos += count_trailing_zeros_u16(mask2) + inc;
        return false;
    }

    *pos += count_trailing_zeros_u16(mask1) + inc;
    return true;
}

inline static int simd_get_ascii_array_len(const char *string) {
    u64 inc = 0;
    int count = 0;
    while(simd_find_char_interrupted(string + inc, ',', ']', &inc)) {
        count++;
        inc++;
    }

    inc -= 16;
    __m128i a = _mm_loadu_si128((__m128i*)(string + inc));

    // @Note idk if there are more newline characters that I should be accounting for...
    __m128i b = _mm_set1_epi8(' ');
    __m128i c = _mm_set1_epi8('\n');
    __m128i d = _mm_set1_epi8('\t');
    __m128i e = _mm_or_si128(_mm_cmpeq_epi8(a, b), _mm_cmpeq_epi8(a, c));
    __m128i f = _mm_or_si128(_mm_cmpeq_epi8(a, d), e);
    u16 mask = _mm_movemask_epi8(f);
    while(mask == Max_u16) {
        inc -= 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        e = _mm_or_si128(_mm_cmpeq_epi8(a, b), _mm_cmpeq_epi8(a, c));
        f = _mm_or_si128(_mm_cmpeq_epi8(a, d), e);
        mask = _mm_movemask_epi8(f);
    }

    mask ^= 0xffff; // make easier to count trailing zeros
    u16 lz = count_leading_zeros_u16(mask);
    mask &= 0xffff & (0x8000 >> lz);
    b = _mm_set1_epi8(',');
    a = _mm_cmpeq_epi8(a, b);
    u16 mask2 = _mm_movemask_epi8(a);
    count++;
    count -= pop_count16(mask2 & mask);
    return count;
}

// Must be safe to assume that x and y have len 16 bytes, must return u16
inline static u64 simd_search_for_char(const char *string, char c) {
    __m128i a =  _mm_loadu_si128((const __m128i*)string);
    __m128i b =  _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    u64 inc = 0;
    while(!mask) {
        inc += 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        a = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(a);
    }
    u16 tz = count_trailing_zeros_u16(mask);
    return inc + tz;
}

// Must be safe to assume that x and y have len 16 bytes, must return u16
inline static u16 simd_strcmp_short(const char *x, const char *y, int pad) {
    __m128i a =  _mm_loadu_si128((const __m128i*)x);
    __m128i b =  _mm_loadu_si128((const __m128i*)y);
    a = _mm_cmpeq_epi8(a, b);
    return (_mm_movemask_epi8(a) << pad) ^ (0xffff << pad);
}
// Assumes strings are at least len 16, use only for 16 < strlen < 32
inline static u32 simd_strcmp_long(const char *x, const char *y, int pad) {
    __m256i a =  _mm256_loadu_si256((const __m256i*)x);
    __m256i b =  _mm256_loadu_si256((const __m256i*)y);
    a = _mm256_cmpeq_epi8(a, b);

    u32 mask = _mm256_movemask_epi8(a);
    return (mask << pad) ^ (0xffffffff << pad);
}

// Must be safe to assume that x has len 16 bytes
inline static u16 simd_match_char(const char *string, char c) {
    __m128i a = _mm_loadu_si128((__m128i*)string);
    __m128i b = _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    return _mm_movemask_epi8(a);
}

// Must be safe to assume string has len 16 bytes
inline static void simd_skip_to_char(const char *string, u64 *offset, char c) {
    u64 inc = 0;
    __m128i a = _mm_loadu_si128((__m128i*)(string + inc));
    __m128i b = _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    // if mask is empty, no character was matched in the 16 bytes
    while(!mask) {
        inc += 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        a = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(a);
    }
    int tz = count_trailing_zeros_u32(mask);
    *offset += tz + inc;
}
// Must be safe to assume string has len 16 bytes
inline static bool simd_skip_to_char(const char *string, u64 *offset, char c, u64 limit) {
    u64 inc = 0;
    __m128i a = _mm_loadu_si128((__m128i*)(string));
    __m128i b = _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    while(!mask) {
        inc += 16;
        if (inc > limit) {
            return false;
        }
        a = _mm_loadu_si128((__m128i*)(string + inc));
        a = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(a);
    }
    int tz = count_trailing_zeros_u32(mask);
    inc += tz;
    *offset += inc;
    return true;
}

// Must be safe to assume string has len 16 bytes
inline static void simd_skip_passed_char(const char *string, u64 *offset, char c) {
    u64 inc = 0;
    __m128i a = _mm_loadu_si128((__m128i*)(string + inc));
    __m128i b = _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    // if mask is empty, no character was matched in the 16 bytes
    while(!mask) {
        inc += 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        a = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(a);
    }
    int tz = count_trailing_zeros_u32(mask);
    *offset += tz + 1 + inc;
}
// Must be safe to assume string has len 16 bytes
inline static bool simd_skip_passed_char(const char *string, u64 *offset, char c, u64 limit) {
    u64 inc = 0;
    __m128i a = _mm_loadu_si128((__m128i*)(string + inc));
    __m128i b = _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    // if mask is empty, no character was matched in the 16 bytes
    while(!mask) {
        inc += 16;
        if (inc > limit) {
            inc -= 16;
            *offset += inc;
            return false;
        }
        a = _mm_loadu_si128((__m128i*)(string + inc));
        a = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(a);
    }
    int tz = count_trailing_zeros_u32(mask);
    *offset += tz + 1 + inc;
    return true;
}

inline static void simd_skip_whitespace(const char *string, u64 *offset) {
    u64 inc = 0;
    __m128i a = _mm_loadu_si128((__m128i*)string);
    __m128i b = _mm_set1_epi8(' ');
    __m128i c = _mm_set1_epi8('\n');
    // Idk if checking tabs is necessary but I think it is because for some reason tabs exist...
    // THEY ARE JUST SOME NUMBER OF SPACES! THERE ISNT EVEN CONSENSUS ON  HOW MANY SPACES!! JSUT SOME NUMBER OF THEM!
    __m128i e = _mm_set1_epi8('\t');
    __m128i d;
    d = _mm_cmpeq_epi8(a, b);
    u16 mask = _mm_movemask_epi8(d);
    d = _mm_cmpeq_epi8(a, e);
    mask |= _mm_movemask_epi8(d);
    d = _mm_cmpeq_epi8(a, c);
    mask |= _mm_movemask_epi8(d);
    while(mask == 0xffff) {
        inc += 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        d = _mm_cmpeq_epi8(a, b);
        mask = _mm_movemask_epi8(d);
        d = _mm_cmpeq_epi8(a, b);
        mask |= _mm_movemask_epi8(d);
        d = _mm_cmpeq_epi8(a, e);
        mask |= _mm_movemask_epi8(d);
    }
    int tz = count_trailing_zeros_u16(mask);
    *offset += inc + tz;
}

// Must be safe to assume string has len 16 bytes
inline static bool simd_skip_to_int(const char *string, u64 *offset) {
    __m128i b = _mm_set1_epi8(47); // ascii 0 - 1
    __m128i c = _mm_set1_epi8(58); // ascii 9 + 1

    __m128i a = _mm_loadu_si128((__m128i*)(string));
    __m128i d = _mm_cmpgt_epi8(a, b);
    a = _mm_cmplt_epi8(a, c);
    a = _mm_and_si128(d, a);
    u16 mask = _mm_movemask_epi8(a);

    u64 inc = 0;
    while(!mask) {
        inc += 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        d = _mm_cmpgt_epi8(a, b);
        a = _mm_cmplt_epi8(a, c);
        a = _mm_and_si128(d, a);
        mask = _mm_movemask_epi8(a);
    }

    int tz = count_trailing_zeros_u16(mask);
    *offset += inc + tz;

    return true;
}
// Must be safe to assume string has len 16 bytes
inline static bool simd_skip_to_int(const char *string, u64 *offset, u64 limit) {
    __m128i b = _mm_set1_epi8(47); // ascii 0 - 1
    __m128i c = _mm_set1_epi8(58); // ascii 9 + 1

    __m128i a = _mm_loadu_si128((__m128i*)(string));
    __m128i d = _mm_cmpgt_epi8(a, b);
    a = _mm_cmplt_epi8(a, c);
    a = _mm_and_si128(d, a);
    u16 mask = _mm_movemask_epi8(a);

    u64 inc = 0;
    while(!mask) {
        inc += 16;
        // @Note inc can be less than limit, but then the match check will check beyond limit.
        // So make limit limit the check range, dont place it at the eof for example...
        if (inc > limit) {
            return false;
        }

        a = _mm_loadu_si128((__m128i*)(string + inc));
        d = _mm_cmpgt_epi8(a, b);
        a = _mm_cmplt_epi8(a, c);
        a = _mm_and_si128(d, a);
        mask = _mm_movemask_epi8(a);
    }

    int tz = count_trailing_zeros_u16(mask);
    *offset += inc + tz;

    return true;
}

// Must be safe to assume string has len 16
inline static bool simd_find_int_interrupted(const char *string, char interrupt, u64 *pos) {
    __m128i b = _mm_set1_epi8(47); // ascii 0 - 1
    __m128i c = _mm_set1_epi8(58); // ascii 9 + 1
    __m128i d = _mm_set1_epi8(interrupt);

    __m128i a = _mm_loadu_si128((__m128i*)string);
    __m128i e = _mm_cmpeq_epi8(a, d);
    u16 mask2 = _mm_movemask_epi8(e);

    e = _mm_cmpgt_epi8(a, b);
    a = _mm_cmplt_epi8(a, c);
    u16 mask1 = _mm_movemask_epi8(e);
    u16 mask3 = _mm_movemask_epi8(a);
    mask1 &= mask3;

    u64 inc = 0;
    while(!mask1 && !mask2) {
        inc += 16;
        a = _mm_loadu_si128((__m128i*)(string + inc));
        e = _mm_cmpeq_epi8(a, d);
        mask2 = _mm_movemask_epi8(e);

        e = _mm_cmpgt_epi8(a, b);
        a = _mm_cmplt_epi8(a, c);
        mask1 = _mm_movemask_epi8(e);
        mask3 = _mm_movemask_epi8(a);
        mask1 &= mask3;
    }

    // Intel optimisation manual: fallthrough condition chosen if nothing in btb
    if (mask1 && mask2) {
        int tz1 = count_trailing_zeros_u16(mask1);
        int tz2 = count_trailing_zeros_u16(mask2);
        *pos += count_trailing_zeros_u16(mask1 | mask2) + inc; // avoid a branch
        return tz1 < tz2;
    }

    if (!mask1 && mask2) {
        *pos += count_trailing_zeros_u16(mask2) + inc;
        return false;
    }

    *pos += count_trailing_zeros_u16(mask1) + inc;
    return true;
}

#endif // include guard
