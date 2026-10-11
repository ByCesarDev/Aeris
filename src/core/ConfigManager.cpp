#include "core/ConfigManager.h"

#include <fstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace aeris {

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::load(std::filesystem::path const& configFile) {
    mConfigPath = configFile;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(mConfigPath, ec)) {
        return save(); // Guardar configuración por defecto si no existe
    }

    std::ifstream file(mConfigPath);
    if (!file.is_open()) return false;

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("configVersion")) mConfig.configVersion = j["configVersion"].get<int>();
        if (j.contains("enabled")) mConfig.enabled = j["enabled"].get<bool>();
        if (j.contains("language")) mConfig.language = j["language"].get<std::string>();
        if (j.contains("dataDir")) mConfig.dataDir = j["dataDir"].get<std::string>();
        if (j.contains("shaderPacksDir")) mConfig.shaderPacksDir = j["shaderPacksDir"].get<std::string>();
        if (j.contains("generatedDir")) mConfig.generatedDir = j["generatedDir"].get<std::string>();

        if (j.contains("ui") && j["ui"].is_object()) {
            auto const& ui = j["ui"];
            if (ui.contains("shortcut")) mConfig.ui.shortcut = ui["shortcut"].get<std::string>();
            if (ui.contains("backdrop")) mConfig.ui.backdrop = ui["backdrop"].get<float>();
            if (ui.contains("theme")) mConfig.ui.theme = ui["theme"].get<std::string>();
        }

        if (j.contains("shaders") && j["shaders"].is_object()) {
            auto const& s = j["shaders"];
            if (s.contains("activePack")) mConfig.shaders.activePack = s["activePack"].get<std::string>();
            if (s.contains("hotReload")) mConfig.shaders.hotReload = s["hotReload"].get<bool>();
        }

        if (j.contains("performance") && j["performance"].is_object()) {
            auto const& p = j["performance"];
            if (p.contains("diagnosticsEnabled")) mConfig.performance.diagnosticsEnabled = p["diagnosticsEnabled"].get<bool>();
        }

        if (j.contains("safety") && j["safety"].is_object()) {
            auto const& s = j["safety"];
            if (s.contains("safeMode")) mConfig.safety.safeMode = s["safeMode"].get<bool>();
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool ConfigManager::save() const {
    if (mConfigPath.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(mConfigPath.parent_path(), ec);

    nlohmann::json j;
    j["configVersion"] = mConfig.configVersion;
    j["enabled"]       = mConfig.enabled;
    j["language"]      = mConfig.language;
    j["dataDir"]       = mConfig.dataDir;
    j["shaderPacksDir"] = mConfig.shaderPacksDir;
    j["generatedDir"]  = mConfig.generatedDir;

    j["ui"] = {
        {"shortcut", mConfig.ui.shortcut},
        {"backdrop", mConfig.ui.backdrop},
        {"theme", mConfig.ui.theme}
    };

    j["shaders"] = {
        {"activePack", mConfig.shaders.activePack},
        {"hotReload", mConfig.shaders.hotReload}
    };

    j["performance"] = {
        {"diagnosticsEnabled", mConfig.performance.diagnosticsEnabled}
    };

    j["safety"] = {
        {"safeMode", mConfig.safety.safeMode}
    };

    // Escritura atómica mediante archivo temporal
    auto tempPath = mConfigPath;
    tempPath += ".tmp";

    std::ofstream file(tempPath);
    if (!file.is_open()) return false;
    file << j.dump(4);
    file.close();

    std::filesystem::rename(tempPath, mConfigPath, ec);
    return !ec;
}

} // namespace aeris
