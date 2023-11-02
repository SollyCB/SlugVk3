#ifndef SOL_STRING_HPP_INCLUDE_GUARD_
#define SOL_STRING_HPP_INCLUDE_GUARD_

#include "tlsf.h"
#include "allocator.hpp"
#include "typedef.h"
#include "assert.h"

// @DebugVsRelease @CompileWarning Be careful using strings in systems where consistent size matters:
// A string compiled in debug mode will be bigger than one compiled in release mode...
// (Idk if this is a dumb optimisation and will just cause more problems than it is worth...)

struct String {
    u32 len;
    const char *str;
};
inline static String cstr_to_string(const char *cstr) {
    String string;
    string.str = cstr;
    string.len = strlen(cstr);
    return string;
}

struct String_Buffer {
    char *buf;
    u32 len;
    u32 cap;
};
inline static String_Buffer create_string_buffer(u32 size, bool temp) {
    String_Buffer ret;
    ret.len = 0;
    size = align(size, 16); // Just in case of some simd
    ret.cap = size;
    if (temp)
        ret.buf = (char*)malloc_t(size, 16);
    else
        ret.buf = (char*)malloc_h(size, 16); // static predict heap
    return ret;
}
inline static void destroy_string_buffer(String_Buffer *buf) {
    free_h(buf->buf);
    *buf = {};
}
inline static String string_buffer_get_string(String_Buffer *buf, String *str) {
    String ret = {};
    ret.len = str->len;
    ret.str = (const char*)(buf->buf + buf->len);

    buf->len += ret.len + 1; // +1 for null byte
    ASSERT(buf->len <= ret.cap, "String Buffer Overflow");
    memcpy((char*)ret.str, str->str, ret.len); // copy null byte
    ret.str[ret.len] = '\0';

    return ret;
}
inline static String string_buffer_get_string(String_Buffer *buf, const char *cstr) {
    String ret = {};
    ret.len = strlen(cstr);
    ret.str = (const char*)(buf->buf + buf->len);

    buf->len += ret.len + 1; // +1 for null byte
    ASSERT(buf->len <= ret.cap, "String Buffer Overflow");
    memcpy((char*)ret.str, cstr, ret.len + 1); // copy null byte

    return ret;
}

    /* Old Interface (still used by test.hpp) */
struct Heap_String_Buffer {
    u32 len;
    char *data;
#if DEBUG
    u32 cap;
#endif
};

struct Temp_String_Buffer {
    u32 len;
    char *data;
#if DEBUG
    u32 cap;
#endif
};

Heap_String_Buffer build_heap_string_buffer(u32 cstr_count, const char **list_of_cstrs);
Temp_String_Buffer build_temp_string_buffer(u32 cstr_count, const char **list_of_cstrs);

int init_heap_string_buffer(Heap_String_Buffer *string_buffer, u32 size);
int init_temp_string_buffer(Temp_String_Buffer *string_buffer, u32 size);

// Inlines
inline void kill_heap_string_buffer(Heap_String_Buffer *string_buffer) {
    free_h((void*)(string_buffer->data));
}

inline void copy_to_heap_string_buffer(Heap_String_Buffer *string_buffer, char *data,  u32 len) {
#if DEBUG
    ASSERT(string_buffer->len + len <= string_buffer->cap, "String Buffer Overflow");
#endif

    memcpy(string_buffer->data + string_buffer->len, data, len);
    string_buffer->len += len;

}
inline void copy_to_temp_string_buffer(Temp_String_Buffer *string_buffer, char *data,  u32 len) {
#if DEBUG
    ASSERT(string_buffer->len + len <= string_buffer->cap, "String Buffer Overflow");
#endif

    memcpy(string_buffer->data + string_buffer->len, data, len);
    string_buffer->len += len;
}

inline const char *string_buffer_to_cstr(Temp_String_Buffer *string_buffer) {
    // Always safe
    string_buffer->data[string_buffer->len] = '\0';
    return string_buffer->data;
}
inline const char *string_buffer_to_cstr(Heap_String_Buffer *string_buffer) {
    // Always safe
    string_buffer->data[string_buffer->len] = '\0';
    return string_buffer->data;
}

#endif
