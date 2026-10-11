#include "TestRunner.h"
#include "core/ConfigManager.h"

#include <filesystem>
#include <fstream>

void runConfigTests() {
    std::cout << "\nRunning ConfigManager Tests...\n";

    aeris::ConfigManager& mgr = aeris::ConfigManager::getInstance();
    mgr.reset();

    // 1. Configuración por defecto es válida
    std::string err;
    check(aeris::ConfigManager::validate(mgr.config(), err), "Default config must be valid");

    // 2. Validación de versiones inválidas
    aeris::Config invalidCfg = mgr.config();
    invalidCfg.configVersion = 999;
    check(!aeris::ConfigManager::validate(invalidCfg, err), "Config with unsupported version must fail");

    // 3. Validación de idioma inválido
    invalidCfg = mgr.config();
    invalidCfg.language = "fr_FR";
    check(!aeris::ConfigManager::validate(invalidCfg, err), "Unsupported language must fail");

    // 4. Validación de path traversal en carpetas
    invalidCfg = mgr.config();
    invalidCfg.dataDir = "../outside";
    check(!aeris::ConfigManager::validate(invalidCfg, err), "Path traversal in dataDir must fail");

    invalidCfg = mgr.config();
    invalidCfg.shaderPacksDir = "..\\evil";
    check(!aeris::ConfigManager::validate(invalidCfg, err), "Path traversal in shaderPacksDir must fail");

    // 5. Validación de backdrop fuera de rango
    invalidCfg = mgr.config();
    invalidCfg.ui.backdrop = 1.5f;
    check(!aeris::ConfigManager::validate(invalidCfg, err), "Backdrop > 1.0 must fail");

    invalidCfg.ui.backdrop = -0.1f;
    check(!aeris::ConfigManager::validate(invalidCfg, err), "Backdrop < 0.0 must fail");

    // 6. Transaccionalidad: un JSON inválido no debe alterar la configuración existente
    auto testDir = std::filesystem::temp_directory_path() / "aeris_test_config";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);
    auto testFile = testDir / "bad_config.json";

    {
        std::ofstream out(testFile);
        out << "{ \"configVersion\": 999, \"language\": \"invalid\" }";
    }

    aeris::Config original = mgr.config();
    bool loadBadResult = mgr.load(testFile);
    check(!loadBadResult, "Loading bad config must return false");
    check(mgr.config().language == original.language, "Config must remain untouched when load fails");

    // 7. Guardado y recarga exitosa
    auto goodFile = testDir / "good_config.json";
    mgr.config().language = "en_US";
    mgr.config().ui.shortcut = "H";
    check(mgr.load(goodFile) || mgr.save(), "Save/init good config must succeed");
    check(mgr.load(goodFile), "Reload good config must succeed");
    check(mgr.config().language == "en_US", "Loaded config must match saved values");
    check(mgr.config().ui.shortcut == "H", "Loaded shortcut must match saved shortcut");

    // 8. Modo seguro y ganchos experimentales desactivados por defecto
    check(mgr.config().safety.safeMode == true, "Safe mode must be enabled by default");
    check(mgr.config().safety.enableExperimentalHooks == false, "Experimental hooks must be disabled by default");
    check(mgr.config().safety.enablePassiveReadHook == false, "Passive read hook must be disabled by default");

    std::filesystem::remove_all(testDir);
}
