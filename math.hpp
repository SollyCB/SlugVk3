#ifndef SOL_MATH_HPP_INCLUDE_GUARD_
#define SOL_MATH_HPP_INCLUDE_GUARD_

#include "builtin_wrappers.h"
#include <xmmintrin.h>

static inline u32 max_clamp32(u32 max, u32 actual) {
    if (actual > max)
        return max;
    else
        return actual;
}
static inline float rad(float deg) {
    // 1/180 * pi
    return 0.0055555555 * 3.1415927 * deg;
}
static inline u64 pow(u64 num, u32 exp) {
    u64 accum = 1;
    for(u32 i = 0; i < exp; ++i) {
        accum *= num;
    }
    return accum;
}

struct Vec4 {
    float x;
    float y;
    float z;
    float w;
};
inline static Vec4 get_vec4(float x, float y, float z, float w) {
    return {x, y, z, w};
}

struct Vec3 {
    float x;
    float y;
    float z;
};
inline static Vec3 get_vec3(float x, float y, float z) {
    return {x, y, z};
}
inline static Vec3 add_vec3(Vec3 *a, Vec3 *b) {
    return {
        a->x + b->x,
        a->y + b->y,
        a->z + b->z,
    };
}
inline static Vec3 scalar_mul_vec3(Vec3 *a, float scalar) {
    return {
        a->x * scalar,
        a->y * scalar,
        a->z * scalar,
    };
}
inline static float magnitude_vec3(Vec3 a) {
    alignas(16) float vec[4] = {a.x, a.y, a.z, 0};
    __m128 b = _mm_load_ps(vec);
    b = _mm_mul_ps(b, b);
    float *f = (float*)&b;
    float sum = f[0] + f[1] + f[2];
    return __builtin_sqrtf(sum);
}
inline static void normalize_vec3(Vec3 *a) {
    float mag = magnitude_vec3(*a);
    alignas(16) float vec[4] = {a->x, a->y, a->z, 1};
    alignas(16) float len[4] = {mag, mag, mag, 1};

    __m128 b = _mm_load_ps(vec);
    __m128 c = _mm_load_ps(len);
    b = _mm_div_ps(b, c);

    float *ret = (float*)&b;
    *a = {ret[0], ret[1], ret[2]};
}
inline static float sum_vec3(Vec3 vec3) {
     return vec3.x + vec3.y + vec3.z;
}
inline static Vec3 add_vec3(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.x + b.y};
}
inline static Vec3 sub_vec3(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.x - b.y};
}
inline static float dot_vec3(Vec3 a, Vec3 b) {
    return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}
inline static Vec3 cross_vec3(Vec3 *a, Vec3 *b) {
    return {
        (a->y * b->z) - (a->z - b->y),
        (a->z * b->x) - (a->x * b->z),
        (a->x * b->y) - (a->y * b->x),
    };
}

struct Vec2 {
    float x;
    float y;
};
inline static Vec2 get_vec2(float x, float y) {
    return {x, y};
}

