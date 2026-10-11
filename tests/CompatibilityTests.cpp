#include "TestRunner.h"
#include "core/CompatibilityManager.h"

void runCompatibilityTests() {
    std::cout << "\nRunning CompatibilityManager Tests...\n";

    aeris::CompatibilityManager& mgr = aeris::CompatibilityManager::getInstance();
    mgr.reset();

    // 1. Estado inicial seguro
    check(!mgr.isVersionCompatible(), "Initial state must not assume version compatibility");
    check(!mgr.isHotReloadSupported(), "Initial state must not assume hot reload capability");
    check(!mgr.isGraphicsSwitchSupported(), "Initial state must not assume graphics switch capability");
    check(mgr.detectedVersion() == "Unknown", "Initial detected version must be Unknown");

    // 2. Coincidencia de versiones
    check(aeris::CompatibilityManager::isVersionMatching("1.26.51.01"), "Exact target 1.26.51.01 must match");
    check(aeris::CompatibilityManager::isVersionMatching("1.26.51.1"), "Prefix 1.26.51 must match");
    check(aeris::CompatibilityManager::isVersionMatching("26.51.5"), "SDK version 26.51.5 must match");

    // 3. Versiones incompatibles deben ser rechazadas
    check(!aeris::CompatibilityManager::isVersionMatching("1.26.40"), "Older 1.26.40 must not match target");
    check(!aeris::CompatibilityManager::isVersionMatching("1.26.30"), "Older 1.26.30 must not match target");
    check(!aeris::CompatibilityManager::isVersionMatching(""), "Empty version must not match");
    check(!aeris::CompatibilityManager::isVersionMatching("Unknown"), "Unknown version must not match");

    // 4. Inicialización con versión incompatible
    mgr.initialize("1.26.40", "26.40.1");
    check(!mgr.isVersionCompatible(), "Initializing with 1.26.40 must set isVersionCompatible to false");
    check(mgr.detectedVersion() == "1.26.40", "Detected version must reflect real passed version");
    check(!mgr.isHotReloadSupported(), "Hot reload must remain disabled on incompatible version");

    // 5. Inicialización con versión objetivo compatible
    mgr.initialize("1.26.51.01", "26.51.5");
    check(mgr.isVersionCompatible(), "Initializing with 1.26.51.01 must set isVersionCompatible to true");
    check(mgr.detectedVersion() == "1.26.51.01", "Detected version must match 1.26.51.01");
}
