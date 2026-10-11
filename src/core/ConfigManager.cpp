#include "core/ConfigManager.h"

#include <chrono>
#include <fstream>
#include <system_error>
#include <thread>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace aeris {

namespace {

bool isSafeRelativePath(std::string_view path) {
    if (path.empty()) return false;
    // No permitir secuencias de escape a directorios superiores
    if (path.find("..") != std::string_view::npos) return false;
    // No permitir rutas absolutas (con /, \, o unidad C:)
    if (path.front() == '/' || path.front() == '\\') return false;
    if (path.size() >= 2 && path[1] == ':') return false;
    return true;
}

} // namespace

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::reset() {
    mConfig    = Config{};
    mLastError.clear();
}

bool ConfigManager::validate(Config const& cfg, std::string& outError) {
    if (cfg.configVersion != 1) {
        outError = "Unsupported config version: " + std::to_string(cfg.configVersion);
        return false;
    }

    if (cfg.language != "es_ES" && cfg.language != "en_US") {
        outError = "Unsupported language: " + cfg.language + " (supported: es_ES, en_US)";
        return false;
    }

    if (!isSafeRelativePath(cfg.dataDir)) {
        outError = "Invalid or unsafe dataDir path: " + cfg.dataDir;
        return false;
    }
    if (!isSafeRelativePath(cfg.shaderPacksDir)) {
        outError = "Invalid or unsafe shaderPacksDir path: " + cfg.shaderPacksDir;
        return false;
    }
    if (!isSafeRelativePath(cfg.generatedDir)) {
        outError = "Invalid or unsafe generatedDir path: " + cfg.generatedDir;
        return false;
    }

    if (cfg.ui.backdrop < 0.0f || cfg.ui.backdrop > 1.0f) {
        outError = "ui.backdrop out of range [0.0, 1.0]: " + std::to_string(cfg.ui.backdrop);
        return false;
    }
    if (cfg.ui.shortcut.empty() || cfg.ui.shortcut.size() > 16) {
        outError = "ui.shortcut invalid length";
        return false;
    }
    if (cfg.ui.theme != "dark" && cfg.ui.theme != "light") {
        outError = "Unsupported ui.theme: " + cfg.ui.theme;
        return false;
    }

    if (!cfg.shaders.activePack.empty() && !isSafeRelativePath(cfg.shaders.activePack)) {
        outError = "Invalid activePack name or path traversal detected: " + cfg.shaders.activePack;
        return false;
    }

    return true;
}

bool ConfigManager::load(std::filesystem::path const& configFile) {
    mConfigPath = configFile;
    mLastError.clear();

    std::error_code ec;
    if (!std::filesystem::is_regular_file(mConfigPath, ec)) {
        // Si el archivo no existe, guardamos la configuración por defecto
        return save();
    }

    std::ifstream file(mConfigPath);
    if (!file.is_open()) {
        mLastError = "Could not open config file: " + mConfigPath.string();
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (std::exception const& e) {
        mLastError = std::string("Malformed JSON in config: ") + e.what();
        return false;
    }

    if (!j.is_object()) {
        mLastError = "Config JSON root must be an object";
        return false;
    }

    // Cargar en objeto candidato temporal para garantizar transaccionalidad
    Config candidate = mConfig;

    try {
        if (j.contains("configVersion")) candidate.configVersion = j["configVersion"].get<int>();
        if (j.contains("enabled"))       candidate.enabled       = j["enabled"].get<bool>();
        if (j.contains("language"))      candidate.language      = j["language"].get<std::string>();
        if (j.contains("dataDir"))       candidate.dataDir       = j["dataDir"].get<std::string>();
        if (j.contains("shaderPacksDir")) candidate.shaderPacksDir = j["shaderPacksDir"].get<std::string>();
        if (j.contains("generatedDir"))  candidate.generatedDir  = j["generatedDir"].get<std::string>();

        if (j.contains("ui") && j["ui"].is_object()) {
            auto const& ui = j["ui"];
            if (ui.contains("shortcut")) candidate.ui.shortcut = ui["shortcut"].get<std::string>();
            if (ui.contains("backdrop")) candidate.ui.backdrop = ui["backdrop"].get<float>();
            if (ui.contains("theme"))    candidate.ui.theme    = ui["theme"].get<std::string>();
        }

        if (j.contains("shaders") && j["shaders"].is_object()) {
            auto const& s = j["shaders"];
            if (s.contains("activePack")) candidate.shaders.activePack = s["activePack"].get<std::string>();
            if (s.contains("hotReload"))  candidate.shaders.hotReload  = s["hotReload"].get<bool>();
        }

        if (j.contains("performance") && j["performance"].is_object()) {
            auto const& p = j["performance"];
            if (p.contains("diagnosticsEnabled")) {
                candidate.performance.diagnosticsEnabled = p["diagnosticsEnabled"].get<bool>();
            }
        }

        if (j.contains("safety") && j["safety"].is_object()) {
            auto const& s = j["safety"];
            if (s.contains("safeMode")) candidate.safety.safeMode = s["safeMode"].get<bool>();
        }
    } catch (std::exception const& e) {
        mLastError = std::string("Type error reading config fields: ") + e.what();
        return false;
    }

    // Validación completa del candidato antes de mutar el estado
    std::string valError;
    if (!validate(candidate, valError)) {
        mLastError = "Validation failed: " + valError;
        return false;
    }

    // Aplicar únicamente cuando la validación sea totalmente correcta
    mConfig = candidate;
    return true;
}

bool ConfigManager::save() const {
    if (mConfigPath.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(mConfigPath.parent_path(), ec);

    nlohmann::json j;
    j["configVersion"]   = mConfig.configVersion;
    j["enabled"]         = mConfig.enabled;
    j["language"]        = mConfig.language;
    j["dataDir"]         = mConfig.dataDir;
    j["shaderPacksDir"]  = mConfig.shaderPacksDir;
    j["generatedDir"]    = mConfig.generatedDir;

    j["ui"] = {
        {"shortcut", mConfig.ui.shortcut},
        {"backdrop", mConfig.ui.backdrop},
        {"theme",    mConfig.ui.theme}
    };

    j["shaders"] = {
        {"activePack", mConfig.shaders.activePack},
        {"hotReload",  mConfig.shaders.hotReload}
    };

    j["performance"] = {
        {"diagnosticsEnabled", mConfig.performance.diagnosticsEnabled}
    };

    j["safety"] = {
        {"safeMode", mConfig.safety.safeMode}
    };

    auto tempPath = mConfigPath;
    tempPath += ".tmp";

    {
        std::ofstream file(tempPath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return false;
        file << j.dump(4) << '\n';
        file.flush();
        if (!file.good()) return false;
    }

#ifdef _WIN32
    // En Windows, MoveFileExW garantiza reemplazo atómico con flush a disco
    bool replaced = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (MoveFileExW(tempPath.c_str(), mConfigPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            replaced = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (replaced) return true;
#endif

    std::filesystem::rename(tempPath, mConfigPath, ec);
    return !ec;
}

} // namespace aeris
