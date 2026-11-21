// wimtests - entry point. Suites register via WIM_TEST in their own
// translation units; this file only runs them.
#include "test.h"

int main()
{
    return ::wimtest::runAll();
}
