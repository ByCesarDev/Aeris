#include "TestRunner.h"
#include "optimization/PerformanceManager.h"

void runPerformanceTests() {
    std::cout << "\nRunning PerformanceManager Tests...\n";

    aeris::PerformanceManager& mgr = aeris::PerformanceManager::getInstance();
    mgr.reset();

    // 1. Debe iniciar sin mediciones ficticias
    check(!mgr.hasMeasurement(), "PerformanceManager must not have measurement upon initialization");
    check(!mgr.currentFps().has_value(), "currentFps must be nullopt before measurement");
    check(!mgr.currentFrameTimeMs().has_value(), "currentFrameTimeMs must be nullopt before measurement");

    // 2. Control de diagnósticos
    check(mgr.diagnosticsEnabled(), "Diagnostics should be enabled by default");
    mgr.setDiagnosticsEnabled(false);
    check(!mgr.diagnosticsEnabled(), "Diagnostics disabled state must be respected");

    // Re-habilitar y resetear
    mgr.setDiagnosticsEnabled(true);
    mgr.reset();
    check(!mgr.hasMeasurement(), "Reset must clear all metrics");
}
