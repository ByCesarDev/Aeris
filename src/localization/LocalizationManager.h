#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aeris {

class LocalizationManager {
public:
    static LocalizationManager& getInstance();

    bool load(std::filesystem::path const& langDir, std::string const& languageCode);

    // Traducción simple por clave
    std::string translate(std::string_view key) const;

    // Traducción con sustitución de parámetros posicionales {0}, {1}, etc.
    std::string format(std::string_view key, std::initializer_list<std::string> args) const;

    std::string const& currentLanguage() const { return mCurrentLanguage; }
    void clear();

private:
    LocalizationManager() = default;

    std::string                                  mCurrentLanguage = "es_ES";
    std::unordered_map<std::string, std::string> mTranslations;
    std::unordered_map<std::string, std::string> mFallbackTranslations;
};

inline std::string tr(std::string_view key) {
    return LocalizationManager::getInstance().translate(key);
}

inline std::string trFormat(std::string_view key, std::initializer_list<std::string> args) {
    return LocalizationManager::getInstance().format(key, args);
}

} // namespace aeris
