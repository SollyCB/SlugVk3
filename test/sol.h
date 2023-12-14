#ifdef __cplusplus
extern "C" {
#endif

/*
   Define SOL_H_IMPL for the C source.
   Define SOL_BIT_STUFF for bit manipulation builtins and SIMD functions
   Define SOL_MATH for math builtins
*/

#define DEBUG 1

#ifndef SOL_H_INCLUDE_GUARD_
#define SOL_H_INCLUDE_GUARD_

#include <immintrin.h>

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

// type defs/convenience size defines
#define SOL_TYPEDEF
typedef unsigned int uint;
typedef unsigned char uchar;

typedef  uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef   int8_t s8;
typedef  int16_t s16;
typedef  int32_t s32;
typedef  int64_t s64;

typedef u32 bool32;

#define Max_u64 UINT64_MAX
#define Max_u32 UINT32_MAX
#define Max_u16 UINT16_MAX
#define Max_u8  UINT8_MAX
#define Max_s64  INT64_MAX
#define Max_s32  INT32_MAX
#define Max_s16  INT16_MAX
#define Max_s8   INT8_MAX

#define max32_if_true(eval)  (~(Max_u32 + (u32)((eval) != 0)))
#define max8_if_true(eval)   (~(Max_u8  +  (u8)((eval) != 0)))
#define max32_if_false(eval)   (Max_u32 + (u32)((eval) != 0))
#define max8_if_false(eval)    (Max_u8  +  (u8)((eval) != 0))

#define SOL_ALIGN
static inline size_t align(size_t size, size_t alignment) {
  const size_t alignment_mask = alignment - 1;
  return (size + alignment_mask) & ~alignment_mask;
}

                        /* Assert */
#if DEBUG
#if _WIN32

#define assert(x) \
    if (!(x)) {printf("\n    [file: %s, line: %i, fn %s]\n        ** ASSERTION FAILED **: %s\n\n", __FILE__, __LINE__, __FUNCTION__, #x); __debugbreak;}

#else

#define assert(x) \
    if (!(x)) {printf("\n    [file: %s, line: %i, fn %s]\n        ** ASSERTION FAILED **: %s\n\n", __FILE__, __LINE__, __FUNCTION__, #x); asm("int $3");}

#endif // _WIN32 or not

#else
#define assert(x) x
#endif // if DEBUG

                        /* Array */
#ifndef __cplusplus

#define SOL_ARRAY

// Backend
inline static void* fn_new_array(int cap, int width) {
    int *ret = malloc(cap * width + 16);
    ret[0] = width * cap;
    ret[1] = 0;
    ret[2] = width;
    ret += 4;
    return (void*)ret;
}
inline static void* fn_realloc_array(int *array) {
    if (array[0] > array[1] * array[2])
        return array + 4;
    array = realloc(array, array[0] * 2 + 16);
    assert(array);
    array[0] *= 2;
    array += 4;
    return array;
}

// Frontend
#define new_array(cap, type) fn_new_array(cap, sizeof(type))
#define free_array(array) free((int*)array - 4)

#define array_cap(array)   (((int*)array)[-4] / ((int*)array)[-2])
#define array_len(array)   ((int*)array)[-3]
#define array_inc(array)   ((int*)array)[-3]++
#define array_dec(array)   ((int*)array)[-3]--

#define array_add(array, elem) \
    array = fn_realloc_array((int*)array - 4); \
    array[array_len(array)] = elem; \
    array_inc(array)
#define array_pop(array) \
    if (array_len(array) != 0) \
        array_dec(array) \
    else \
        false
#endif // ifndef cpp

                  /* Sorting */
void sort_high_low(int *array, int start, int end) {
    if (start < end) {
        int x = start - 1;
        int tmp;
        for(int i = start; i <= end; ++i) {
            if (array[i] > array[end]) {
                tmp = array[i];
                x++;
                array[i] = array[x];
                array[x] = tmp;
            }
        }
        x++;
        tmp = array[end];
        array[end] = array[x];
        array[x] = tmp;
        sort_high_low(array, start, x - 1);
        sort_high_low(array, x + 1, end);
    }
}
void sort_low_high(int *array, int start, int end) {
    if (start < end) {
        int x = start - 1;
        int tmp;
        for(int i = start; i <= end; ++i) {
            if (array[i] < array[end]) {
                tmp = array[i];
                x++;
                array[i] = array[x];
                array[x] = tmp;
            }
        }
        x++;
        tmp = array[end];
        array[end] = array[x];
        array[x] = tmp;
        sort_low_high(array, start, x - 1);
        sort_low_high(array, x + 1, end);
    }
}
inline static u8* get_seq8(u32 len, bool reversed) {
    u8 *ret = (u8*)malloc(sizeof(u8) * len);
    for(u8 i = 0; i < len; ++i)
        ret[i] = reversed ? (len-1) - i : i;
    return ret;
}
inline static u16* get_seq16(u32 len, bool reversed) {
    u16 *ret = (u16*)malloc(sizeof(u16) * len);
    for(u16 i = 0; i < len; ++i)
        ret[i] = reversed ? (len-1) - i : i;
    return ret;
}
inline static u32* get_seq32(u32 len, bool reversed) {
    u32 *ret = (u32*)malloc(sizeof(u32) * len);
    for(u32 i = 0; i < len; ++i)
        ret[i] = reversed ? (len-1) - i : i;
    return ret;
}
inline static u64* get_seq64(u32 len, bool reversed) {
    u64 *ret = (u64*)malloc(sizeof(u64) * len);
    for(u64 i = 0; i < len; ++i)
        ret[i] = reversed ? (len-1) - i : i;
    return ret;
}
                                        /* Print + Formatter */

// I consider string formatting to be a printer thing, not string thing. 'String' manages memory, it is not
// about making stuff look pretty;

/*
    Options are:
            %c - individual char
            %s - null terminated string
            %i - signed integer
            %u - unsigned integer
            %f - float @Unimplemented

    Options can be escaped by placing '-' before the '%':
            println("-%i") => stdout: -%i

    Integer types can also recognise:
            %h - print as hex
            %b - print as binary
            %z - print leading zeros <- zeros are not printed in decimal, only hex and bin

    primary functions are:
            print(const char *fmt, ...);
            println(const char *fmt, ...); <- print but with a newline
            string_format(char * return_buffer, const char *fmt, va_args args); <- caller must call start on args

    @Note Hex values which are negative are printed as positive values with a negative sign in front of them.

    @Note Valid options can be placed anywhere contiguously with %i or %u.

          The following are equivalent -
               println("My number is: %zhi.", 2);
               println("My number is: %zih.", 2);
               println("My number is: %izh.", 2);

    @Note If you provide an invalid combination, the interruption is considered as marking the token complete,
          as there are no more contiguous valid chars to parse.

               1. println("My number is: %zhhi.", 2); => My number is: hi.
               2. println("My number is: %zifh.", 2); => My number is: 2fh.

          Example 1: 'leading zeros' and 'hex' are set, then 'hex' is set again.

          Example 2: The token is interrupted by the float identifier, which is invalid with leading zeros or
                     an integer type.

*/

#define SOL_PRINT

// 'args' must have been started, ends 'args' itself
void string_format_backend(char *format_buffer, const char *fmt, va_list args);

inline static void string_format(char *format_buffer, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    string_format_backend(format_buffer, fmt, args);
}

inline static void print(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char format_buffer[1024];
    string_format_backend(format_buffer, fmt, args);

    fwrite(format_buffer, 1, strlen(format_buffer), stdout);
}
inline static void println(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char format_buffer[1024];
    string_format_backend(format_buffer, fmt, args);

    int tmp = strlen(format_buffer);
    format_buffer[tmp] = '\n';

    fwrite(format_buffer, 1, tmp + 1, stdout);
}

// @Todo Allocator - BIG TIME, omg... look at how lame this is...
// Tbf for small programs where this header is used, I really am not going to need an allocator lol. Idk what
// I was thinking writing the above comment...
#define malloc_h(sz, a) malloc(sz)
#define free_h(ptr) free(ptr)
#define malloc_t(sz, a) malloc(sz)

/* builtin wrappers (why the fuck do they differ between compilers!!! the world is retarded) */
#define SOL_BUILTIN_WRAPPERS

#ifndef _WIN32

    /* bit manipulation */
static inline int count_trailing_zeros_u16(unsigned short a) {
    // @Note This has to be copied between compiler directives because gcc will not compile
    // tzcnt16 with only one leading undescore. I assume this is a compiler bug, because tzcnt32
    // and 64 both want one leading underscore...
    return (int)__tzcnt_u16(a);
}
static inline int count_trailing_zeros_u32(unsigned int a) {
    return (int)_tzcnt_u32(a);
}
static inline int count_trailing_zeros_u64(u64 a) {
    return (int)_tzcnt_u64(a);
}
static inline int count_leading_zeros_u16(u16 mask) {
    return __builtin_clzs(mask);
}
static inline int count_leading_zeros_u32(u32 mask) {
    return __builtin_clzl(mask);
}
static inline int count_leading_zeros_u64(u64 mask) {
    return __builtin_clzll(mask);
}
static inline int pop_count16(u16 num) {
    u32 tmp = num;
    tmp &= 0x0000ffff; // just to be sure;
    return (int)__builtin_popcount(tmp);
}
static inline int pop_count32(u32 num) {
    return (int)__builtin_popcountl(num);
}
static inline int pop_count64(u64 num) {
    return (int)__builtin_popcountll(num);
}
    /* builtin wrapper math */
static inline float sqrtf(float num) {
    return __builtin_sqrtf(num); // @Todo add for windows (I cannot find builtin for windows)
}
static inline float sinf(float x) {
    return __builtin_sinf(x);
}
static inline float asinf(float x) {
    return __builtin_asinf(x);
}
static inline float cosf(float x) {
    return __builtin_cosf(x);
}
static inline float acosf(float x) {
    return __builtin_acosf(x);
}
#else
static inline int count_trailing_zeros_u16(unsigned short a) {
    return (int)_tzcnt_u16(a);
}
static inline int count_trailing_zeros_u32(unsigned int a) {
    return (int)_tzcnt_u32(a);
}
static inline int count_trailing_zeros_u64(u64 a) {
    return (int)_tzcnt_u64(a);
}
static inline int count_leading_zeros_u16(u16 mask) {
    return __lzcnt16(mask);
}
static inline int count_leading_zeros_u32(u32 mask) {
    return __lzcnt(mask);
}
static inline int count_leading_zeros_u64(u64 mask) {
    return __lzcnt64(mask);
}
static inline int pop_count16(u16 num) {
    return (int)__popcnt16(num);
}
static inline int pop_count32(u32 num) {
    return (int)__popcnt(num);
}
static inline int pop_count64(u64 num) {
    return (int)__popcnt64(num);
}
// math
static inline float sinf(float x) {
    return sinf(x);
}
static inline float asinf(float x) {
    return asinf(x);
}
static inline float cosf(float x) {
    return cosf(x);
}
static inline float acosf(float x) {
    return acosf(x);
}
#endif // WIN32 or not

                        /* String */
typedef struct String {
    u32 len;
    const char *str;
} String;
inline static String cstr_to_string(const char *cstr) {
    String string;
    string.str = cstr;
    string.len = strlen(cstr);
    return string;
}

typedef struct String_Buffer {
    int cap;
    int len;
    char *buf;
} String_Buffer;

inline static String_Buffer new_string_buffer(int size) {
    String_Buffer ret;
    ret.len = 0;
    size = align(size, 16);
    ret.cap = size;
    ret.buf = (char*)malloc(size);
    return ret;
}
inline static void free_string_buffer(String_Buffer *buf) {
    free(buf->buf);
}
inline static String string_buffer_get_string(String_Buffer *buf, String *str) {
    String ret = {};
    ret.len = str->len;

    if (ret.len + 1 + buf->len >= buf->cap) {
        buf->cap *= 2;
        buf->buf = (char*)realloc(buf->buf, buf->cap);
    }

    ret.str = (const char*)(buf->buf + buf->len);

    buf->len += ret.len + 1; // +1 for null byte
    assert(buf->len <= buf->cap && "String Buffer Overflow");
    memcpy((char*)ret.str, str->str, ret.len);
    char *tmp = (char*)ret.str;
    tmp[ret.len] = '\0';

    return ret;
}
inline static String string_buffer_string_from_cstr(String_Buffer *buf, const char *cstr) {
    String ret = {};
    ret.len = strlen(cstr);

    if (ret.len + 1 + buf->len >= buf->cap) {
        buf->cap *= 2;
        buf->buf = (char*)realloc(buf->buf, buf->cap);
    }

    ret.str = (const char*)(buf->buf + buf->len);

    buf->len += ret.len + 1; // +1 for null byte
    assert(buf->len <= buf->cap && "String Buffer Overflow");
    memcpy((char*)ret.str, cstr, ret.len + 1); // copy null byte

    return ret;
}

#ifdef SOL_BIT_STUFF
// SIMD -- @Todo @Note find_flags + update flags function are out of date, having an overlap bug
inline static u32 simd_find_update_flags_u8(u32 count, u8 *flags, u8 find, u8 negate, u8 set, u8 clear, u32 *indices) {
    __m128i a;
    __m128i b = _mm_set1_epi8(find | negate);
    __m128i c = _mm_set1_epi8(find);
    __m128i d;
    __m128i e;
    u16 mask;
    u32 pc;
    u32 tz;
    u32 cnt = 0;
    u32 inc = 0;
    while(inc < count) {
        a = _mm_load_si128((__m128i*)(flags + inc));
        d = _mm_and_si128(a, b);
        d = _mm_cmpeq_epi8(d, c);
        mask = _mm_movemask_epi8(d);

        e = _mm_set1_epi8(clear);
        e = _mm_and_si128(e, d);
        a = _mm_and_si128(a, ~e);
        e = _mm_set1_epi8(set);
        e = _mm_and_si128(e, d);
        a = _mm_or_si128(a, e);
        _mm_store_si128((__m128i*)(flags + inc), a);

        pc = pop_count16(mask);
        for(u32 i = 0; i < pc; ++i) {
            tz = count_trailing_zeros_u16(mask);
            indices[cnt] = tz + inc;
            mask ^= 1 << tz;
            cnt++;
        }
        inc += 16;
    }
    return cnt;
}
inline static u32 simd_find_flags_u8(u32 count, u8 *flags, u8 find, u8 negate, u32 *indices) {
    __m128i a;
    __m128i b = _mm_set1_epi8(find | negate);
    __m128i c = _mm_set1_epi8(find);
    u16 mask;
    u32 pc;
    u32 tz;
    u32 cnt = 0;
    u32 inc = 0;
    while(inc < count) {
        a = _mm_load_si128((__m128i*)(flags + inc));
        a = _mm_and_si128(a, b);
        a = _mm_cmpeq_epi8(a, c);
        mask = _mm_movemask_epi8(a);
        pc = pop_count16(mask);
        for(u32 i = 0; i < pc; ++i) {
            tz = count_trailing_zeros_u16(mask);
            indices[cnt] = tz + inc;
            mask ^= 1 << tz;
            cnt++;
        }
        inc += 16;
    }
    return cnt;
}
#endif // ifdef SOL_BIT_STUFF

#ifndef __cplusplus
                    /* HashMap */
#define SOL_HASH_MAP
/*
                                               ** BEGIN WYHASH **

    -- This is not my (Solomon Brown's) code:
           see wyhash github link at file start and below, wyhash full license before dict implementation

*/

// This is free and unencumbered software released into the public domain under The Unlicense (http://unlicense.org/)
// main repo: https://github.com/wangyi-fudan/wyhash
// author: 王一 Wang Yi <godspeed_china@yeah.net>
// contributors: Reini Urban, Dietrich Epp, Joshua Haberman, Tommy Ettinger, Daniel Lemire, Otmar Ertl, cocowalla, leo-yuriev, Diego Barrios Romero, paulie-g, dumblob, Yann Collet, ivte-ms, hyb, James Z.M. Gao, easyaspi314 (Devin), TheOneric


#ifndef wyhash_final_version_4
#define wyhash_final_version_4

#ifndef WYHASH_CONDOM
//protections that produce different results:
//1: normal valid behavior
//2: extra protection against entropy loss (probability=2^-63), aka. "blind multiplication"
#define WYHASH_CONDOM 1
#endif

#ifndef WYHASH_32BIT_MUM
//0: normal version, slow on 32 bit systems
//1: faster on 32 bit systems but produces different results, incompatible with wy2u0k function
#define WYHASH_32BIT_MUM 0
#endif

//includes
#include <stdint.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_M_X64)
  #include <intrin.h>
  #pragma intrinsic(_umul128)
#endif

//likely and unlikely macros
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
  #define _likely_(x)  __builtin_expect(x,1)
  #define _unlikely_(x)  __builtin_expect(x,0)
#else
  #define _likely_(x) (x)
  #define _unlikely_(x) (x)
#endif

//128bit multiply function
static inline uint64_t _wyrot(uint64_t x) { return (x>>32)|(x<<32); }
static inline void _wymum(uint64_t *A, uint64_t *B){
#if(WYHASH_32BIT_MUM)
  uint64_t hh=(*A>>32)*(*B>>32), hl=(*A>>32)*(uint32_t)*B, lh=(uint32_t)*A*(*B>>32), ll=(uint64_t)(uint32_t)*A*(uint32_t)*B;
  #if(WYHASH_CONDOM>1)
  *A^=_wyrot(hl)^hh; *B^=_wyrot(lh)^ll;
  #else
  *A=_wyrot(hl)^hh; *B=_wyrot(lh)^ll;
  #endif
#elif defined(__SIZEOF_INT128__)
  __uint128_t r=*A; r*=*B;
  #if(WYHASH_CONDOM>1)
  *A^=(uint64_t)r; *B^=(uint64_t)(r>>64);
  #else
  *A=(uint64_t)r; *B=(uint64_t)(r>>64);
  #endif
#elif defined(_MSC_VER) && defined(_M_X64)
  #if(WYHASH_CONDOM>1)
  uint64_t  a,  b;
  a=_umul128(*A,*B,&b);
  *A^=a;  *B^=b;
  #else
  *A=_umul128(*A,*B,B);
  #endif
#else
  uint64_t ha=*A>>32, hb=*B>>32, la=(uint32_t)*A, lb=(uint32_t)*B, hi, lo;
  uint64_t rh=ha*hb, rm0=ha*lb, rm1=hb*la, rl=la*lb, t=rl+(rm0<<32), c=t<rl;
  lo=t+(rm1<<32); c+=lo<t; hi=rh+(rm0>>32)+(rm1>>32)+c;
  #if(WYHASH_CONDOM>1)
  *A^=lo;  *B^=hi;
  #else
  *A=lo;  *B=hi;
  #endif
#endif
}

//multiply and xor mix function, aka MUM
static inline uint64_t _wymix(uint64_t A, uint64_t B){ _wymum(&A,&B); return A^B; }

//endian macros
#ifndef WYHASH_LITTLE_ENDIAN
  #if defined(_WIN32) || defined(__LITTLE_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    #define WYHASH_LITTLE_ENDIAN 1
  #elif defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    #define WYHASH_LITTLE_ENDIAN 0
  #else
    #warning could not determine endianness! Falling back to little endian.
    #define WYHASH_LITTLE_ENDIAN 1
  #endif
#endif

//read functions
#if (WYHASH_LITTLE_ENDIAN)
static inline uint64_t _wyr8(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v;}
static inline uint64_t _wyr4(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v;}
#elif defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
static inline uint64_t _wyr8(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return __builtin_bswap64(v);}
static inline uint64_t _wyr4(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return __builtin_bswap32(v);}
#elif defined(_MSC_VER)
static inline uint64_t _wyr8(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return _byteswap_uint64(v);}
static inline uint64_t _wyr4(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return _byteswap_ulong(v);}
#else
static inline uint64_t _wyr8(const uint8_t *p) {
  uint64_t v; memcpy(&v, p, 8);
  return (((v >> 56) & 0xff)| ((v >> 40) & 0xff00)| ((v >> 24) & 0xff0000)| ((v >>  8) & 0xff000000)| ((v <<  8) & 0xff00000000)| ((v << 24) & 0xff0000000000)| ((v << 40) & 0xff000000000000)| ((v << 56) & 0xff00000000000000));
}
static inline uint64_t _wyr4(const uint8_t *p) {
  uint32_t v; memcpy(&v, p, 4);
  return (((v >> 24) & 0xff)| ((v >>  8) & 0xff00)| ((v <<  8) & 0xff0000)| ((v << 24) & 0xff000000));
}
#endif
static inline uint64_t _wyr3(const uint8_t *p, size_t k) { return (((uint64_t)p[0])<<16)|(((uint64_t)p[k>>1])<<8)|p[k-1];}
//wyhash main function
static inline uint64_t wyhash(const void *key, size_t len, uint64_t seed, const uint64_t *secret){
  const uint8_t *p=(const uint8_t *)key; seed^=_wymix(seed^secret[0],secret[1]);	uint64_t	a,	b;
  if(_likely_(len<=16)){
    if(_likely_(len>=4)){ a=(_wyr4(p)<<32)|_wyr4(p+((len>>3)<<2)); b=(_wyr4(p+len-4)<<32)|_wyr4(p+len-4-((len>>3)<<2)); }
    else if(_likely_(len>0)){ a=_wyr3(p,len); b=0;}
    else a=b=0;
  }
  else{
    size_t i=len;
    if(_unlikely_(i>48)){
      uint64_t see1=seed, see2=seed;
      do{
        seed=_wymix(_wyr8(p)^secret[1],_wyr8(p+8)^seed);
        see1=_wymix(_wyr8(p+16)^secret[2],_wyr8(p+24)^see1);
        see2=_wymix(_wyr8(p+32)^secret[3],_wyr8(p+40)^see2);
        p+=48; i-=48;
      }while(_likely_(i>48));
      seed^=see1^see2;
    }
    while(_unlikely_(i>16)){  seed=_wymix(_wyr8(p)^secret[1],_wyr8(p+8)^seed);  i-=16; p+=16;  }
    a=_wyr8(p+i-16);  b=_wyr8(p+i-8);
  }
  a^=secret[1]; b^=seed;  _wymum(&a,&b);
  return  _wymix(a^secret[0]^len,b^secret[1]);
}

//the default secret parameters
static const uint64_t _wyp[4] = {0xa0761d6478bd642full, 0xe7037ed1a0b428dbull, 0x8ebc6af09c88c6e3ull, 0x589965cc75374cc3ull};

//a useful 64bit-64bit mix function to produce deterministic pseudo random numbers that can pass BigCrush and PractRand
static inline uint64_t wyhash64(uint64_t A, uint64_t B){ A^=0xa0761d6478bd642full; B^=0xe7037ed1a0b428dbull; _wymum(&A,&B); return _wymix(A^0xa0761d6478bd642full,B^0xe7037ed1a0b428dbull);}

//The wyrand PRNG that pass BigCrush and PractRand
static inline uint64_t wyrand(uint64_t *seed){ *seed+=0xa0761d6478bd642full; return _wymix(*seed,*seed^0xe7037ed1a0b428dbull);}

//convert any 64 bit pseudo random numbers to uniform distribution [0,1). It can be combined with wyrand, wyhash64 or wyhash.
static inline double wy2u01(uint64_t r){ const double _wynorm=1.0/(1ull<<52); return (r>>12)*_wynorm;}

//convert any 64 bit pseudo random numbers to APPROXIMATE Gaussian distribution. It can be combined with wyrand, wyhash64 or wyhash.
static inline double wy2gau(uint64_t r){ const double _wynorm=1.0/(1ull<<20); return ((r&0x1fffff)+((r>>21)&0x1fffff)+((r>>42)&0x1fffff))*_wynorm-3.0;}

#ifdef	WYTRNG
#include <sys/time.h>
//The wytrand true random number generator, passed BigCrush.
static inline uint64_t wytrand(uint64_t *seed){
	struct	timeval	t;	gettimeofday(&t,0);
	uint64_t	teed=(((uint64_t)t.tv_sec)<<32)|t.tv_usec;
	teed=_wymix(teed^_wyp[0],*seed^_wyp[1]);
	*seed=_wymix(teed^_wyp[0],_wyp[2]);
	return _wymix(*seed,*seed^_wyp[3]);
}
#endif

#if(!WYHASH_32BIT_MUM)
//fast range integer random number generation on [0,k) credit to Daniel Lemire. May not work when WYHASH_32BIT_MUM=1. It can be combined with wyrand, wyhash64 or wyhash.
static inline uint64_t wy2u0k(uint64_t r, uint64_t k){ _wymum(&r,&k); return k; }
#endif

//make your own secret
static inline void make_secret(uint64_t seed, uint64_t *secret){
  uint8_t c[] = {15, 23, 27, 29, 30, 39, 43, 45, 46, 51, 53, 54, 57, 58, 60, 71, 75, 77, 78, 83, 85, 86, 89, 90, 92, 99, 101, 102, 105, 106, 108, 113, 114, 116, 120, 135, 139, 141, 142, 147, 149, 150, 153, 154, 156, 163, 165, 166, 169, 170, 172, 177, 178, 180, 184, 195, 197, 198, 201, 202, 204, 209, 210, 212, 216, 225, 226, 228, 232, 240 };
  for(size_t i=0;i<4;i++){
    uint8_t ok;
    do{
      ok=1; secret[i]=0;
      for(size_t j=0;j<64;j+=8) secret[i]|=((uint64_t)c[wyrand(&seed)%sizeof(c)])<<j;
      if(secret[i]%2==0){ ok=0; continue; }
      for(size_t j=0;j<i;j++) {
#if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__clang__)
        if(__builtin_popcountll(secret[j]^secret[i])!=32){ ok=0; break; }
#elif defined(_MSC_VER) && defined(_M_X64)
        if(_mm_popcnt_u64(secret[j]^secret[i])!=32){ ok=0; break; }
#else
        //manual popcount
        uint64_t x = secret[j]^secret[i];
        x -= (x >> 1) & 0x5555555555555555;
        x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
        x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0f;
        x = (x * 0x0101010101010101) >> 56;
        if(x!=32){ ok=0; break; }
#endif
      }
    }while(!ok);
  }
}

#endif
                             /* WYHASH LICENCE */
/* The Unlicense
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/

                        /* BEGIN HASH MAP HEADER */
typedef struct Hash_Map {
    int cap;
    int remaining;
    int kv_stride;
    u8 *data;
} Hash_Map;

#define HASH_MAP_FULL        0b01111111
#define HASH_MAP_EMPTY       0b11111111
#define HASH_MAP_DELETED     0b10000000
#define HASH_MAP_GROUP_WIDTH 16

                        /* Backend Functions*/
bool  fn_hash_map_insert_hash(Hash_Map *map, u64 hash, void *elem, int elem_width);
void  fn_hash_map_if_full(Hash_Map *map, int elem_width);
void* fn_hash_map_find_hash(Hash_Map *map, u64 hash);
void* fn_hash_map_delete_hash(Hash_Map *map, u64 hash);

                        /* Frontend Functions */
inline static u64 get_hash(int byte_len, void *bytes) {
    return wyhash(bytes, byte_len, 0, _wyp);
}

inline static Hash_Map fn_new_hash_map(int cap, int elem_width) {
    Hash_Map ret  = {};
    ret.cap       = align(cap, 16);
    ret.remaining = ((cap + 1) / 8) * 7;
    ret.kv_stride = align(8 + elem_width, 16);
    ret.data      = malloc(ret.cap + ret.cap * ret.kv_stride);
    memset(ret.data, HASH_MAP_EMPTY, ret.cap);
    return ret;
}
 static void fn_free_hash_map(Hash_Map *map) {
    free(map->data);
}

inline static bool fn_hash_map_insert(Hash_Map *map, int byte_len, void *key, void *elem, int elem_width) {
    if (map->remaining == 0)
        fn_hash_map_if_full(map, elem_width);

    u64 hash = wyhash(key, byte_len, 0, _wyp);
    return fn_hash_map_insert_hash(map, hash, elem, elem_width);
}
inline static bool fn_hash_map_insert_str(Hash_Map *map, const char *key, void *elem, int elem_width) {
    if (map->remaining == 0)
        fn_hash_map_if_full(map, elem_width);

    u64 hash = wyhash(key, strlen(key), 0, _wyp);
    return fn_hash_map_insert_hash(map, hash, elem, elem_width);
}

inline static void* fn_hash_map_find(Hash_Map *map, int byte_len, void *bytes) {
    u64 hash = wyhash(bytes, byte_len, 0, _wyp);
    return fn_hash_map_find_hash(map, hash);
}
inline static void* fn_hash_map_find_str(Hash_Map *map, const char* key) {
    u64 hash = wyhash(key, strlen(key), 0, _wyp);
    return fn_hash_map_find_hash(map, hash);
}

inline static void* fn_hash_map_delete(Hash_Map *map, int byte_len, void *bytes) {
    u64 hash = wyhash(bytes, byte_len, 0, _wyp);
    return fn_hash_map_delete_hash(map, hash);
}
inline static void* fn_hash_map_delete_str(Hash_Map *map, const char* key) {
    u64 hash = wyhash(key, strlen(key), 0, _wyp);
    return fn_hash_map_delete_hash(map, hash);
}

                        /* Frontend Macros */
#define new_hash_map(cap, type) fn_new_hash_map(cap, sizeof(type))
#define free_hash_map(p_map) fn_free_hash_map(p_map)

#define hash_map_insert(p_map, p_key, p_value) fn_hash_map_insert(p_map, sizeof(*(p_key)), p_key, p_value, sizeof(*p_value))
#define hash_map_insert_str(p_map, str_key, p_value) fn_hash_map_insert_str(p_map, str_key, p_value, sizeof(*p_value))

#define hash_map_find(p_map, p_key) fn_hash_map_find(p_map, sizeof(*(p_key)), p_key)
#define hash_map_find_str(p_map, str_key) fn_hash_map_find_str(p_map, str_key)

#define hash_map_delete(p_map, p_key) fn_hash_map_delete(p_map, sizeof(*(key)), p_key)
#define hash_map_delete_str(p_map, str_key) fn_hash_map_delete_str(p_map, str_key)
                    /* End Hash Map Header */
#endif // ifndef cpp

inline static int ascii_to_int(const char *data, u64 *offset) {
    u64 inc = 0;
    while(data[inc] - '0' == -1) // I dont remember the point of this loop
        inc++;

    int accum = 0;
    while(data[inc] >= '0' && data[inc] <= '9') {
        accum *= 10;
        accum += data[inc] - '0';
        inc++;
    }
    *offset += inc;
    return accum;
}
inline static u64 ascii_to_u64(const char *data, u64 *offset) {
    u64 inc = 0;
    while(data[inc] - '0' == -1) // I dont remember the point of this loop
        inc++;

    u64 accum = 0;
    while(data[inc] >= '0' && data[inc] <= '9') {
        accum *= 10;
        accum += data[inc] - '0';
        inc++;
    }
    *offset += inc;
    return accum;
}

                /* File */
inline static char* file_read_char(const char *file_name, u64 *size) {

    FILE *f = fopen(file_name, "r");

    if (!f) {
        println("Failed to read file %c", file_name);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *ret = (char*)malloc(*size + 16);
    fread(ret, 1, *size, f);

    fclose(f);
    return ret;
}
inline static u64 file_write_char(const char *file_name, char *buffer, u64 count) {
    FILE *f = fopen(file_name, "w");
    u64 ret = fwrite(buffer, 1, count, f);
    fclose(f);
    return ret;
}
inline static u8* file_read_bytes(const char *file_name, u64 *size) {

    FILE *f = fopen(file_name, "rb");

    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8 *ret = (u8*)malloc(*size);
    fread(ret, 1, *size, f);

    fclose(f);
    return ret;
}
inline static u64 file_write_bytes(const char *file_name, u8 *buffer, u64 count) {
    FILE *f = fopen(file_name, "wb");
    u64 ret = fwrite(buffer, 1, count, f);
    fclose(f);
    return ret;
}

#ifdef __cplusplus
} // extern "C"
#endif

#ifdef SOL_H_IMPL

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PRINT_HEX_BIT    = 0x00000001,
    PRINT_BIN_BIT    = 0x00000002,
    PRINT_LZ_BIT     = 0x00000004,
    PRINT_UINT_BIT   = 0x00000008,
    PRINT_SINT_BIT   = 0x00000010,
    PRINT_FLOAT_BIT  = 0x00000020,
    PRINT_STRING_BIT = 0x00000040,
    PRINT_CHAR_BIT   = 0x00000080,
} Print_Flag_Bits;

typedef u32 Print_Flags;

typedef enum {
    PRINT_INT_SIZE_NIL = 0,
    PRINT_INT_SIZE_8   = 8,
    PRINT_INT_SIZE_16  = 16,
    PRINT_INT_SIZE_32  = 32,
    PRINT_INT_SIZE_64  = 64,
} Print_Int_Size;

typedef struct {
    Print_Flags flags;
    Print_Int_Size int_size;
} Print_Config;

inline static void print_parse_int_dec(u64 i, int *len, char *buf) {
    if (i == 0) {
        buf[*len] = '0';
        *len += 1;
        return ;
    }

    while(i > 0) {
        buf[*len] = (i % 10) + '0';
        *len += 1;
        i /= 10;
    }
}
inline static void print_parse_int_hex(u64 i, int *len, char *buf) {
    if (i == 0) {
        buf[*len] = '0';
        *len += 1;
        return ;
    }

    char hex[] = {'a','b','c','d','e','f'};
    u64 mask = 0x000000000000000f;
    int tmp;
    while(i > 0) {
        tmp = i & mask;

        if (tmp < 10)
            buf[*len] = tmp + '0';
        else
            buf[*len] = hex[tmp % 10];

        *len += 1;
        i >>= 4;
    }
}
inline static void print_parse_int_bin(u64 i, int *len, char *buf) {
    if (i == 0) {
        buf[*len] = '0';
        *len += 1;
        return ;
    }

    u64 mask = 0x0000000000000001;
    while(i > 0) {
        buf[*len] = (i & mask) + '0';
        *len += 1;
        i >>= 1;
    }
}

static void print_parse_int(Print_Config *config, s64 i, int *buf_pos, char *print_buffer) {
    int max_zeros;

    switch(config->int_size) { // hex zeros
    case PRINT_INT_SIZE_8:
        max_zeros = 2;
        break;
    case PRINT_INT_SIZE_16:
        max_zeros = 4;
        break;
    case PRINT_INT_SIZE_32:
        max_zeros = 8;
        break;
    case PRINT_INT_SIZE_64:
        max_zeros = 16;
        break;
    default:
        max_zeros = 16;
        break;
    }

    char int_buf[96];
    int int_pos = 0;
    if (config->flags & PRINT_HEX_BIT) {
        print_parse_int_hex(i, &int_pos, int_buf);

        print_buffer[*buf_pos + 0] = '0';
        print_buffer[*buf_pos + 1] = 'x';

        *buf_pos += 2;
    } else if (config->flags & PRINT_BIN_BIT) {
        print_parse_int_bin(i, &int_pos, int_buf);

        print_buffer[*buf_pos + 0] = '0';
        print_buffer[*buf_pos + 1] = 'b';

        max_zeros *= 4;
        *buf_pos += 2;
    } else {
        print_parse_int_dec(i, &int_pos, int_buf);
        max_zeros = 0;
    }

    if (config->flags & PRINT_LZ_BIT) {
        int zeros;
        if (i > 0) {
            zeros = count_leading_zeros_u64(i);
            zeros &= max_zeros - 1;
        } else {
            zeros = max_zeros;
        }
        for(int j = 0; j < zeros; ++j) {
            int_buf[int_pos] = '0';
            int_pos++;
        }
    }

    // reverse int
    for(int j = 0; j < int_pos; ++j) {
        print_buffer[*buf_pos] = int_buf[(int_pos - 1) - j];
        *buf_pos += 1;
    }
}
inline static void print_parse_signed_int(Print_Config *config, s64 i, int *buf_pos, char *print_buffer) {
    if (i < 0) {
        print_buffer[*buf_pos] = '-';
        *buf_pos += 1;
        i = -i;
    }
    print_parse_int(config, (u64)i, buf_pos, print_buffer);
}
inline static void print_parse_unsigned_int(Print_Config *config, s64 i, int *buf_pos, char *print_buffer) {
    print_parse_int(config, i, buf_pos, print_buffer);
}
typedef enum {
    PRINT_VALUE_STRING,
    PRINT_VALUE_CHAR,
    PRINT_VALUE_UINT,
    PRINT_VALUE_SINT,
    PRINT_VALUE_FLOAT,
    PRINT_VALUE_HEX,
    PRINT_VALUE_BIN,
    PRINT_VALUE_LZ,
} Print_Value;
inline static bool print_check_config_flags(Print_Flags flags, Print_Value value) {
    switch(value) {
    case PRINT_VALUE_STRING:
    case PRINT_VALUE_CHAR:
    case PRINT_VALUE_FLOAT:
        return flags == 0;
    case PRINT_VALUE_SINT:
    case PRINT_VALUE_UINT:
        flags &= ~(PRINT_HEX_BIT | PRINT_BIN_BIT | PRINT_LZ_BIT);
        return flags == 0;
    case PRINT_VALUE_HEX:
        flags &= PRINT_STRING_BIT | PRINT_FLOAT_BIT | PRINT_CHAR_BIT | PRINT_HEX_BIT;
        return flags == 0;
    case PRINT_VALUE_BIN:
        flags &= PRINT_STRING_BIT | PRINT_FLOAT_BIT | PRINT_CHAR_BIT | PRINT_BIN_BIT;
        return flags == 0;
    case PRINT_VALUE_LZ:
        flags &= PRINT_STRING_BIT | PRINT_FLOAT_BIT | PRINT_CHAR_BIT | PRINT_LZ_BIT;
        return flags == 0;
    default:
        assert(false && "Invalid Flag Check");
        return false;
    }
}

void string_format_backend(char *format_buffer, const char *fmt, va_list args) { // args must have been started
    int buf_pos = 0;
    char c;
    char *s;
    u64 u;
    s64 i;
    double f;

    Print_Config config = {};
    bool is_ident    = false;
    bool parse_sint  = false;
    bool parse_uint  = false;
    bool parse_float = false;

    int tmp;
    char last_char = 0;
    for(int j = 0; fmt[j] != 0; ++j) {

        if (fmt[j] != '%' && fmt[j] != '-') {
            format_buffer[buf_pos] = fmt[j];
            buf_pos++;
            continue;
        } else if (fmt[j] == '-') {
            if (fmt[j+1] == '%') {
                format_buffer[buf_pos + 0] = fmt[j + 0];
                format_buffer[buf_pos + 1] = fmt[j + 1];
                buf_pos += 2;
                j++;
            } else {
                format_buffer[buf_pos] = fmt[j];
                buf_pos++;
            }
            continue;
        } else {
            is_ident = true;

            config   = (Print_Config){};
            parse_sint  = false;
            parse_uint  = false;
            parse_float = false;

            j++;
            while(is_ident && fmt[j] != 0) {
                switch(fmt[j]) {
                case 0:
                    goto not_ident;
                case 'f':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_FLOAT)) {
                        goto not_ident;
                    }
                    // @Unimplemented
                    f = va_arg(args, double);
                    j++;
                    goto not_ident;
                case 'c':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_CHAR)) {
                        goto not_ident;
                    }
                    c = (char)va_arg(args, int);
                    format_buffer[buf_pos] = c;
                    buf_pos++;

                    j++;
                    goto not_ident;
                case 's':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_STRING)) {
                        goto not_ident;
                    }
                    s = va_arg(args, char*);
                    tmp = strlen(s);
                    memcpy(format_buffer + buf_pos, s, tmp);
                    buf_pos += tmp;

                    j++;
                    goto not_ident;
                case 'h':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_HEX)) {
                        j++;
                        goto not_ident;
                    } else {
                        config.flags |= PRINT_HEX_BIT;
                    }
                    break;
                case 'b':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_BIN)) {
                        j++;
                        goto not_ident;
                    } else {
                        config.flags |= PRINT_BIN_BIT;
                    }
                    break;
                case 'z':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_LZ)) {
                        j++;
                        goto not_ident;
                    } else {
                        config.flags |= PRINT_LZ_BIT;
                    }
                    break;
                case 'i':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_SINT)) {
                        j++;
                        goto not_ident;
                    } else {
                        i = va_arg(args, s64);
                        parse_sint = true;
                    }
                    break;
                case 'u':
                    if (!print_check_config_flags(config.flags, PRINT_VALUE_UINT)) {
                        j++;
                        goto not_ident;
                    } else {
                        u = va_arg(args, u64);
                        parse_uint = true;
                    }
                    break;
                default:
                    goto not_ident;
                }
                j++;
            }

            not_ident:
            j--; // ensure next loop iteration sees non ident value

            if (parse_uint) {
                print_parse_unsigned_int(&config, u, &buf_pos, format_buffer);
                parse_uint = false;
            } else if (parse_sint) {
                print_parse_signed_int(&config, i, &buf_pos, format_buffer);
                parse_sint = false;
            }
        }
    }

    va_end(args);
    format_buffer[buf_pos] = 0;
}


