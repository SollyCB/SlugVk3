#include "file.hpp"

/*
   @Todo Collapse these functions. So many just being pasted is sily lol.
*/

const u8* file_read_bin_temp_large(const char *file_name, u64 size) {
    FILE *file = fopen(file_name, "rb");
    assert(file && "Could Not Open File");
    if (!file) {
        println("Failed to read file %s", file_name);
        return NULL;
    }
    u8 *ret = malloc_t(size, 8);
    size = fread(ret, 1, size, file);
    fclose(file);
    return ret;
}

const u8* file_read_bin_temp(const char *file_name, u64 *size) {
    FILE *file = fopen(file_name, "rb");

    if (!file) {
        println("FAILED TO READ FILE %s", file_name);
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *contents = malloc_t(*size, 8); // 8 byte aligned as contents of file may need to be aligned

    // idk if it is worth checking the returned size? It seems to be wrong on windows
    //fread(contents, 1, *size, file);
    size_t read = fread(contents, 1, *size, file);

    if (*size != read)
        println("Failed to read entire file");

    //assert(read == *byte_count && "Failed to read entire file: read = %i, file_len = %i", read, *byte_count);
    fclose(file);

    return (u8*)contents;
}

const u8* file_read_bin_heap(const char *file_name, u64 *size) {
    FILE *file = fopen(file_name, "rb");

    if (!file) {
        println("FAILED TO READ FILE %s", file_name);
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *contents = malloc_h(*size, 8); // 8 byte aligned as contents of file may need to be aligned

    // idk if it is worth checking the returned size? It seems to be wrong on windows
    //fread(contents, 1, *size, file);
    size_t read = fread(contents, 1, *size, file);

    if (*size != read)
        println("Failed to read entire file, %s", file_name);

    //assert(read == *byte_count && "Failed to read entire file: read = %i, file_len = %i", read, *byte_count);
    fclose(file);

    return (u8*)contents;
}
const u8* file_read_char_temp(const char *file_name, u64 *size) {
    FILE *file = fopen(file_name, "r");

    if (!file) {
        println("Failed to read file %s", file_name);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *contents = malloc_t(*size, 8); // 8 byte aligned as contents of file may need to be aligned

    // idk if it is worth checking the returned size? It seems to be wrong on windows
    //fread(contents, 1, *size, file);
    size_t read = fread(contents, 1, *size, file);

    if (*size != read) {
        println("Failed to read entire file, %s", file_name);
        println("    File Size: %u, Size Read: %u", *size, read);
    }

    //assert(read == *byte_count && "Failed to read entire file: read = %i, file_len = %i", read, *byte_count);
    fclose(file);

    return (u8*)contents;
}

const u8* file_read_char_heap(const char *file_name, u64 *size) {
    FILE *file = fopen(file_name, "r");

    if (!file) {
        println("FAILED TO READ FILE %s", file_name);
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *contents = malloc_h(*size, 8); // 8 byte aligned as contents of file may need to be aligned

    // idk if it is worth checking the returned size? It seems to be wrong on windows
    //fread(contents, 1, *size, file);
    size_t read = fread(contents, 1, *size, file);

    if (*size != read) {
        println("Failed to read entire file, %s", file_name);
        println("    File Size: %u, Size Read: %u", *size, read);
    }

    //assert(read == *byte_count && "Failed to read entire file: read = %i, file_len = %i", read, *byte_count);
    fclose(file);

    return (u8*)contents;
}

const u8* file_read_char_heap_padded(const char *file_name, u64 *size, int pad_size) {
    FILE *file = fopen(file_name, "r");

    if (!file) {
        println("FAILED TO READ FILE %s", file_name);
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *contents = malloc_h(size[0] + pad_size, 8);

    // idk if it is worth checking the returned size? It seems to be wrong on windows
    //fread(contents, 1, *size, file);
    size_t read = fread(contents, 1, *size, file);

    if (*size != read) {
        println("Failed to read entire file, %s", file_name);
        println("    File Size: %u, Size Read: %u", *size, read);
    }

    //assert(read == *byte_count && "Failed to read entire file: read = %i, file_len = %i", read, *byte_count);
    fclose(file);

    return (u8*)contents;
}
const u8* file_read_char_temp_padded(const char *file_name, u64 *size, int pad_size) {
    FILE *file = fopen(file_name, "r");

    if (!file) {
        println("FAILED TO READ FILE %s", file_name);
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *contents = malloc_t(size[0] + pad_size, 8);

    // idk if it is worth checking the returned size? It seems to be wrong on windows
    //fread(contents, 1, *size, file);
    size_t read = fread(contents, 1, *size, file);

    if (*size != read) {
        println("Failed to read entire file, %s", file_name);
        println("    File Size: %u, Size Read: %u", *size, read);
    }

    //assert(read == *byte_count && "Failed to read entire file: read = %i, file_len = %i", read, *byte_count);
    fclose(file);

    return (u8*)contents;
}
