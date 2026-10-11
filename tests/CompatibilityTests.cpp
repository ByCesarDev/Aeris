#include "TestRunner.h"
#include "core/CompatibilityManager.h"

void runCompatibilityTests() {
    std::cout << "\nRunning CompatibilityManager Tests...\n";

    aeris::CompatibilityManager& mgr = aeris::CompatibilityManager::getInstance();
    mgr.reset();

    // 1. Estado inicial seguro
    check(!mgr.isVersionCompatible(), "Initial state must not assume version compatibility");
    check(!mgr.isMaterialRedirectionSupported(), "Initial state must not assume material redirection capability");
    check(!mgr.isHotReloadSupported(), "Initial state must not assume hot reload capability");
    check(!mgr.isGraphicsSwitchSupported(), "Initial state must not assume graphics switch capability");
    check(mgr.detectedVersion() == "Unknown", "Initial detected version must be Unknown");

    check(mgr.getCapabilityState(aeris::FeatureCapability::MaterialRedirection) == aeris::CapabilityState::Unknown,
        "Initial material redirection state must be Unknown");
    check(mgr.getCapabilityState(aeris::FeatureCapability::MaterialHotReload) == aeris::CapabilityState::Unknown,
        "Initial material hot reload state must be Unknown");
    check(mgr.getCapabilityState(aeris::FeatureCapability::GraphicsModeSwitch) == aeris::CapabilityState::Unknown,
        "Initial graphics mode switch state must be Unknown");

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
    check(!mgr.isMaterialRedirectionSupported(), "Material redirection must be disabled on incompatible version");
    check(mgr.getCapabilityState(aeris::FeatureCapability::MaterialRedirection) == aeris::CapabilityState::Unsupported,
        "Incompatible version must set material redirection to Unsupported");
    check(mgr.getCapabilityState(aeris::FeatureCapability::MaterialHotReload) == aeris::CapabilityState::Unsupported,
        "Incompatible version must set material hot reload to Unsupported");

    // 5. Inicialización con versión objetivo compatible
    mgr.initialize("1.26.51.01", "26.51.5");
    check(mgr.isVersionCompatible(), "Initializing with 1.26.51.01 must set isVersionCompatible to true");
    check(mgr.detectedVersion() == "1.26.51.01", "Detected version must match 1.26.51.01");
    check(!mgr.isMaterialRedirectionSupported(), "Material redirection must start unverified on target version");
    check(!mgr.isHotReloadSupported(), "Hot reload must start unverified on target version");
    check(mgr.getCapabilityState(aeris::FeatureCapability::MaterialRedirection) == aeris::CapabilityState::Unknown,
        "Target version initialization must mark capabilities as Unknown awaiting runtime check");

    // 6. Transición y registro de estados de capacidades
    mgr.setCapabilityState(aeris::FeatureCapability::MaterialRedirection, aeris::CapabilityState::Supported, "Signature verified");
    check(mgr.isMaterialRedirectionSupported(), "Setting capability to Supported must reflect in boolean check");
    check(mgr.getCapabilityState(aeris::FeatureCapability::MaterialRedirection) == aeris::CapabilityState::Supported,
        "CapabilityState must be Supported");
    check(mgr.getCapabilityReason(aeris::FeatureCapability::MaterialRedirection) == "Signature verified",
        "Capability reason must match set explanation");

    check(aeris::capabilityStateToString(aeris::CapabilityState::Supported) == "Supported", "Supported string check");
    check(aeris::capabilityStateToString(aeris::CapabilityState::Unsupported) == "Unsupported", "Unsupported string check");
    check(aeris::capabilityStateToString(aeris::CapabilityState::Unknown) == "Unknown", "Unknown string check");

        // 7. Normalizacion de versiones con sufijo git/build (1.26.51+0559ac5)
    mgr.initialize("1.26.51+0559ac5", "26.51.5");
    check(mgr.isVersionCompatible(), "Version 1.26.51+0559ac5 must be recognized as compatible");
    check(mgr.detectedVersion() == "1.26.51.01", "1.26.51+0559ac5 must normalize to 1.26.51.01");
    check(mgr.rawDetectedVersion() == "1.26.51+0559ac5", "rawDetectedVersion must retain exact engine build");

    mgr.reset();
}
