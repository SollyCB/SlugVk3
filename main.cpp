#include "allocator.hpp"
#include "print.hpp"
#include "spirv.hpp"
#include "gltf.hpp"

#if TEST
   #include "test.hpp"
   void run_tests(); 
#endif

int main() {
    init_allocators();

#if TEST
    run_tests();
#endif

    println("Hello Sailor");

    kill_allocators();
    return 0;
}

#if TEST
void run_tests() {
    load_tests();

    test_spirv();
    test_gltf();

    end_tests();
}
#endif
