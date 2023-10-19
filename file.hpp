#ifndef SOL_FILE_HPP_INCLUDE_GUARD_
#define SOL_FILE_HPP_INCLUDE_GUARD_

#include "basic.h"


const u8* file_read_bin_temp_large(const char *file_name, u64 size);
const u8* file_read_bin_temp(const char *file_name, u64 *size);
const u8* file_read_bin_heap(const char *file_name, u64 *size);
const u8* file_read_char_temp(const char *file_name, u64 *size);
const u8* file_read_char_heap(const char *file_name, u64 *size);
const u8* file_read_char_heap_padded(const char *file_name, u64 *size, int pad_size);
const u8* file_read_char_temp_padded(const char *file_name, u64 *size, int pad_size);

#endif // include guard
