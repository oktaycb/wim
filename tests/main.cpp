// wimtests - entry point. Suites register via WIM_TEST in their own
// translation units; this file only runs them.
#include "test.h"

#include <cstring>

// Defined in tests/test_conpty.cpp: runs the live pseudoconsole scenarios
// (this binary re-launched detached) and returns the failure count.
int wimConPtyChildMain();

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--conpty-child") == 0)
            return wimConPtyChildMain();
    }
    return ::wimtest::runAll();
}
