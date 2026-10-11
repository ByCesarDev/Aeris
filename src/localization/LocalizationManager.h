#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aeris {

class LocalizationManager {
public:
    static LocalizationManager& getInstance();

    bool load(std::filesystem::path const& langDir, std::string const& languageCode);
    std::string translate(std::string_view key) const;

    std::string const& currentLanguage() const { return mCurrentLanguage; }

private:
    LocalizationManager() = default;

    std::string                                          mCurrentLanguage = "es_ES";
    std::unordered_map<std::string, std::string>        mTranslations;
    std::unordered_map<std::string, std::string>        mFallbackTranslations;
};

inline std::string tr(std::string_view key) {
    return LocalizationManager::getInstance().translate(key);
}

} // namespace aeris
