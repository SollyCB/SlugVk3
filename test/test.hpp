#if TEST

#ifndef SOL_TEST_HPP_INCLUDE_GUARD_
#define SOL_TEST_HPP_INCLUDE_GUARD_

#define TEST_LIST_BROKEN true
#define TEST_LIST_SKIPPED true

// Define 'TEST_LIST_BROKEN' to list broken tests, and 'TEST_LIST_SKIPPED' to list skipped tests
#include "../assert.h"
#include "../allocator.hpp"
#include "../string.hpp"
#include "../array.hpp"
#include "../print.h"

struct Test_Suite;
Test_Suite* get_test_suite_instance();

enum Test_Result {
    TEST_RESULT_SUCCESS = 0,
    TEST_RESULT_FAIL = 1,
    TEST_RESULT_SKIPPED = 2,
    TEST_RESULT_BROKEN = 3,
};

struct Test_Module {
    String info;
    Test_Result result;
    u32 start;
    u32 end;
};

struct Test {
    String info;
    Test_Result result;
    u32 index;
};

struct Test_Suite {
    u32 fail;
    u32 skipped;
    u32 broken;
    String_Buffer string_buffer;
    Array<Test_Module> modules;
    Array<Test> tests;
};

void load_tests();
void end_tests();

void begin_test_module(const char *name, Test_Module *mod, const char *function_name, const char *file_name,
                       bool broken, bool skipped);
void end_test_module(Test_Module *mod);

void test_eq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
             const char *arg2_name, s64 arg1, s64 arg2, bool broken, int line_number, const char *file_name);
void test_lt(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
             const char *arg2_name, s64 arg1, s64 arg2, bool broken, int line_number, const char *file_name);
void test_floateq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
                  const char *arg2_name, float arg1, float arg2, bool broken, int line_number, const char *file_name);
void test_streq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
                const char *arg2_name, const char* arg1, const char* arg2, bool broken, int line_number,
                const char *file_name);
void test_ptreq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
                const char *arg2_name, const void *arg1, const void* arg2, bool broken, int line_number,
                const char *file_name);

#define TEST_MODULES (&get_test_suite_instance()->modules)
#define LAST_MODULE  (array_last(TEST_MODULES))

#define BEGIN_TEST_MODULE(name, broken, skipped) \
    begin_test_module(name, array_append(TEST_MODULES), __FUNCTION__, __FILE__, broken, skipped);

#define END_TEST_MODULE() LAST_MODULE->end = (get_test_suite_instance()->tests.len - 1);

#define TEST_EQ(test_name, arg1, arg2, broken) \
    test_eq(LAST_MODULE, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__);
#define TEST_LT(test_name, arg1, arg2, broken) \
    test_lt(LAST_MODULE, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__);
#define TEST_STREQ(test_name, arg1, arg2, broken) \
    test_streq(LAST_MODULE, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__);
#define TEST_FEQ(test_name, arg1, arg2, broken) \
    test_floateq(LAST_MODULE, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__);
#define TEST_PTREQ(test_name, arg1, arg2, broken) \
    test_ptreq(LAST_MODULE, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken, __LINE__, __FILE__);


#endif // include guard
#endif // #if TEST
