#include "localization/LocalizationManager.h"

#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace aeris {

LocalizationManager& LocalizationManager::getInstance() {
    static LocalizationManager instance;
    return instance;
}

static bool loadJsonTranslations(std::filesystem::path const& file, std::unordered_map<std::string, std::string>& out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) return false;

    std::ifstream in(file);
    if (!in.is_open()) return false;

    try {
        nlohmann::json j;
        in >> j;
        if (!j.is_object()) return false;

        for (auto const& [k, v] : j.items()) {
            if (v.is_string()) {
                out[k] = v.get<std::string>();
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool LocalizationManager::load(std::filesystem::path const& langDir, std::string const& languageCode) {
    mCurrentLanguage = languageCode;
    mTranslations.clear();
    mFallbackTranslations.clear();

    // 1. Siempre cargar inglés como fallback base
    auto enPath = langDir / "en_US.json";
    loadJsonTranslations(enPath, mFallbackTranslations);

    // 2. Cargar el idioma seleccionado
    if (languageCode != "en_US") {
        auto langPath = langDir / (languageCode + ".json");
        loadJsonTranslations(langPath, mTranslations);
    }

    return true;
}

std::string LocalizationManager::translate(std::string_view key) const {
    std::string k(key);

    // Buscar en idioma activo
    auto it = mTranslations.find(k);
    if (it != mTranslations.end()) {
        return it->second;
    }

    // Fallback a inglés
    auto fallbackIt = mFallbackTranslations.find(k);
    if (fallbackIt != mFallbackTranslations.end()) {
        return fallbackIt->second;
    }

    // Si no existe, devolver la clave tal cual
    return k;
}

} // namespace aeris
