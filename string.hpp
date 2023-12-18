#ifndef SOL_STRING_HPP_INCLUDE_GUARD_
#define SOL_STRING_HPP_INCLUDE_GUARD_

#include "tlsf.h"
#include "allocator.hpp"
#include "typedef.h"
#include "assert.h"

struct String {
    u64 len;
    const char *str;
};

inline static String cstr_to_string(const char *cstr) {
    String string;
    string.str = cstr;
    string.len = strlen(cstr);
    return string;
}

enum String_Buffer_Flag_Bits {
    STRING_BUFFER_GROWABLE_BIT = 0x01,
    STRING_BUFFER_TEMP_BIT     = 0x02,
};
typedef u8 String_Buffer_Flags;

struct String_Buffer {
    char *buf;
    u32 len;
    u32 cap;
    String_Buffer_Flags flags;
};

// I am dumb for making the args order different to array... I will fix...
String_Buffer create_string_buffer(u32 size, bool temp = false, bool growable = false);
void          destroy_string_buffer(String_Buffer *buf);

String string_buffer_get_string(String_Buffer *string_buffer, String *string);
String string_buffer_get_string(String_Buffer *string_buffer, const char *cstr);

#endif
