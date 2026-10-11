#include "TestRunner.h"
#include "localization/LocalizationManager.h"

#include <filesystem>
#include <fstream>

void runLocalizationTests() {
    std::cout << "\nRunning LocalizationManager Tests...\n";

    aeris::LocalizationManager& mgr = aeris::LocalizationManager::getInstance();
    mgr.clear();

    auto testDir = std::filesystem::temp_directory_path() / "aeris_test_lang";
    std::filesystem::create_directories(testDir);

    // Crear en_US.json
    {
        std::ofstream en(testDir / "en_US.json");
        en << "{\n"
           << "  \"test.greeting\": \"Hello\",\n"
           << "  \"test.only_in_en\": \"English Only\",\n"
           << "  \"test.format\": \"User {0} has {1} shaders\"\n"
           << "}\n";
    }

    // Crear es_ES.json
    {
        std::ofstream es(testDir / "es_ES.json");
        es << "{\n"
           << "  \"test.greeting\": \"Hola\",\n"
           << "  \"test.format\": \"El usuario {0} tiene {1} shaders\"\n"
           << "}\n";
    }

    mgr.load(testDir, "es_ES");

    // 1. Clave presente en español
    check(mgr.translate("test.greeting") == "Hola", "Key in Spanish must return Spanish translation");

    // 2. Fallback automático a inglés cuando falta en español
    check(mgr.translate("test.only_in_en") == "English Only", "Missing key in Spanish must fall back to English");

    // 3. Clave inexistente en ambos debe devolver la clave
    check(mgr.translate("test.nonexistent") == "test.nonexistent", "Unknown key must return key literal");

    // 4. Formateo con parámetros posicionales
    std::string formatted = mgr.format("test.format", {"Cesar", "5"});
    check(formatted == "El usuario Cesar tiene 5 shaders", "Positional argument formatting must substitute correctly");

    std::filesystem::remove_all(testDir);
}
