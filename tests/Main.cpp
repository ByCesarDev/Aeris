#include "TestRunner.h"

void runConfigTests();
void runCompatibilityTests();
void runLocalizationTests();
void runPerformanceTests();
void runShaderPackTests();
void runMaterialLoaderTests();

int main() {
    std::cout << "========================================\n";
    std::cout << "        AERIS UNIT TEST SUITE           \n";
    std::cout << "========================================\n";

    runConfigTests();
    runCompatibilityTests();
    runLocalizationTests();
    runPerformanceTests();
    runShaderPackTests();
    runMaterialLoaderTests();

    std::cout << "\n========================================\n";
    std::cout << "Results: " << gTestCount << " ran, " << gTestFailures << " failed.\n";
    std::cout << "========================================\n";

    return gTestFailures == 0 ? 0 : 1;
}
