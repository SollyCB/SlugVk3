#ifndef SOL_ASSERT_H_INCLUDE_GUARD_
#define SOL_ASSERT_H_INCLUDE_GUARD_

#include <iostream> // i really dont want iostream but idk how else to print __file__ and __function__

#ifndef _WIN32
    #define HALT_EXECUTION() __builtin_trap()
#else
    #define HALT_EXECUTION() abort() // @Todo get rid of abort on windows
#endif

#if DEBUG 
    #define ASSERT(predicate, fmt) if (!(predicate)) { \
        std::cout << "Assert Failed in " << __FILE__ << ", " << __FUNCTION__ << "(...), Line " \
        << __LINE__ << ": " << #predicate << ", " << fmt << '\n'; \
        HALT_EXECUTION(); \
    }
#else

    #define ASSERT(predicate, fmt, ...) {}

#endif

#endif // include guard