struct Mat4 {
    Vec4 row0;
    Vec4 row1;
    Vec4 row2;
    Vec4 row3;
};
inline static Vec4 mul_mat4_vec4(Mat4 mat, Vec4 vec) {
    Vec4 ret;
    ret.x = vec.x * mat.row0.x + vec.y * mat.row0.y + vec.z * mat.row0.z + vec.w * mat.row0.w;
    ret.y = vec.x * mat.row1.x + vec.y * mat.row1.y + vec.z * mat.row1.z + vec.w * mat.row1.w;
    ret.z = vec.x * mat.row2.x + vec.y * mat.row2.y + vec.z * mat.row2.z + vec.w * mat.row2.w;
    ret.w = vec.x * mat.row3.x + vec.y * mat.row3.y + vec.z * mat.row3.z + vec.w * mat.row3.w;
    return ret;
}
inline static Mat4 mul_mat4(Mat4 a, Mat4 b)
{
    alignas(16) float c[4];
    alignas(16) float d[4];
    float *tmp;
    Mat4 ret;
    __m128 f;
    __m128 e;

    // Row 0
    c[0] = a.row0.x;
    c[1] = a.row0.y;
    c[2] = a.row0.z;
    c[3] = a.row0.w;

    d[0] = b.row0.x;
    d[1] = b.row1.x;
    d[2] = b.row2.x;
    d[3] = b.row3.x;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row0.x = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row0.x;
    c[1] = a.row0.y;
    c[2] = a.row0.z;
    c[3] = a.row0.w;

    d[0] = b.row0.y;
    d[1] = b.row1.y;
    d[2] = b.row2.y;
    d[3] = b.row3.y;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row0.y = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row0.x;
    c[1] = a.row0.y;
    c[2] = a.row0.z;
    c[3] = a.row0.w;

    d[0] = b.row0.z;
    d[1] = b.row1.z;
    d[2] = b.row2.z;
    d[3] = b.row3.z;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row0.z = tmp[0] + tmp[1] + tmp[2], tmp[3];

    c[0] = a.row0.x;
    c[1] = a.row0.y;
    c[2] = a.row0.z;
    c[3] = a.row0.w;

    d[0] = b.row0.w;
    d[1] = b.row1.w;
    d[2] = b.row2.w;
    d[3] = b.row3.w;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row0.w = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    // Row 1
    c[0] = a.row1.x;
    c[1] = a.row1.y;
    c[2] = a.row1.z;
    c[3] = a.row1.w;

    d[0] = b.row0.x;
    d[1] = b.row1.x;
    d[2] = b.row2.x;
    d[3] = b.row3.x;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row1.x = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row1.x;
    c[1] = a.row1.y;
    c[2] = a.row1.z;
    c[3] = a.row1.w;

    d[0] = b.row0.y;
    d[1] = b.row1.y;
    d[2] = b.row2.y;
    d[3] = b.row3.y;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row1.y = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row1.x;
    c[1] = a.row1.y;
    c[2] = a.row1.z;
    c[3] = a.row1.w;

    d[0] = b.row0.z;
    d[1] = b.row1.z;
    d[2] = b.row2.z;
    d[3] = b.row3.z;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row1.z = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row1.x;
    c[1] = a.row1.y;
    c[2] = a.row1.z;
    c[3] = a.row1.w;

    d[0] = b.row0.w;
    d[1] = b.row1.w;
    d[2] = b.row2.w;
    d[3] = b.row3.w;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row1.w = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    // Row 2
    c[0] = a.row2.x;
    c[1] = a.row2.y;
    c[2] = a.row2.z;
    c[3] = a.row2.w;

    d[0] = b.row0.x;
    d[1] = b.row1.x;
    d[2] = b.row2.x;
    d[3] = b.row3.x;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row2.x = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row2.x;
    c[1] = a.row2.y;
    c[2] = a.row2.z;
    c[3] = a.row2.w;

    d[0] = b.row0.y;
    d[1] = b.row1.y;
    d[2] = b.row2.y;
    d[3] = b.row3.y;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row2.y = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row2.x;
    c[1] = a.row2.y;
    c[2] = a.row2.z;
    c[3] = a.row2.w;

    d[0] = b.row0.z;
    d[1] = b.row1.z;
    d[2] = b.row2.z;
    d[3] = b.row3.z;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row2.z = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row2.x;
    c[1] = a.row2.y;
    c[2] = a.row2.z;
    c[3] = a.row2.w;

    d[0] = b.row0.w;
    d[1] = b.row1.w;
    d[2] = b.row2.w;
    d[3] = b.row3.w;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row2.w = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    // Row 3
    c[0] = a.row3.x;
    c[1] = a.row3.y;
    c[2] = a.row3.z;
    c[3] = a.row3.w;

    d[0] = b.row0.x;
    d[1] = b.row1.x;
    d[2] = b.row2.x;
    d[3] = b.row3.x;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row3.x = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row3.x;
    c[1] = a.row3.y;
    c[2] = a.row3.z;
    c[3] = a.row3.w;

    d[0] = b.row0.y;
    d[1] = b.row1.y;
    d[2] = b.row2.y;
    d[3] = b.row3.y;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row3.y = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row3.x;
    c[1] = a.row3.y;
    c[2] = a.row3.z;
    c[3] = a.row3.w;

    d[0] = b.row0.z;
    d[1] = b.row1.z;
    d[2] = b.row2.z;
    d[3] = b.row3.z;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row3.z = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    c[0] = a.row3.x;
    c[1] = a.row3.y;
    c[2] = a.row3.z;
    c[3] = a.row3.w;

    d[0] = b.row0.w;
    d[1] = b.row1.w;
    d[2] = b.row2.w;
    d[3] = b.row3.w;

    e = _mm_load_ps(c);
    f = _mm_load_ps(d);
    e = _mm_mul_ps(e, f);
    tmp = (float*)&e;
    ret.row3.w = tmp[0] + tmp[1] + tmp[2] + tmp[3];

    return ret;
}
inline static Mat4 look_at(Vec3 *right, Vec3 *up, Vec3 *dir, Vec3 *pos) {
    Mat4 mat = {
        right->x, right->y, right->z, 0,
        up->x   , up->y   , up->z   , 0,
        dir->x  , dir->y  , dir->z  , 0,
        0       , 0       , 0       , 1,
    };
    Mat4 trans = {
        0, 0, 0, -pos->x,
        0, 0, 0, -pos->y,
        0, 0, 0, -pos->z,
        0, 0, 0, 1,
    };

    return mul_mat4(mat, trans);
}

#endif // include guard
