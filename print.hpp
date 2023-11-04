#ifndef SOL_PRINT_HPP_INCLUDE_GUARD_
#define SOL_PRINT_HPP_INCLUDE_GUARD_

#include <stdio.h> // @Todo implement my own streaming to stdout and get rid of printf
#include <string.h>
#include <assert.h> // @Todo get rid of this asserting
#include <cstdarg>
#include "typedef.h"

static const u64 PRINT_FORMATTER_BUF_LEN = 1024;

static inline s32 print_parse_signed_int(const char *str, u32 len, char *buf, s64 num) {
    u32 end = 0;
    char rev[200];

    if (num == 0) {
        rev[0] = '0';
        end++;
        goto loop_end; // @Hack
    }

    if (num < 0) {
        buf[0] = '-';
        buf++;
        end++;
        num = -num;
    }

    char c;
    u8 val;
    while(num > 0) {
        val = num % 10;
        num /= 10;
        switch(val) {
        case 0:
        {
            c = '0';
            break;
        }
        case 1:
        {
            c = '1';
            break;
        }
        case 2:
        {
            c = '2';
            break;
        }
        case 3:
        {
            c = '3';
            break;
        }
        case 4:
        {
            c = '4';
            break;
        }
        case 5:
        {
            c = '5';
            break;
        }
        case 6:
        {
            c = '6';
            break;
        }
        case 7:
        {
            c = '7';
            break;
        }
        case 8:
        {
            c = '8';
            break;
        }
        case 9:
        {
            c = '9';
            break;
        }
        default:
            assert(false && "this is not a number i can use");
        }
        rev[end] = c;
        end++;
    }

    loop_end:
    for(int i = 0; i < end; ++i) {
        buf[i] = rev[(end - 1) - i];
    }
    return end;
}
static inline s32 print_parse_unsigned_int(const char *str, u32 len, char *buf, u64 num) {
    u32 end = 0;

    char rev[200];

    char c;
    u8 val;
    if (num == 0) {
        rev[0] = '0';
        end++;
        goto loop_end; // @Hack
    }

    while(num > 0) {
        val = num % 10;
        num /= 10;
        switch(val) {
        case 0:
        {
            c = '0';
            break;
        }
        case 1:
        {
            c = '1';
            break;
        }
        case 2:
        {
            c = '2';
            break;
        }
        case 3:
        {
            c = '3';
            break;
        }
        case 4:
        {
            c = '4';
            break;
        }
        case 5:
        {
            c = '5';
            break;
        }
        case 6:
        {
            c = '6';
            break;
        }
        case 7:
        {
            c = '7';
            break;
        }
        case 8:
        {
            c = '8';
            break;
        }
        case 9:
        {
            c = '9';
            break;
        }
        default:
            assert(false && "this is not a number i can use");
        }
        rev[end] = c;
        end++;
    }

    loop_end: // goto label
    for(int i = 0; i < end; ++i) {
        buf[i] = rev[(end - 1) - i];
    }

    return end;
}
static inline u64 print_parse_string(const char *str, u32 len, char *buf, const char* cstr) {
    u64 str_len = strlen(cstr);
    strcpy(buf, cstr);
    return str_len;
}

static void println(const char* str, ...) {
    va_list args;
    va_start(args, str);

    char buf[PRINT_FORMATTER_BUF_LEN];
    u32 buf_index = 0;

    u64 len = strlen(str);
    s32 size;

    s64 s;
    u64 u;
    const char *cstr;

    bool skip = false;
    for(int i = 0; i < len; ++i) {

        if (str[i] == '\\' && skip) {
            buf[buf_index] = '\\';
            buf_index++;
            skip = false;
            continue;
        }
        if (str[i] == '\\') {
            skip = true;
            //buf_index++;
            continue;
        }
        if (str[i] == '%' && skip) {
            buf[buf_index] = str[i];
            buf_index++;
            skip = false;
            continue;
        }
        if (str[i] != '%') {
            buf[buf_index] = str[i];
            buf_index++;
            skip = false;
            continue;
        }
        skip = false;

        i++;
        switch(str[i]) {
        case 's':
        {
            s = va_arg(args, s64);
            size = print_parse_signed_int(str + i, len - i, buf + buf_index, s);
            buf_index += size;
            break;
        }
        case 'u':
        {
            u = va_arg(args, u64);
            size = print_parse_unsigned_int(str + i, len - i, buf + buf_index, u);
            buf_index += size;
            break;
        }
        case 'c':
        {
            cstr = va_arg(args, const char*);
            size = print_parse_string(str + i, len - i, buf + buf_index, cstr);
            buf_index += size;
            break;
        }
        default:
            assert(false && "Cannot understand print statement");
        } // switch str[i]
    }

    va_end(args);

    buf[buf_index] = '\n';
    buf[buf_index + 1] = '\0';
    printf("%s", buf);
}

