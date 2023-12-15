#if TEST
#include "test.hpp"

static Test_Suite s_Test_Suite;
Test_Suite* get_test_suite_instance() { return &s_Test_Suite; }

// Format colors
#define RED    "\u001b[31;1m"
#define GREEN  "\u001b[32;1m"
#define YELLOW "\u001b[33;1m"
#define BLUE   "\u001b[34;1m"
#define NC     "\u001b[0m"

void load_tests() {
    println("\nTest Config:");
    if (TEST_LIST_BROKEN) {
        println("TEST_LIST_BROKEN  == true, printing broken tests...");
    } else {
        println("TEST_LIST_BROKEN  == false, broken tests silent...");
    }
    if (TEST_LIST_SKIPPED) {
        println("TEST_LIST_SKIPPED == true, printing skipped tests...");
    } else {
        println("TEST_LIST_SKIPPED == false, skipped tests silent...");
    }
    println("\nBeginning Tests...");

    Test_Suite *suite = get_test_suite_instance();
    bool growable        = true;
    bool temp            = false;

    suite->tests         = new_array<Test>(128, growable, temp);
    suite->modules       = new_array<Test_Module>(128, growable, temp);
    suite->string_buffer = create_string_buffer(1024, temp, growable);

    // Test_Tracker *test = get_test_tracker_instance();
    // init_dyn_array(&test->modules, 10);
}
void end_tests() {
    Test_Suite *test = get_test_suite_instance();

    if (test->fail)
        println("");

    u32 skipped_count = 0;
    u32 broken_count = 0;
    u32 fail_count = 0;

    // Ofc these should just be separate arrays on the suite. But it does not matter for now. Bigger fish.
    for(u32 i = 0; i < test->modules.len; ++i) {
        switch(test->modules.data[i].result) {
        case TEST_RESULT_BROKEN:
            println("%sMODULE BROKEN:%s", YELLOW, NC);
            println("    %s", test->modules.data[i].info.str);
            broken_count++;
            break;
        case TEST_RESULT_SKIPPED:
            println("%sMODULE SKIPPED:%s", YELLOW, NC);
            println("    %s", test->modules.data[i].info.str);
            skipped_count++;
            break;
        case TEST_RESULT_FAIL:
            println("%sMODULE FAILED:%s", RED, NC);
            println("    %s", test->modules.data[i].info.str);
            fail_count++;
            break;
        case TEST_RESULT_SUCCESS:
            break;
        }
    }

    if (test->fail || (test->broken && TEST_LIST_BROKEN) || (test->skipped && TEST_LIST_SKIPPED))
        println("");

    if (test->skipped)
        println("%sTESTS WERE SKIPPED%s", YELLOW, NC);
    else
        println("%sNO TESTS SKIPPED%s", GREEN, NC);

    if (test->broken)
        println("%sTESTS ARE BROKEN%s", YELLOW, NC);
    else
        println("%sNO TESTS BROKEN%s", GREEN, NC);

    if (test->fail)
        println("%sTESTS WERE FAILED%s", RED, NC);
    else 
        println("%sNO TESTS FAILED%s", GREEN, NC);

    println("");

    destroy_string_buffer(&test->string_buffer);
    free_array(&test->modules);
    free_array(&test->tests);

    reset_temp();
}

void begin_test_module(const char *name, Test_Module *mod, const char *function_name,
        const char *file_name, bool broken, bool skipped) 
{
    *mod = {};

    Test_Suite *suite = get_test_suite_instance();
    mod->start = suite->tests.len;

    char info_buffer[256];
    string_format(info_buffer, "[%s, fn %s] %s", file_name, function_name, name);
    assert(strlen(info_buffer) < 255);

    mod->info = string_buffer_get_string(&suite->string_buffer, info_buffer);
    if (broken) {
        mod->result = TEST_RESULT_BROKEN;
    } else if (skipped) {
        mod->result = TEST_RESULT_SKIPPED;
    } else {
        mod->result = TEST_RESULT_SUCCESS;
    }
}

// Message macros
#if TEST_LIST_BROKEN
#define TEST_MSG_BROKEN(info) \
    println("%sBroken Test: %s%s", YELLOW, NC, info)
