#if TEST
#include "test.hpp"

static Test_Tracker s_Test_Tracker;
Test_Tracker* get_test_tracker_instance() { return &s_Test_Tracker; }
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

    Test_Tracker *test = get_test_tracker_instance();
    init_dyn_array(&test->modules, 10);
}
void end_tests() {
    Test_Tracker *test = get_test_tracker_instance();

    bool all_pass  = true;
    bool no_skip   = true;
    bool no_broken = true;
    for(int i = 0; i < test->modules.len; ++i) {
        Test_Module mod = test->modules.data[i];
        std::cout << string_buffer_to_cstr(&mod.info) << ": \n";
        if (mod.failed_test_names.len  == 0 &&
            mod.broken_test_names.len  == 0 &&
            mod.skipped_test_names.len == 0 &&
            mod.skipped_broken_test_names.len == 0)
        {
            std::cout << "All OK\n";
            continue;
        }

        if (mod.failed_test_names.len) {
            std::cout << RED << "FAILED " << NC << mod.failed_test_names.len << " test(s):\n";
            for(int j = 0; j < mod.failed_test_names.len; ++j) {
                std::cout << "    " << string_buffer_to_cstr(&mod.failed_test_names.data[j]) << '\n';
            }
            all_pass = false;
        }

        if (mod.broken_test_names.len) {
            std::cout << "Contains "<< mod.broken_test_names.len << ' '<<  RED << "BROKEN " << NC " test(s)";
            if (TEST_LIST_BROKEN) {
                std::cout << ":\n";
                for(int j = 0; j < mod.broken_test_names.len; ++j) {
                    std::cout << "    " << string_buffer_to_cstr(&mod.broken_test_names.data[j]) << '\n';
                }
            } else { std::cout << '\n'; }

            no_broken = false;
        }
        if (mod.skipped_test_names.len) {
            std::cout << YELLOW << "Skipped " << NC << mod.skipped_test_names.len << " test(s)";
            if (TEST_LIST_SKIPPED) {
                std::cout << ":\n";
                for(int j = 0; j < mod.skipped_test_names.len; ++j) {
                    std::cout << "    " << string_buffer_to_cstr(&mod.skipped_test_names.data[j]) << '\n';
                }
            } else { std::cout << '\n'; }
            no_skip = false;
        }
        if (mod.skipped_broken_test_names.len) {
            std::cout << YELLOW << "Skipped " << NC << mod.skipped_broken_test_names.len << " BROKEN test(s)";
            if (TEST_LIST_SKIPPED || TEST_LIST_BROKEN) {
                std::cout << ":\n";
                for(int j = 0; j < mod.skipped_broken_test_names.len; ++j) {
                    std::cout << "    " << string_buffer_to_cstr(&mod.skipped_broken_test_names.data[j]) << '\n';
                }
            } else { std::cout << '\n'; }
        }
    }

    // @Todo looping through everything again is lame but at least there is no branching
    // but since all of these are c++ way heap allocated this doestn really matter lol
    for(int i = 0; i < test->modules.len; ++i) {
        Test_Module *mod = &test->modules.data[i];
        for(int j = 0; j < mod->failed_test_names.len; ++j) {
            kill_heap_string_buffer(&mod->failed_test_names.data[j]);
        }
        for(int j = 0; j < mod->broken_test_names.len; ++j) {
            kill_heap_string_buffer(&mod->broken_test_names.data[j]);
        }
        for(int j = 0; j < mod->skipped_test_names.len; ++j) {
            kill_heap_string_buffer(&mod->skipped_test_names.data[j]);
        }
        for(int j = 0; j < mod->skipped_broken_test_names.len; ++j) {
            kill_heap_string_buffer(&mod->skipped_broken_test_names.data[j]);
        }
        kill_heap_string_buffer(&mod->info);
        kill_dyn_array<Heap_String_Buffer>(&mod->failed_test_names);
        kill_dyn_array<Heap_String_Buffer>(&mod->skipped_test_names);
        kill_dyn_array<Heap_String_Buffer>(&mod->broken_test_names);
        kill_dyn_array<Heap_String_Buffer>(&mod->skipped_broken_test_names);
    }
    kill_dyn_array<Test_Module>(&test->modules);

    if (!no_skip)
        std::cout << YELLOW << "TESTS WERE SKIPPED" << NC << '\n';
    else
        std::cout << GREEN << "NO TESTS SKIPPED" << NC << '\n';

    if (!no_broken)
        std::cout << YELLOW << "TESTS ARE BROKEN" << NC << '\n';
    else
        std::cout << GREEN << "NO BROKEN TESTS" << NC << '\n';

    if (!all_pass)
        std::cout << RED << "TESTS FAILED" << NC << '\n';
    else 
        std::cout << GREEN << "NO FAILURES" << NC << '\n';
}

void begin_test_module(const char *name, Test_Module *mod, const char *function_name,
        const char *file_name, bool broken, bool skipped) 
{
    const char *info_strings[] = {
        "[", function_name, ", ", file_name, "] ", name
    };

    mod->info = build_heap_string_buffer(6, info_strings);
    mod->skipped = skipped ? true : false;
    mod->broken = broken ? true : false;

    init_dyn_array<Heap_String_Buffer>(&mod->failed_test_names,  10);
    init_dyn_array<Heap_String_Buffer>(&mod->skipped_test_names, 10);
    init_dyn_array<Heap_String_Buffer>(&mod->broken_test_names,  10);
    init_dyn_array<Heap_String_Buffer>(&mod->skipped_broken_test_names, 10);
}
void end_test_module(Test_Module *mod) {}
#endif // #if TEST