#ifndef __cplusplus
                    /* BEGIN HASH MAP IMPLEMENTATION */
bool fn_hash_map_insert_hash(Hash_Map *map, u64 hash, void *elem, int elem_width) {
    int g_idx = (hash & (map->cap - 1));
    g_idx    -= g_idx & 15;

    u8  *data   = map->data;
    int  cap    = map->cap;
    int  stride = map->kv_stride;

    int  tz;
    u16  mask;
    u64 *phash;

    __m128i a;
    int inc = 0;
    while(inc < cap) {
        a    = _mm_load_si128((__m128i*)(data + g_idx));
        mask = _mm_movemask_epi8(a);

        if (!mask) {
            inc   += 16;
            g_idx += inc;
            g_idx &= cap - 1;
            continue;
        } else {
            tz = count_trailing_zeros_u16(mask);

            u8 top7 = (hash >> 57) & HASH_MAP_FULL;
            data[g_idx + tz] = 0x0 | top7;

            phash  = (u64*)(data + cap + (stride * (tz + g_idx)));
           *phash  =  hash;
            memcpy(data + cap + (stride * (tz + g_idx)) + 8, elem, elem_width);

            map->remaining -= 1;
            return true;
        }
    }
    return false;
}
void fn_hash_map_if_full(Hash_Map *map, int elem_width) {
    assert(map->cap * 2 > map->cap && "mul overflow");

    u8 *old_data = map->data;
    int old_cap  = map->cap;

    map->cap      *= 2;
    map->data      = malloc(map->cap + map->cap * map->kv_stride);
    map->remaining = ((map->cap + 1) / 8) * 7;

    memset(map->data, HASH_MAP_EMPTY, map->cap);

    int stride = map->kv_stride;

    int  pc;
    int  tz;
    u16  mask;
    u64 *phash;

    __m128i a;
    for(int i = 0; i < old_cap; i += 16) {
        a    = _mm_load_si128((__m128i*)(old_data + i));
        mask = ~(_mm_movemask_epi8(a));

        pc = pop_count16(mask);
        for(int j = 0; j < pc; ++j) {
            tz    = count_trailing_zeros_u16(mask);
            mask ^= 1 << tz;

            phash = (u64*)(old_data + old_cap + (stride * (tz + i)));
            assert(fn_hash_map_insert_hash(map, *phash, (u8*)phash + 8, elem_width) && "hash map grow failure");
        }
    }
    free(old_data);
}
void* fn_hash_map_find_hash(Hash_Map *map, u64 hash) {
    u8 top7   = (hash >> 57) & HASH_MAP_FULL;
    int g_idx = hash & (map->cap - 1);
    g_idx -= g_idx & 15;

    u8 *data   = map->data;
    int stride = map->kv_stride;
    int cap    = map->cap;

    int  pc;
    int  tz;
    u16  mask;
    u64 *phash;

    __m128i a;
    __m128i b = _mm_set1_epi8(top7);

    int inc = 0;
    while(inc < cap) {
        a    = _mm_load_si128((__m128i*)(data + g_idx));
        a    = _mm_cmpeq_epi8(a, b);

        mask = _mm_movemask_epi8(a);
        pc   = pop_count16(mask);

        for(int i = 0; i < pc; ++i) {
            tz    = count_trailing_zeros_u16(mask);
            mask ^= 1 << tz;
            phash = (u64*)(data + cap + (stride * (tz + g_idx)));
            if (*phash == hash)
                return (u8*)phash + 8;
        }
        g_idx += 16;
        g_idx &= cap - 1;
        inc   += 16;
    }
    return NULL;
}
void* fn_hash_map_delete_hash(Hash_Map *map, u64 hash) {
    u8 top7   = (hash >> 57) & HASH_MAP_FULL;
    int g_idx = hash & (map->cap - 1);
    g_idx    -= g_idx & 15;

    __m128i a;
    __m128i b = _mm_set1_epi8(top7);

    u8 *data   = map->data;
    int stride = map->kv_stride;
    int cap    = map->cap;

    int  pc;
    int  tz;
    u16  mask;
    u64 *phash;

    int inc = 0;
    while(inc < cap) {
        a    = _mm_load_si128((__m128i*)(data + g_idx));
        a    = _mm_cmpeq_epi8(a, b);

        mask = _mm_movemask_epi8(a);
        pc   = pop_count16(mask);

        for(int i = 0; i < pc; ++i) {
            tz    = count_trailing_zeros_u16(mask);
            mask ^= 1 << tz;
            phash = (u64*)(data + cap + (stride * (tz + g_idx)));
            if (*phash == hash) {
                data[g_idx + tz] = HASH_MAP_DELETED;
                return (u8*)phash + 8;
            }
        }
        g_idx += 16;
        g_idx &= cap - 1;
        inc   += 16;
    }
    return NULL;
}

#endif // implementation guard
#endif // ifndef cpp

#endif // include guard

#ifdef __cplusplus
} // extern "C"
#endif
