#include "string.hpp"
#include "allocator.hpp"

Heap_String_Buffer build_heap_string_buffer(u32 cstr_count, const char **list_of_cstrs) {

    u32 total_len = 0;
    u64 mark = get_mark_temp();
    u32 *lens = (u32*)malloc_t(cstr_count * 4, 4);

    for(int i = 0; i < cstr_count; ++i) {
        lens[i] = 0;
        for(int j = 0; list_of_cstrs[i][j] != '\0'; ++j) { 
            total_len++;
            lens[i]++;
        }
    }

    Heap_String_Buffer ret; 
    init_heap_string_buffer(&ret, total_len);
    for(int i = 0; i < cstr_count; ++i) {
        copy_to_heap_string_buffer(&ret, (char*)list_of_cstrs[i], lens[i]);
    }

    reset_to_mark_temp(mark);
    return ret;
}

Temp_String_Buffer build_temp_string_buffer(u32 cstr_count, const char **list_of_cstrs) {
    u32 total_len = 0;
    u64 mark = get_mark_temp();
    u32 *lens = (u32*)malloc_t(cstr_count * 4, 4);

    for(int i = 0; i < cstr_count; ++i) {
        lens[i] = 0;
        for(int j = 0; *(list_of_cstrs[i] + j) != '\0'; ++j) { 
            total_len++;
            lens[i]++;
        }
    }

    Temp_String_Buffer ret; 
    init_temp_string_buffer(&ret, total_len);
    for(int i = 0; i < cstr_count; ++i) {
        copy_to_temp_string_buffer(&ret, (char*)list_of_cstrs[i], lens[i]);
    }

    reset_to_mark_temp(mark);
    return ret;
}

int init_heap_string_buffer(Heap_String_Buffer *string_buffer, u32 size) {
    string_buffer->data = (char*)malloc_h(size + 1, 1);
    string_buffer->len = 0;

#if DEBUG
    string_buffer->cap = size;
#endif
    return size + 1;
}
int init_temp_string_buffer(Temp_String_Buffer *string_buffer, u32 size) {
    string_buffer->data = (char*)malloc_t(size + 1, 1);
    string_buffer->len = 0;

#if DEBUG
    string_buffer->cap = size;
#endif
    return size + 1;
}
