#ifndef SOL_TYPEDEF_H_INCLUDE_GUARD_
#define SOL_TYPEDEF_H_INCLUDE_GUARD_

#include <string.h>
#include <stdint.h>

typedef unsigned int uint;

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

#endif