#else
#define TEST_MSG_BROKEN(name)
#endif

#if TEST_LIST_SKIPPED
#define TEST_MSG_SKIPPED(name) \
    println("%sSkipped Test%s '%s'", YELLOW, NC, name)
#else
#define TEST_MSG_SKIPPED(name)
#endif

#define SKIP_BROKEN_TEST_MSG(mod) \
    println("%sWarning: Module Skips %u Broken Tests...%s", YELLOW, mod.skipped_broken_test_names.len, NC)

#define TEST_MSG_FAIL(name, info, msg) println("%sFAILED TEST %s%s:\n%s\n    %s", RED, name, NC, info, msg)

#define TEST_MSG_PASS println("%sOK%s", GREEN, NC)

// Tests
void test(Test_Module *module, const char *function_name, const char *file_name, int line_number,
          const char *test_name, const char *msg, bool test_passed, bool broken)
{
    Test_Suite *suite  = get_test_suite_instance();
    Test_Result result;

    char info[127];
    string_format(info, "%s, line %i, fn %s", file_name,  line_number, function_name);
    String string = string_buffer_get_string(&suite->string_buffer, info);

    if (broken || module->result == TEST_RESULT_BROKEN || module->result == TEST_RESULT_SKIPPED) {
        if (broken || module->result == broken) {
            TEST_MSG_BROKEN(info);
            result = TEST_RESULT_BROKEN;
            suite->broken++;
        } else {
            println("%s", string.str);
            TEST_MSG_SKIPPED(test_name);
            result = TEST_RESULT_SKIPPED;
            suite->skipped++;
        }
    } else {
        if (test_passed) {
            result = TEST_RESULT_SUCCESS;
        } else {
            TEST_MSG_FAIL(test_name, info, msg);
            result = TEST_RESULT_FAIL;
            module->result = TEST_RESULT_FAIL;
            suite->fail++;
        }
    }

    if (result != TEST_RESULT_SUCCESS) {
        Test test = {.info = string, .result = result, .index = suite->tests.len};
        array_add(&suite->tests, &test);
    }

    return;
}

void test_eq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
             const char *arg2_name, s64 arg1, s64 arg2, bool broken, int line_number, const char *file_name)
{
    bool test_passed = arg1 == arg2;

    char msg[256];
    string_format(msg, "%s = %i, %s = %i", arg1_name, arg1, arg2_name, arg2);
    test(mod, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

void test_lt(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
             const char *arg2_name, s64 arg1, s64 arg2, bool broken, int line_number, const char *file_name)
{   
    bool test_passed = arg1 < arg2;

    char msg[256];
    string_format(msg, "%s = %i, %s = %i", arg1_name, arg1, arg2_name, arg2);
    test(mod, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

void test_floateq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
                  const char *arg2_name, float arg1, float arg2, bool broken, int line_number, const char *file_name)
{   
    float f1 = arg1 - arg2;
    float f2 = arg2 - arg1;
    float inaccuracy = 0.000001;

    bool test_passed = (f1 < inaccuracy) && (f2 < inaccuracy) ? true : false;

    char msg[256];
    string_format(msg, "%s = %f, %s = %f", arg1_name, arg1, arg2_name, arg2);
    test(mod, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

void test_streq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
                const char *arg2_name, const char* arg1, const char* arg2, bool broken, int line_number,
                const char *file_name)
{   
    bool test_passed = strcmp(arg1, arg2) == 0;

    char msg[256];
    string_format(msg, "%s = %s, %s = %s", arg1_name, arg1, arg2_name, arg2);
    test(mod, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

void test_ptreq(Test_Module *mod, const char *test_name, const char *function_name, const char *arg1_name,
                const char *arg2_name, const void *arg1, const void* arg2, bool broken, int line_number,
                const char *file_name)
{   
    bool test_passed = arg1 == arg2;

    char msg[256];
    string_format(msg, "%s = %uh, %s = %uh", arg1_name, arg1, arg2_name, arg2);
    test(mod, function_name, file_name, line_number, test_name, msg, test_passed, broken);

    return;
}

#endif // #if TEST
