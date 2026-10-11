#include "TestRunner.h"
#include "core/CompatibilityManager.h"
#include "core/SignatureVerifier.h"

void runCompatibilityTests() {
    std::cout << "\nRunning CompatibilityManager & SignatureVerifier Tests...\n";

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
    check(aeris::CompatibilityManager::isVersionMatching("26.51.6"), "SDK version 26.51.6 must match");
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
    mgr.initialize("1.26.51.01", "26.51.6");
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

    // 7. Normalización de versiones con sufijo git/build (1.26.51+0559ac5)
    mgr.initialize("1.26.51+0559ac5", "26.51.6");
    check(mgr.isVersionCompatible(), "Version 1.26.51+0559ac5 must be recognized as compatible");
    check(mgr.detectedVersion() == "1.26.51.01", "1.26.51+0559ac5 must normalize to 1.26.51.01");
    check(mgr.rawDetectedVersion() == "1.26.51+0559ac5", "rawDetectedVersion must retain exact engine build");

    // 8. Diagnósticos de firmas nativas en CompatibilityManager
    aeris::SignatureDiagnostic testDiag;
    testDiag.name           = "readFile";
    testDiag.status         = aeris::SignatureStatus::Verified;
    testDiag.address        = 0x1402cb260ULL;
    testDiag.rva            = 0x2cb260ULL;
    testDiag.matchCount     = 1;
    testDiag.matchedPattern = "55 41 57 ?";
    testDiag.details        = "Unique verified match";
    mgr.setSignatureDiagnostic("readFile", testDiag);

    auto retrievedDiag = mgr.getSignatureDiagnostic("readFile");
    check(retrievedDiag.has_value(), "getSignatureDiagnostic must find registered diagnostic");
    check(retrievedDiag->status == aeris::SignatureStatus::Verified, "Diagnostic status must match Verified");
    check(retrievedDiag->rva == 0x2cb260ULL, "Diagnostic RVA must match 0x2cb260");
    check(retrievedDiag->matchCount == 1, "Diagnostic matchCount must be 1");
    check(!mgr.getSignatureDiagnostic("nonExistent").has_value(), "Unregistered diagnostic must return nullopt");
    check(mgr.getAllSignatureDiagnostics().size() == 1, "getAllSignatureDiagnostics must return 1 element");

    // 9. Pruebas de SignatureVerifier::parsePattern
    auto parsed = aeris::SignatureVerifier::parsePattern("55 41 57 ? 56 ?? 48 89");
    check(parsed.size() == 8, "Parsed pattern must have 8 tokens");
    check(parsed[0].byte == 0x55 && !parsed[0].isWildcard, "Token 0 must be 0x55 literal");
    check(parsed[1].byte == 0x41 && !parsed[1].isWildcard, "Token 1 must be 0x41 literal");
    check(parsed[2].byte == 0x57 && !parsed[2].isWildcard, "Token 2 must be 0x57 literal");
    check(parsed[3].isWildcard, "Token 3 ('?') must be wildcard");
    check(parsed[4].byte == 0x56 && !parsed[4].isWildcard, "Token 4 must be 0x56 literal");
    check(parsed[5].isWildcard, "Token 5 ('??') must be wildcard");
    check(parsed[6].byte == 0x48 && !parsed[6].isWildcard, "Token 6 must be 0x48 literal");
    check(parsed[7].byte == 0x89 && !parsed[7].isWildcard, "Token 7 must be 0x89 literal");

    // 10. Pruebas de SignatureVerifier::scanSpan
    std::vector<uint8_t> mockBuffer = {
        0x90, 0x90, 0x55, 0x41, 0x57, 0xAA, 0x56, 0xBB, 0x48, 0x89, 0x90, 0x90,
        0x55, 0x41, 0x57, 0xCC, 0x56, 0xDD, 0x48, 0x89, 0x90
    };

    auto singlePattern = aeris::SignatureVerifier::parsePattern("55 41 57 AA");
    auto singleMatches = aeris::SignatureVerifier::scanSpan(mockBuffer, singlePattern, 0x1000);
    check(singleMatches.size() == 1, "Single pattern must produce exactly 1 match");
    check(singleMatches[0] == 0x1002, "Single pattern match address must be 0x1002 (offset 2)");

    auto ambiguousPattern = aeris::SignatureVerifier::parsePattern("55 41 57 ? 56");
    auto ambMatches = aeris::SignatureVerifier::scanSpan(mockBuffer, ambiguousPattern, 0x1000);
    check(ambMatches.size() == 2, "Ambiguous pattern must produce exactly 2 matches");
    check(ambMatches[0] == 0x1002, "First match at 0x1002");
    check(ambMatches[1] == 0x100C, "Second match at 0x100C");

    auto missingPattern = aeris::SignatureVerifier::parsePattern("FF FF FF FF");
    auto missingMatches = aeris::SignatureVerifier::scanSpan(mockBuffer, missingPattern, 0x1000);
    check(missingMatches.empty(), "Missing pattern must return 0 matches");

    // 11. Pruebas de SignatureVerifier::verifyMemoryRegion
    aeris::MemoryRegionInfo validRegion{0x1000, 0x2000, true, true};
    aeris::MemoryRegionInfo nonExecRegion{0x1000, 0x2000, true, false};
    aeris::MemoryRegionInfo nonCommittedRegion{0x1000, 0x2000, false, true};

    check(aeris::SignatureVerifier::verifyMemoryRegion(0x1500, validRegion) == aeris::SignatureStatus::Verified,
        "In-bounds committed executable address must be Verified");
    check(aeris::SignatureVerifier::verifyMemoryRegion(0x0500, validRegion) == aeris::SignatureStatus::InvalidMemoryRegion,
        "Address below base must be InvalidMemoryRegion");
    check(aeris::SignatureVerifier::verifyMemoryRegion(0x3500, validRegion) == aeris::SignatureStatus::InvalidMemoryRegion,
        "Address above limit must be InvalidMemoryRegion");
    check(aeris::SignatureVerifier::verifyMemoryRegion(0x1500, nonExecRegion) == aeris::SignatureStatus::InvalidMemoryRegion,
        "Non-executable memory region must be rejected as InvalidMemoryRegion");
    check(aeris::SignatureVerifier::verifyMemoryRegion(0x1500, nonCommittedRegion) == aeris::SignatureStatus::InvalidMemoryRegion,
        "Non-committed memory region must be rejected as InvalidMemoryRegion");

    // 12. Pruebas de SignatureVerifier::evaluate (Diagnósticos completos)
    auto evalIncompatible = aeris::SignatureVerifier::evaluate(1, 0x1500, "55 41", validRegion, false);
    check(evalIncompatible.status == aeris::SignatureStatus::IncompatibleVersion,
        "Incompatible version must evaluate to IncompatibleVersion status");

    auto evalNotFound = aeris::SignatureVerifier::evaluate(0, 0, "55 41", validRegion, true);
    check(evalNotFound.status == aeris::SignatureStatus::NotFound,
        "0 matches must evaluate to NotFound status");

    auto evalAmbiguous = aeris::SignatureVerifier::evaluate(2, 0x1002, "55 41", validRegion, true);
    check(evalAmbiguous.status == aeris::SignatureStatus::Ambiguous,
        "Multiple matches must evaluate to Ambiguous status");

    auto evalInvalidMem = aeris::SignatureVerifier::evaluate(1, 0x9999, "55 41", validRegion, true);
    check(evalInvalidMem.status == aeris::SignatureStatus::InvalidMemoryRegion,
        "Out of bounds single match must evaluate to InvalidMemoryRegion status");

    auto evalVerified = aeris::SignatureVerifier::evaluate(1, 0x1500, "55 41", validRegion, true);
    check(evalVerified.status == aeris::SignatureStatus::Verified,
        "Unique match in valid executable region must evaluate to Verified status");
    check(evalVerified.matchCount == 1, "Verified match count must be 1");
    check(evalVerified.address == 0x1500, "Verified address must be 0x1500");

    // 13. Conversión de enum a string
    check(aeris::signatureStatusToString(aeris::SignatureStatus::Verified) == "Verified", "Verified string");
    check(aeris::signatureStatusToString(aeris::SignatureStatus::NotFound) == "NotFound", "NotFound string");
    check(aeris::signatureStatusToString(aeris::SignatureStatus::Ambiguous) == "Ambiguous", "Ambiguous string");
    check(aeris::signatureStatusToString(aeris::SignatureStatus::InvalidMemoryRegion) == "InvalidMemoryRegion", "InvalidMemoryRegion string");
    check(aeris::signatureStatusToString(aeris::SignatureStatus::IncompatibleVersion) == "IncompatibleVersion", "IncompatibleVersion string");
    check(aeris::signatureStatusToString(aeris::SignatureStatus::DisabledBySafety) == "DisabledBySafety", "DisabledBySafety string");

    mgr.reset();
}
