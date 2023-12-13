#ifndef SOL_assert_H_INCLUDE_GUARD_
#define SOL_assert_H_INCLUDE_GUARD_

#include "print.h"

#if DEBUG
#if _WIN32

#define assert(x) \
    if (!(x)) {println("\n    [file: %s, line: %i, fn %s]\n        ** ASSERTION FAILED **: %s\n", __FILE__, __LINE__, __FUNCTION__, #x); __debugbreak;}

#else

#define assert(x) \
    if (!(x)) {println("\n    [file: %s, line: %i, fn %s]\n        ** ASSERTION FAILED **: %s\n", __FILE__, __LINE__, __FUNCTION__, #x); asm("int $3");}

#endif // _WIN32 or not

#else

#define assert(x)

#endif // if DEBUG

#endif // include guard

/* Old assert code. LOL I was such a noob just a few weeks ago...

#include <iostream> // i really dont want iostream but idk how else to print __file__ and __function__
#include "print.h"

#ifndef _WIN32
    #define HALT_EXECUTION() __builtin_trap()
#else
    #define HALT_EXECUTION() abort() // @Todo get rid of abort on windows
#endif

#if DEBUG
    #define assert(predicate, fmt) if (!(predicate)) { \
        std::cout << "Assert Failed in " << __FILE__ << ", " << __FUNCTION__ << "(...), Line " \
        << __LINE__ << ": " << #predicate << ", " << fmt << '\n'; \
        HALT_EXECUTION(); \
    }
#else

    #define assert(predicate, fmt, ...) {}

#endif
*/

