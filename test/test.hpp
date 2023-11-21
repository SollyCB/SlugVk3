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

#include <iostream> // LAAAAAAAME!! @Todo refine this fucking test system lol...

enum Test_Result {
    FAIL    = 0,
    PASS    = 0x01,
    SKIPPED = 0x02,
    BROKEN  = 0x04,
};

// @Todo for now these are just heap string buffers to prevent code from interfering with tests
// (like emptying the temp allocator at whatever stage). Need to give tests their own temp allocators
struct Test_Module {
    Dyn_Array<Heap_String_Buffer> failed_test_names;
    Dyn_Array<Heap_String_Buffer> skipped_test_names;
    Dyn_Array<Heap_String_Buffer> broken_test_names;
    Dyn_Array<Heap_String_Buffer> skipped_broken_test_names;
    Heap_String_Buffer info;
    bool skipped;
    bool broken;
};
struct Test_Tracker {
    Dyn_Array<Test_Module> modules;
};
Test_Tracker* get_test_tracker_instance();
void load_tests();
void end_tests();

void begin_test_module(const char *name,
        Test_Module *mod, const char *function_name,
        const char *file_name, bool broken, bool skipped);
void end_test_module(Test_Module *mod);

#define RED "\u001b[31;1m"
#define GREEN "\u001b[32;1m"
#define YELLOW "\u001b[33;1m"
#define BLUE "\u001b[34;1m"
#define NC "\u001b[0m"

#if TEST_LIST_BROKEN
#define TEST_MSG_BROKEN(name) \
    std::cout << RED << "BROKEN TEST " << NC << "'" << name << "'\n";
#else
#define TEST_MSG_BROKEN(name)
#endif

#if TEST_LIST_SKIPPED
#define TEST_MSG_SKIPPED(name) \
    std::cout << YELLOW << "Skip Test " << NC << "'" << name << "'\n";
#define TEST_MSG_SKIP_BROKEN(name) \
    std::cout << YELLOW << "Warning: Skipping Broken Test " << NC << "'" << name << "'\n";
#else
#define TEST_MSG_SKIPPED(name)
#define TEST_MSG_SKIP_BROKEN(name)
#endif

#define SKIP_BROKEN_TEST_MSG(mod) \
    std::cout << YELLOW << "Warning: Module Skips " << mod.skipped_broken_test_names.len <<  " Broken Tests...\n" << NC;

#define TEST_MSG_FAIL(name, arg1_name, arg2_name, arg1_val, arg2_val) \
    std::cout << RED << "FAILED TEST " << name << ": \n" << NC  \
    << "    " << arg1_name << " = " << arg1_val << ", " << arg2_name << " = " << arg2_val << '\n';

template<typename Arg1, typename Arg2>
void test_eq(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken)
{
    if (broken) {
        TEST_MSG_BROKEN(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->broken_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    // @Todo more efficient to use copy heres rather than build
    if (mod->skipped) {
        if (broken) {
            Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_broken_test_names);
            *tmp = build_heap_string_buffer(1, &test_name); // a build (not a copy here - see above)
        }
        TEST_MSG_SKIPPED(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    if (arg1 == arg2)
        return;

    Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->failed_test_names);
    *tmp = build_heap_string_buffer(1, &test_name);

    TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);

    return;
}
template<typename Arg1, typename Arg2>
void test_lt(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken)
{
    if (broken) {
        TEST_MSG_BROKEN(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->broken_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    // @Todo more efficient to use copy heres rather than build
    if (mod->skipped) {
        if (broken) {
            Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_broken_test_names);
            *tmp = build_heap_string_buffer(1, &test_name); // a build (not a copy here - see above)
        }
        TEST_MSG_SKIPPED(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    if (arg1 < arg2)
        return;

    Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->failed_test_names);
    *tmp = build_heap_string_buffer(1, &test_name);

    TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);

    return;
}
template<typename Arg1, typename Arg2>
void test_floateq(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken)
{
    if (broken) {
        TEST_MSG_BROKEN(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->broken_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    // @Todo more efficient to use copy heres rather than build
    if (mod->skipped) {
        if (broken) {
            Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_broken_test_names);
            *tmp = build_heap_string_buffer(1, &test_name); // a build (not a copy here - see above)
        }
        TEST_MSG_SKIPPED(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    float f1 = arg1 - arg2;
    float f2 = arg2 - arg1;
    float inaccuracy = 0.000001;
    if (f1 < inaccuracy && f2 < inaccuracy)
        return;

    Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->failed_test_names);
    *tmp = build_heap_string_buffer(1, &test_name);

    TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);

    return;
}
template<typename Arg1, typename Arg2>
void test_streq(Test_Module *mod, const char *test_name, const char *function_name,
        const char *arg1_name, const char *arg2_name,
        Arg1 arg1, Arg2 arg2, bool broken)
{
    if (broken) {
        TEST_MSG_BROKEN(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->broken_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    // @Todo more efficient to use copy heres rather than build
    if (mod->skipped) {
        if (broken) {
            Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_broken_test_names);
            *tmp = build_heap_string_buffer(1, &test_name); // a build (not a copy here - see above)
        }
        TEST_MSG_SKIPPED(test_name);
        Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->skipped_test_names);
        *tmp = build_heap_string_buffer(1, &test_name);
        return;
    }

    if (strcmp(arg1, arg2) == 0)
        return;

    Heap_String_Buffer *tmp = append_to_dyn_array<Heap_String_Buffer>(&mod->failed_test_names);
    *tmp = build_heap_string_buffer(1, &test_name);

    TEST_MSG_FAIL(test_name, arg1_name, arg2_name, arg1, arg2);

    return;
}

#define BEGIN_TEST_MODULE(name, broken, skipped) \
    Test_Module *test_module = append_to_dyn_array(&get_test_tracker_instance()->modules); \
    begin_test_module(name, test_module, __FUNCTION__, __FILE__, broken, skipped);

// @Todo see if this needs to do anything... (currently does nothing)
#define END_TEST_MODULE() \
    end_test_module(test_module);

#define TEST_EQ(test_name, arg1, arg2, broken) \
    test_eq(test_module, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken);
#define TEST_LT(test_name, arg1, arg2, broken) \
    test_lt(test_module, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken);
#define TEST_STREQ(test_name, arg1, arg2, broken) \
    test_streq(test_module, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken);
#define TEST_FEQ(test_name, arg1, arg2, broken) \
    test_floateq(test_module, test_name, __FUNCTION__, #arg1, #arg2, arg1, arg2, broken);


#endif // include guard
#endif // #if TEST
