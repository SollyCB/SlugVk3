#if TEST
#include "test.hpp"

static Test_Suite s_Test_Suite;
Test_Suite* get_test_suite_instance() { return &s_Test_Suite; }

void load_tests() {
    std::cout << "Test Config:\n";
    if (TEST_LIST_BROKEN) {
        std::cout << "TEST_LIST_BROKEN == true, printing broken tests...\n";
    } else {
        std::cout << "TEST_LIST_BROKEN == false, broken tests silent...\n";
    }
    if (TEST_LIST_SKIPPED) {
        std::cout << "TEST_LIST_SKIPPED == true, printing skipped tests...\n";
    } else {
        std::cout << "TEST_LIST_SKIPPED == false, skipped tests silent...\n";
    }
    std::cout << "\nBeginning Tests...\n";

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

    bool all_pass  = true;
    bool no_skip   = true;
    bool no_broken = true;

    for(u32 i = 0; i < test->modules.len; ++i) {
        switch(test->modules.data[i].result) {
        case TEST_RESULT_BROKEN:
            println("%sMODULE BROKEN:%s", YELLOW, NC);
            println("    %s", test->modules.data[i].info.str);
            break;
        case TEST_RESULT_SKIPPED:
            println("%sMODULE SKIPPED:%s", YELLOW, NC);
            println("    %s", test->modules.data[i].info.str);
            break;
        case TEST_RESULT_FAIL:
            println("%sMODULE FAILED:%s", RED, NC);
            println("    %s", test->modules.data[i].info.str);
            break;
        case TEST_RESULT_SUCCESS:
            break;
        }
    }

    if (test->skipped)
        std::cout << YELLOW << "TESTS WERE SKIPPED" << NC << '\n';
    else
        std::cout << GREEN << "NO TESTS SKIPPED" << NC << '\n';

    if (test->broken)
        std::cout << YELLOW << "TESTS ARE BROKEN" << NC << '\n';
    else
        std::cout << GREEN << "NO BROKEN TESTS" << NC << '\n';

    if (test->fail)
        std::cout << RED << "TESTS FAILED" << NC << '\n';
    else 
        std::cout << GREEN << "NO FAILURES" << NC << '\n';

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
    string_format(info_buffer, "[%s, %s] %s", function_name, file_name, name);
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

#endif // #if TEST