static void print(const char* str, ...) {
    va_list args;
    va_start(args, str);

    char buf[PRINT_FORMATTER_BUF_LEN];
    u32 buf_index = 0;

    u64 len = strlen(str);
    s32 size;

    s64 s;
    u64 u;
    const char *cstr;

    bool skip = false;
    for(int i = 0; i < len; ++i) {

        if (str[i] == '\\' && skip) {
            buf[buf_index] = '\\';
            skip = false;
            buf_index++;
            continue;
        }
        if (str[i] == '\\') {
            skip = true;
            buf_index++;
            continue;
        }
        if (str[i] != '%') {
            buf[buf_index] = str[i];
            buf_index++;
            skip = false;
            continue;
        }
        if (str[i] == '%' && skip) {
            buf[buf_index] = '%';
            buf_index++;
            skip = false;
            continue;
        }
        skip = false;

        i++;
        switch(str[i]) {
        case 's':
        {
            s = va_arg(args, s64);
            size = print_parse_signed_int(str + i, len - i, buf + buf_index, s);
            buf_index += size;
            break;
        }
        case 'u':
        {
            u = va_arg(args, u64);
            size = print_parse_unsigned_int(str + i, len - i, buf + buf_index, u);
            buf_index += size;
            break;
        }
        case 'c':
        {
            cstr = va_arg(args, const char*);
            size = print_parse_string(str + i, len - i, buf + buf_index, cstr);
            buf_index += size;
            break;
        }
        default:
            assert(false && "Cannot understand print statement");
        } // switch str[i]
    }

    va_end(args);

    buf[buf_index] = '\0';
    printf("%s", buf);
}

#if DEBUG
static void dbg_println(const char* str, ...) {
    va_list args;
    va_start(args, str);

    char buf[PRINT_FORMATTER_BUF_LEN];
    u32 buf_index = 0;

    u64 len = strlen(str);
    s32 size;

    s64 s;
    u64 u;
    const char *cstr;

    bool skip = false;
    for(int i = 0; i < len; ++i) {

        if (str[i] == '\\' && skip) {
            buf[buf_index] = '\\';
            skip = false;
            buf_index++;
            continue;
        }
        if (str[i] == '\\') {
            skip = true;
            buf_index++;
            continue;
        }
        if (str[i] != '%') {
            buf[buf_index] = str[i];
            buf_index++;
            skip = false;
            continue;
        }
        if (str[i] == '%' && skip) {
            buf[buf_index] = '%';
            buf_index++;
            skip = false;
            continue;
        }
        skip = false;

        i++;
        switch(str[i]) {
        case 's':
        {
            s = va_arg(args, s64);
            size = print_parse_signed_int(str + i, len - i, buf + buf_index, s);
            buf_index += size;
            break;
        }
        case 'u':
        {
            u = va_arg(args, u64);
            size = print_parse_unsigned_int(str + i, len - i, buf + buf_index, u);
            buf_index += size;
            break;
        }
        case 'c':
        {
            cstr = va_arg(args, const char*);
            size = print_parse_string(str + i, len - i, buf + buf_index, cstr);
            buf_index += size;
            break;
        }
        default:
            assert(false && "Cannot understand print statement");
        } // switch str[i]
    }

    va_end(args);

    buf[buf_index] = '\0';
    printf("%s\n", buf);
}
#else
#define dbg_println(const char* str, ...)
#endif

#endif // include guard
