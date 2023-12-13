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

#include <iostream> // LAAAAAAAME!! @Todo refine this fucking test system lol...

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

#define RED    "\u001b[31;1m"
#define GREEN  "\u001b[32;1m"
#define YELLOW "\u001b[33;1m"
#define BLUE   "\u001b[34;1m"
#define NC     "\u001b[0m"

#if TEST_LIST_BROKEN
#define TEST_MSG_BROKEN(name) \
    println("%sBROKEN TEST '%s'", RED, NC, name)
#else
#define TEST_MSG_BROKEN(name)
#endif

#if TEST_LIST_SKIPPED
#define TEST_MSG_SKIPPED(name) \
    println("%sSkip Test%s '%s'")
#define TEST_MSG_SKIP_BROKEN(name) \
    println("%sWarning: Skipping Broken Test '%s'", YELLOW, NC, name)
#else
#define TEST_MSG_SKIPPED(name)
#define TEST_MSG_SKIP_BROKEN(name)
#endif

#define SKIP_BROKEN_TEST_MSG(mod) \
    println("%sWarning: Module Skips %u Broken Tests...%s", YELLOW, mod.skipped_broken_test_names.len, NC)

// @Todo Cannot print floats yet, and these may be floats...
#define TEST_MSG_FAIL(name, arg1_name, arg2_name, arg1_val, arg2_val) \
    std::cout << RED << "FAILED TEST " << name << ": \n" << NC  \
    << "    " << arg1_name << " = " << arg1_val << ", " << arg2_name << " = " << arg2_val << '\n'

#define TEST_MSG_PASS println("%sOK%s", GREEN, NC)

template<typename Arg1, typename Arg2>
void test_eq(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken, int line_number, const char *file)
{
    Test_Suite *suite  = get_test_suite_instance();
    Test_Result result;

    char info[127];
    string_format(info, "%s, %s(), line: %i", file, function_name, line_number);
    String string = string_buffer_get_string(&suite->string_buffer, info);

    if (broken) {
        println("%s", string.str);
        TEST_MSG_SKIPPED(test_name);
        result      = TEST_RESULT_BROKEN;
        mod->result = TEST_RESULT_BROKEN;
        suite->broken++;
    } else if (mod->result == TEST_RESULT_SKIPPED) {
        println("%s", string.str);
        TEST_MSG_BROKEN(test_name);
        result      = TEST_RESULT_SKIPPED;
        suite->skipped++;
    } else if (arg1 == arg2) {
        result = TEST_RESULT_SUCCESS;
        //TEST_MSG_PASS;
    } else {
        println("%s", string.str);
        result      = TEST_RESULT_FAIL;
        mod->result = mod->result != TEST_RESULT_BROKEN ? TEST_RESULT_FAIL : TEST_RESULT_BROKEN;
        TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);
        suite->fail++;
    }

    if (broken) {result = TEST_RESULT_BROKEN;}

    if (result != TEST_RESULT_SUCCESS) {
        Test test = {.info=string,.result=result,.index=suite->tests.len};
        array_add(&suite->tests, &test);
    }

    return;
}

template<typename Arg1, typename Arg2>
void test_lt(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken, int line_number, const char *file)
{   
    Test_Suite *suite  = get_test_suite_instance();
    Test_Result result;

    char info[127];
    string_format(info, "%s, %s(), line: %i", file, function_name, line_number);
    String string = string_buffer_get_string(&suite->string_buffer, info);

    if (broken) {
        println("%s", string.str);
        TEST_MSG_SKIPPED(test_name);
        result      = TEST_RESULT_BROKEN;
        mod->result = TEST_RESULT_BROKEN;
        suite->broken++;
    } else if (mod->result == TEST_RESULT_SKIPPED) {
        println("%s", string.str);
        TEST_MSG_BROKEN(test_name);
        result      = TEST_RESULT_SKIPPED;
        suite->skipped++;
    } else if (arg1 < arg2) {
        result = TEST_RESULT_SUCCESS;
        // TEST_MSG_PASS;
    } else {
        println("%s", string.str);
        result      = TEST_RESULT_FAIL;
        mod->result = mod->result != TEST_RESULT_BROKEN ? TEST_RESULT_FAIL : TEST_RESULT_BROKEN;
        TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);
        suite->fail++;
    }

    if (broken) {result = TEST_RESULT_BROKEN;}

    if (result != TEST_RESULT_SUCCESS) {
        Test test = {.info=string,.result=result,.index=suite->tests.len};
        array_add(&suite->tests, &test);
    }

    return;
}

template<typename Arg1, typename Arg2>
void test_floateq(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken, int line_number, const char *file)
{   
    Test_Suite *suite  = get_test_suite_instance();
    Test_Result result;

    char info[127];
    string_format(info, "%s, %s(), line: %i", file, function_name, line_number);
    String string = string_buffer_get_string(&suite->string_buffer, info);

    float f1 = arg1 - arg2;
    float f2 = arg2 - arg1;
    float inaccuracy = 0.000001;

    if (broken) {
        println("%s", string.str);
        TEST_MSG_SKIPPED(test_name);
        result      = TEST_RESULT_BROKEN;
        mod->result = TEST_RESULT_BROKEN;
        suite->broken++;
    } else if (mod->result == TEST_RESULT_SKIPPED) {
        println("%s", string.str);
        TEST_MSG_BROKEN(test_name);
        result      = TEST_RESULT_SKIPPED;
        suite->skipped++;
    } else if (f1 < inaccuracy && f2 < inaccuracy) {
        result = TEST_RESULT_SUCCESS;
        // TEST_MSG_PASS;
    } else {
        println("%s", string.str);
        result      = TEST_RESULT_FAIL;
        mod->result = mod->result != TEST_RESULT_BROKEN ? TEST_RESULT_FAIL : TEST_RESULT_BROKEN;
        TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);
        suite->fail++;
    }

    if (broken) {result = TEST_RESULT_BROKEN;}

    if (result != TEST_RESULT_SUCCESS) {
        Test test = {.info=string,.result=result,.index=suite->tests.len};
        array_add(&suite->tests, &test);
    }

    return;
}

template<typename Arg1, typename Arg2>
void test_streq(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken, int line_number, const char *file)
{   
    Test_Suite *suite  = get_test_suite_instance();
    Test_Result result;

    char info[127];
    string_format(info, "%s, %s(), line: %i", file, function_name, line_number);
    String string = string_buffer_get_string(&suite->string_buffer, info);

    if (broken) {
        println("%s", string.str);
        TEST_MSG_SKIPPED(test_name);
        result      = TEST_RESULT_BROKEN;
        mod->result = TEST_RESULT_BROKEN;
        suite->broken++;
    } else if (mod->result == TEST_RESULT_SKIPPED) {
        println("%s", string.str);
        TEST_MSG_BROKEN(test_name);
        result      = TEST_RESULT_SKIPPED;
        suite->skipped++;
    } else if (strcmp(arg1, arg2) == 0) {
        result = TEST_RESULT_SUCCESS;
        // TEST_MSG_PASS;
    } else {
        println("%s", string.str);
        result      = TEST_RESULT_FAIL;
        mod->result = mod->result != TEST_RESULT_BROKEN ? TEST_RESULT_FAIL : TEST_RESULT_BROKEN;
        TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);
        suite->fail++;
    }

    if (broken) {result = TEST_RESULT_BROKEN;}

    if (result != TEST_RESULT_SUCCESS) {
        Test test = {.info=string,.result=result,.index=suite->tests.len};
        array_add(&suite->tests, &test);
    }

    return;
}

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


#endif // include guard
#endif // #if TEST
