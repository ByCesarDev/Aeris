#include "TestRunner.h"

void runConfigTests();
void runCompatibilityTests();
void runLocalizationTests();
void runPerformanceTests();

int main() {
    std::cout << "========================================\n";
    std::cout << "        AERIS UNIT TEST SUITE           \n";
    std::cout << "========================================\n";

    runConfigTests();
    runCompatibilityTests();
    runLocalizationTests();
    runPerformanceTests();

    std::cout << "\n========================================\n";
    std::cout << "Results: " << gTestCount << " ran, " << gTestFailures << " failed.\n";
    std::cout << "========================================\n";

    return gTestFailures == 0 ? 0 : 1;
}
