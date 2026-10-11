#include "core/ConfigManager.h"

#include <chrono>
#include <fstream>
#include <process.h>
#include <sstream>
#include <system_error>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace aeris {

namespace {

bool isSafeRelativePath(std::string_view p) {
    if (p.empty()) return false;
    if (p.starts_with('/') || p.starts_with('\\')) return false;
    if (p.size() >= 2 && p[1] == ':') return false; // Drive letter
    if (p.find("..") != std::string_view::npos) return false;
    return true;
}

std::string generateUniqueSuffix() {
    static std::atomic<uint64_t> sCounter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto pid = _getpid();
    auto count = sCounter.fetch_add(1);
    std::ostringstream ss;
    ss << pid << "_" << now << "_" << count;
    return ss.str();
}

} // namespace

ConfigManager::ConfigManager() {
    reset();
}

ConfigManager& ConfigManager::getInstance() {
    static ConfigManager instance;
    return instance;
}

void ConfigManager::reset() {
    mConfig     = Config{};
    mConfigPath.clear();
    mLastError.clear();
}

bool ConfigManager::validate(Config const& cfg, std::string& outError) {
    if (cfg.configVersion != 1) {
        outError = "Unsupported configVersion: " + std::to_string(cfg.configVersion);
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
        mLastError = "Failed to open config file for reading: " + mConfigPath.string();
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (std::exception const& e) {
        mLastError = std::string("JSON parse error: ") + e.what();
        return false;
    }

    // Instancia de prueba (candidato) para no modificar el estado activo hasta verificar
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
            if (s.contains("enableExperimentalHooks")) {
                candidate.safety.enableExperimentalHooks = s["enableExperimentalHooks"].get<bool>();
            }
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
    if (mConfigPath.empty()) {
        mLastError = "Config path is empty; cannot save";
        return false;
    }

    std::string valError;
    if (!validate(mConfig, valError)) {
        mLastError = "Cannot save invalid config: " + valError;
        return false;
    }

    std::error_code ec;
    auto parent = mConfigPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            mLastError = "Failed to create directory: " + ec.message();
            return false;
        }
    }

    nlohmann::json j;
    j["configVersion"]  = mConfig.configVersion;
    j["enabled"]        = mConfig.enabled;
    j["language"]       = mConfig.language;
    j["dataDir"]        = mConfig.dataDir;
    j["shaderPacksDir"] = mConfig.shaderPacksDir;
    j["generatedDir"]   = mConfig.generatedDir;
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
        {"safeMode",                mConfig.safety.safeMode},
        {"enableExperimentalHooks", mConfig.safety.enableExperimentalHooks}
    };

    // Escritura atómica mediante archivo temporal con identificador exclusivo
    auto suffix = generateUniqueSuffix();
    auto tempFile = parent / (mConfigPath.filename().string() + ".tmp_" + suffix);

    {
        std::ofstream file(tempFile, std::ios::trunc);
        if (!file.is_open()) {
            mLastError = "Failed to open temporary file for writing: " + tempFile.string();
            return false;
        }

        try {
            file << j.dump(4);
            file.flush();
            if (!file.good()) {
                mLastError = "Stream error while writing config payload";
                file.close();
                std::filesystem::remove(tempFile, ec);
                return false;
            }
        } catch (std::exception const& e) {
            mLastError = std::string("JSON serialization error: ") + e.what();
            file.close();
            std::filesystem::remove(tempFile, ec);
            return false;
        }
    }

#ifdef _WIN32
    // En Windows MoveFileExW con MOVEFILE_REPLACE_EXISTING realiza sustitución atómica
    std::wstring wTemp = tempFile.wstring();
    std::wstring wDest = mConfigPath.wstring();
    if (!MoveFileExW(wTemp.c_str(), wDest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        mLastError = "Atomic MoveFileExW failed with code " + std::to_string(GetLastError());
        std::filesystem::remove(tempFile, ec);
        return false;
    }
#else
    std::filesystem::rename(tempFile, mConfigPath, ec);
    if (ec) {
        mLastError = "Atomic rename failed: " + ec.message();
        std::filesystem::remove(tempFile, ec);
        return false;
    }
#endif

    return true;
}

} // namespace aeris
