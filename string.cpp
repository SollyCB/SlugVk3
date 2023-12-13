#include "string.hpp"
#include "allocator.hpp"

String_Buffer create_string_buffer(u32 size, bool temp, bool growable) {
    String_Buffer string_buffer = {};

    string_buffer.flags |= growable ? STRING_BUFFER_GROWABLE_BIT : 0x0;
    string_buffer.flags |= temp     ? STRING_BUFFER_TEMP_BIT     : 0x0;

    string_buffer.cap = align(size, 16);

    if (temp) {
        string_buffer.buf = (char*)malloc_t(string_buffer.cap, 16);
    } else {
        string_buffer.buf = (char*)malloc_h(string_buffer.cap, 16);
    }

    return string_buffer;
}

void destroy_string_buffer(String_Buffer *buf) {
    if ((buf->flags & STRING_BUFFER_TEMP_BIT) == 0)
        free_h(buf->buf);
    *buf = {};
}

static void string_buffer_do_resize(String_Buffer *string_buffer, u32 len);

String string_buffer_get_string(String_Buffer *string_buffer, String *str) {
    String string = {};
    string.len = str->len;
    string.str = (char*)(string_buffer->buf + string_buffer->len);

    if (string_buffer->len + string.len + 1 >= string_buffer->cap)
        string_buffer_do_resize(string_buffer, string.len + 1);

    memcpy(string_buffer->buf + string_buffer->len, string.str, string.len); // copy null

    string_buffer->len += string.len + 1;
    string_buffer->buf[string_buffer->len - 1] = '\0';

    return string;
}

String string_buffer_get_string(String_Buffer *string_buffer, const char *cstr) {
    String string = {};
    string.len = strlen(cstr);
    string.str = (const char*)(string_buffer->buf + string_buffer->len);

    if (string_buffer->len + string.len + 1 >= string_buffer->cap)
        string_buffer_do_resize(string_buffer, string.len + 1);

    memcpy(string_buffer->buf + string_buffer->len, cstr, string.len + 1); // copy null

    string_buffer->len += string.len + 1;

    return string;
}

static void string_buffer_do_resize(String_Buffer *string_buffer, u32 len) {
    if (string_buffer->flags & STRING_BUFFER_GROWABLE_BIT) {

        string_buffer->cap += len;
        string_buffer->cap *= 2;
        string_buffer->cap  = align(string_buffer->cap, 16);

        if (string_buffer->flags & STRING_BUFFER_TEMP_BIT) {
            char *old_data     = string_buffer->buf;
            string_buffer->buf = (char*)malloc_t(string_buffer->cap, 16);

            memcpy(string_buffer->buf, old_data, string_buffer->len - len);
        } else {
            string_buffer->buf = (char*)realloc_h(string_buffer->buf, string_buffer->cap);
        }
    } else {
        assert(false && "String Buffer Overflow");
        *string_buffer = {}; // Crash
        return;
    }
}
