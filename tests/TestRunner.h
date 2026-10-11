#pragma once

#include <iostream>

inline int gTestFailures = 0;
inline int gTestCount    = 0;

inline void check(bool condition, char const* description) {
    ++gTestCount;
    if (!condition) {
        std::cerr << "  [FAIL] " << description << '\n';
        ++gTestFailures;
    } else {
        std::cout << "  [OK]   " << description << '\n';
    }
}
