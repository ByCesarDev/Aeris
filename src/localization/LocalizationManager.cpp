#include "localization/LocalizationManager.h"

#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace aeris {

LocalizationManager& LocalizationManager::getInstance() {
    static LocalizationManager instance;
    return instance;
}

void LocalizationManager::clear() {
    mTranslations.clear();
    mFallbackTranslations.clear();
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
    clear();

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

    // 1. Buscar en idioma activo
    auto it = mTranslations.find(k);
    if (it != mTranslations.end()) {
        return it->second;
    }

    // 2. Fallback a inglés
    auto fallbackIt = mFallbackTranslations.find(k);
    if (fallbackIt != mFallbackTranslations.end()) {
        return fallbackIt->second;
    }

    // 3. Clave tal cual si no se encuentra
    return k;
}

std::string LocalizationManager::format(std::string_view key, std::initializer_list<std::string> args) const {
    std::string text = translate(key);
    int index = 0;
    for (auto const& arg : args) {
        std::string placeholder = "{" + std::to_string(index) + "}";
        size_t pos = 0;
        while ((pos = text.find(placeholder, pos)) != std::string::npos) {
            text.replace(pos, placeholder.length(), arg);
            pos += arg.length();
        }
        ++index;
    }
    return text;
}

} // namespace aeris
