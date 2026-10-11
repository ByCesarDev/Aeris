#include "shaders/ShaderPackParser.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "nlohmann/json.hpp"
#include "shaders/ShaderPackManager.h"

namespace aeris {

namespace {

std::string trim(std::string_view sv) {
    auto start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(start, end - start + 1));
}

std::string_view trimView(std::string_view sv) {
    auto start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    auto end = sv.find_last_not_of(" \t\r\n");
    return sv.substr(start, end - start + 1);
}

std::string toLower(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

std::vector<std::string> splitTokens(std::string_view line) {
    std::vector<std::string> tokens;
    size_t start = 0;
    while (start < line.size()) {
        start = line.find_first_not_of(" \t\r\n", start);
        if (start == std::string_view::npos) break;
        size_t end = line.find_first_of(" \t\r\n", start);
        if (end == std::string_view::npos) {
            tokens.emplace_back(line.substr(start));
            break;
        }
        tokens.emplace_back(line.substr(start, end - start));
        start = end;
    }
    return tokens;
}

void configureParameterFromTokens(ShaderParameter& param, std::vector<std::string> const& tokens) {
    if (tokens.empty()) {
        param.type         = ParameterType::Bool;
        param.defaultValue = "OFF";
        param.current      = "OFF";
        return;
    }

    if (tokens.size() == 1) {
        auto val = tokens[0];
        auto low = toLower(val);
        if (low == "true" || low == "on" || low == "1") {
            param.type         = ParameterType::Bool;
            param.defaultValue = "ON";
            param.current      = "ON";
            return;
        }
        if (low == "false" || low == "off" || low == "0") {
            param.type         = ParameterType::Bool;
            param.defaultValue = "OFF";
            param.current      = "OFF";
            return;
        }
    }

    // Comprobar formato slider numérico: <min> <max> <default> [step]
    bool allNumeric = true;
    for (auto const& t : tokens) {
        char* end = nullptr;
        std::strtod(t.c_str(), &end);
        if (end == t.c_str() || *end != '\0') {
            allNumeric = false;
            break;
        }
    }

    if (allNumeric && tokens.size() >= 3) {
        param.type         = ParameterType::Slider;
        param.minVal       = std::strtod(tokens[0].c_str(), nullptr);
        param.maxVal       = std::strtod(tokens[1].c_str(), nullptr);
        param.defaultValue = tokens[2];
        param.current      = tokens[2];

        if (tokens.size() >= 4) {
            param.stepVal = std::strtod(tokens[3].c_str(), nullptr);
        } else {
            param.stepVal = 1.0;
        }

        bool hasDot = false;
        for (auto const& t : tokens) {
            if (t.find('.') != std::string::npos) {
                hasDot = true;
                break;
            }
        }
        param.isInteger = !hasDot;
        return;
    }

    // Enumeración estándar
    param.type         = ParameterType::Enum;
    param.defaultValue = tokens.front();
    param.current      = tokens.front();
    param.options.clear();
    for (auto const& t : tokens) {
        param.options.push_back({t, t});
    }
}

std::string subpackNameOf(std::filesystem::path const& path) {
    auto str = path.generic_string();
    std::size_t pos = std::string::npos;
    if (str.starts_with("subpacks/")) {
        pos = 0;
    } else {
        pos = str.find("/subpacks/");
        if (pos != std::string::npos) {
            pos += 1; // Saltear '/' inicial para apuntar exactamente al inicio de "subpacks/"
        }
    }
    if (pos == std::string::npos) return {};

    pos += 9;
    auto nextSlash = str.find('/', pos);
    if (nextSlash == std::string::npos) {
        return str.substr(pos);
    }
    return str.substr(pos, nextSlash - pos);
}

} // namespace

bool ShaderPackParser::validateParameterValue(
    ShaderParameter const& param,
    std::string_view value,
    std::string& outNormalized
) {
    auto trimmed = trim(value);
    if (trimmed.empty()) return false;

    switch (param.type) {
    case ParameterType::Bool: {
        auto lower = toLower(trimmed);
        if (lower == "on" || lower == "true" || lower == "1") {
            outNormalized = "ON";
            return true;
        }
        if (lower == "off" || lower == "false" || lower == "0") {
            outNormalized = "OFF";
            return true;
        }
        return false;
    }
    case ParameterType::Slider: {
        char* endPtr = nullptr;
        double val = std::strtod(trimmed.c_str(), &endPtr);
        if (endPtr == trimmed.c_str() || *endPtr != '\0') {
            return false;
        }
        if (std::isnan(val) || std::isinf(val)) {
            return false;
        }
        if (val < param.minVal || val > param.maxVal) {
            return false;
        }
        if (param.isInteger) {
            if (std::floor(val) != val) return false;
            outNormalized = std::to_string(static_cast<long long>(val));
        } else {
            outNormalized = trimmed;
        }
        return true;
    }
    case ParameterType::Enum: {
        for (auto const& opt : param.options) {
            if (opt.value == trimmed) {
                outNormalized = opt.value;
                return true;
            }
        }
        return false;
    }
    case ParameterType::Color: {
        if (trimmed.size() != 7 || trimmed[0] != '#') return false;
        for (size_t i = 1; i < 7; ++i) {
            char c = trimmed[i];
            if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        }
        outNormalized = trimmed;
        return true;
    }
    case ParameterType::Category:
        outNormalized = trimmed;
        return true;
    }
    return false;
}

std::string ShaderPackParser::parsePrebuiltDirective(std::filesystem::path const& propertiesFile) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(propertiesFile, ec)) return {};

    std::ifstream file(propertiesFile);
    if (!file.is_open()) return {};

    std::string line;
    while (std::getline(file, line)) {
        auto trimmed = trimView(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;

        auto eqPos = trimmed.find('=');
        if (eqPos == std::string_view::npos) continue;

        auto key = trim(trimmed.substr(0, eqPos));
        if (key == "prebuilt") {
            return trim(trimmed.substr(eqPos + 1));
        }
    }
    return {};
}

std::vector<std::string> ShaderPackParser::parseProfiles(std::filesystem::path const& propertiesFile) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(propertiesFile, ec)) return {};

    std::ifstream file(propertiesFile);
    if (!file.is_open()) return {};

    std::string line;
    while (std::getline(file, line)) {
        auto trimmed = trimView(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;

        auto eqPos = trimmed.find('=');
        if (eqPos == std::string_view::npos) continue;

        auto key = trim(trimmed.substr(0, eqPos));
        if (key == "profiles") {
            return splitTokens(trimmed.substr(eqPos + 1));
        }
    }
    return {};
}

std::vector<ShaderParameter> ShaderPackParser::parseProperties(std::filesystem::path const& propertiesFile) {
    std::vector<ShaderParameter> params;
    std::map<std::string, std::size_t> nameIndexMap;

    std::error_code ec;
    if (!std::filesystem::is_regular_file(propertiesFile, ec)) return params;

    std::ifstream file(propertiesFile);
    if (!file.is_open()) return params;

    std::string line;
    while (std::getline(file, line)) {
        auto trimmed = trimView(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;

        auto eqPos = trimmed.find('=');
        if (eqPos == std::string_view::npos) continue;

        auto key   = trim(trimmed.substr(0, eqPos));
        auto value = trim(trimmed.substr(eqPos + 1));

        if (key.starts_with("option.")) {
            auto paramName = key.substr(7);
            if (paramName.empty()) continue;

            auto tokens = splitTokens(value);

            ShaderParameter param;
            param.name  = paramName;
            param.label = paramName;
            configureParameterFromTokens(param, tokens);

            nameIndexMap[paramName] = params.size();
            params.push_back(std::move(param));
        } else if (key.starts_with("screen.")) {
            auto screenPath = key.substr(7);
            auto tokens     = splitTokens(value);

            ShaderParameter screenParam;
            screenParam.type         = ParameterType::Category;
            screenParam.name         = screenPath;
            screenParam.label        = screenPath;
            screenParam.childNames   = tokens;
            params.push_back(std::move(screenParam));
        } else if (key == "sliders") {
            auto tokens = splitTokens(value);
            for (auto const& name : tokens) {
                if (nameIndexMap.find(name) == nameIndexMap.end()) {
                    ShaderParameter p;
                    p.name      = name;
                    p.label     = name;
                    p.type      = ParameterType::Slider;
                    p.minVal    = 0.0;
                    p.maxVal    = 1.0;
                    p.current   = "0.0";
                    p.span      = ParameterSpan::Full;
                    nameIndexMap[name] = params.size();
                    params.push_back(std::move(p));
                }
            }
        }
    }
    return params;
}

ShaderPackMetadata ShaderPackParser::parseMetadata(
    std::filesystem::path const& packDir,
    std::vector<SubpackVariant>& outSubpacks
) {
    ShaderPackMetadata meta;
    meta.name = packDir.filename().string();
    outSubpacks.clear();

    std::error_code ec;

    // 1. Leer manifest.json respetando el orden explícito del autor
    auto manifestPath = packDir / "manifest.json";
    if (std::filesystem::is_regular_file(manifestPath, ec)) {
        std::ifstream file(manifestPath);
        if (file.is_open()) {
            try {
                nlohmann::json j;
                file >> j;

                if (j.contains("header") && j["header"].is_object()) {
                    auto const& h = j["header"];
                    if (h.contains("name") && h["name"].is_string()) {
                        meta.name = h["name"].get<std::string>();
                    }
                    if (h.contains("description") && h["description"].is_string()) {
                        meta.description = h["description"].get<std::string>();
                    }
                    if (h.contains("version") && h["version"].is_array()) {
                        auto const& v = h["version"];
                        std::string verStr;
                        for (std::size_t i = 0; i < v.size(); ++i) {
                            if (i > 0) verStr += ".";
                            verStr += std::to_string(v[i].get<int>());
                        }
                        meta.version = verStr;
                    }
                }

                if (j.contains("subpacks") && j["subpacks"].is_array()) {
                    for (auto const& sub : j["subpacks"]) {
                        if (sub.contains("folder_name") && sub.contains("name")) {
                            SubpackVariant variant;
                            variant.folderName  = sub["folder_name"].get<std::string>();
                            variant.displayName = sub["name"].get<std::string>();
                            outSubpacks.push_back(std::move(variant));
                        }
                    }
                }
            } catch (...) {}
        }
    }

    // 2. Si no hubo subpacks en el manifest, detectar carpetas reales en subpacks/
    auto subpacksFolder = packDir / "subpacks";
    if (outSubpacks.empty() && std::filesystem::is_directory(subpacksFolder, ec)) {
        for (auto const& entry : std::filesystem::directory_iterator(subpacksFolder, ec)) {
            if (entry.is_directory(ec)) {
                SubpackVariant v;
                v.folderName  = entry.path().filename().string();
                v.displayName = v.folderName;
                outSubpacks.push_back(std::move(v));
            }
        }
    }

    return meta;
}

std::filesystem::path ShaderPackParser::findPrebuiltMaterials(std::filesystem::path const& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return {};

    if (hasMaterialBin(root)) {
        return root;
    }

    auto candidate = root / "renderer" / "materials";
    if (hasMaterialBin(candidate)) return candidate;

    candidate = root / "materials";
    if (hasMaterialBin(candidate)) return candidate;

    std::vector<std::filesystem::path> found;
    std::filesystem::recursive_directory_iterator it(root, ec);
    std::filesystem::recursive_directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec)) continue;

        auto name = it->path().filename().string();
        if (!name.empty() && (name[0] == '.' || name[0] == '_')) {
            it.disable_recursion_pending();
            continue;
        }

        if (hasMaterialBin(it->path())) {
            found.push_back(it->path());
            it.disable_recursion_pending();
        }
    }

    if (found.empty()) return {};

    for (auto const& p : found) {
        if (p.generic_string().find("subpacks") != std::string::npos) {
            return p;
        }
    }
    return found.front();
}

std::vector<std::filesystem::path> ShaderPackParser::collectPrebuiltDirs(
    std::filesystem::path const& root,
    std::string_view activeSubpack
) {
    std::vector<std::filesystem::path> base;
    std::vector<std::string> subpackOrder;
    std::unordered_map<std::string, std::vector<std::filesystem::path>> subpackMap;

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return base;

    if (hasMaterialBin(root)) {
        base.push_back(root);
    }

    std::filesystem::recursive_directory_iterator it(root, ec);
    std::filesystem::recursive_directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_directory(ec)) continue;

        auto name = it->path().filename().string();
        if ((!name.empty() && (name[0] == '.' || name[0] == '_')) || name == "vanilla") {
            it.disable_recursion_pending();
            continue;
        }

        if (!hasMaterialBin(it->path())) continue;

        auto sub = subpackNameOf(it->path());
        if (sub.empty()) {
            base.push_back(it->path());
        } else {
            if (subpackMap.find(sub) == subpackMap.end()) {
                subpackOrder.push_back(sub);
            }
            subpackMap[sub].push_back(it->path());
        }
        it.disable_recursion_pending();
    }

    // Consultar manifest.json para obtener el orden exacto declarado por el autor
    std::vector<std::string> authorDeclaredSubpacks;
    auto manifestPath = root / "manifest.json";
    if (!std::filesystem::is_regular_file(manifestPath, ec) && root.has_parent_path()) {
        manifestPath = root.parent_path() / "manifest.json";
    }
    if (std::filesystem::is_regular_file(manifestPath, ec)) {
        std::ifstream mf(manifestPath);
        if (mf.is_open()) {
            try {
                nlohmann::json j;
                mf >> j;
                if (j.contains("subpacks") && j["subpacks"].is_array()) {
                    for (auto const& s : j["subpacks"]) {
                        if (s.contains("folder_name") && s["folder_name"].is_string()) {
                            authorDeclaredSubpacks.push_back(s["folder_name"].get<std::string>());
                        }
                    }
                }
            } catch (...) {}
        }
    }

    std::vector<std::filesystem::path> out = base;

    if (!subpackMap.empty()) {
        if (!activeSubpack.empty()) {
            auto itSub = subpackMap.find(std::string(activeSubpack));
            if (itSub != subpackMap.end()) {
                out.insert(out.end(), itSub->second.begin(), itSub->second.end());
            } else {
                // Fallback al orden declarado por el autor o al primero descubierto
                std::string chosen;
                for (auto const& aSub : authorDeclaredSubpacks) {
                    if (subpackMap.find(aSub) != subpackMap.end()) {
                        chosen = aSub;
                        break;
                    }
                }
                if (chosen.empty() && !subpackOrder.empty()) {
                    chosen = subpackOrder.front();
                }
                if (!chosen.empty()) {
                    auto const& dirs = subpackMap[chosen];
                    out.insert(out.end(), dirs.begin(), dirs.end());
                }
            }
        } else {
            // Sin selección explícita: usar estrictamente el primer subpack según el orden declarado por el autor
            std::string chosen;
            for (auto const& aSub : authorDeclaredSubpacks) {
                if (subpackMap.find(aSub) != subpackMap.end()) {
                    chosen = aSub;
                    break;
                }
            }
            if (chosen.empty() && !subpackOrder.empty()) {
                chosen = subpackOrder.front();
            }
            if (!chosen.empty()) {
                auto const& dirs = subpackMap[chosen];
                out.insert(out.end(), dirs.begin(), dirs.end());
            }
        }
    }

    return out;
}

ShaderRenderMode ShaderPackParser::detectRenderMode(std::filesystem::path const& binsDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(binsDir, ec)) {
        return ShaderRenderMode::Forward;
    }

    for (auto const& entry : std::filesystem::directory_iterator(binsDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto filename = entry.path().filename().string();
        if (filename.find("Deferred") != std::string::npos && filename.ends_with(".material.bin")) {
            return ShaderRenderMode::Deferred;
        }
    }
    return ShaderRenderMode::Forward;
}

bool ShaderPackParser::hasMaterialBin(std::filesystem::path const& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;

    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().filename().string().ends_with(".material.bin")) {
            return true;
        }
    }
    return false;
}

bool ShaderPackParser::loadSettings(ShaderPack& pack, std::filesystem::path const& settingsFile) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(settingsFile, ec)) return false;

    std::ifstream file(settingsFile);
    if (!file.is_open()) return false;

    std::map<std::string, std::string> savedMap;
    std::string line;
    while (std::getline(file, line)) {
        auto trimmed = trimView(line);
        if (trimmed.empty() || trimmed.front() == '#') continue;

        auto eqPos = trimmed.find('=');
        if (eqPos == std::string_view::npos) continue;

        auto key   = trim(trimmed.substr(0, eqPos));
        auto value = trim(trimmed.substr(eqPos + 1));
        savedMap[key] = value;
    }

    // Cargar subpack activo si fue guardado
    auto subpackIt = savedMap.find("subpack");
    if (subpackIt != savedMap.end() && !subpackIt->second.empty()) {
        for (auto const& s : pack.subpacks) {
            if (s.folderName == subpackIt->second) {
                pack.activeSubpack = subpackIt->second;
                break;
            }
        }
    }

    // Cargar y validar parámetros individuales
    for (auto& param : pack.parameters) {
        auto it = savedMap.find(param.name);
        if (it != savedMap.end()) {
            std::string normalized;
            if (validateParameterValue(param, it->second, normalized)) {
                param.current = std::move(normalized);
            }
        }
    }
    return true;
}

bool ShaderPackParser::saveSettings(ShaderPack const& pack, std::filesystem::path const& settingsFile) {
    std::error_code ec;
    std::filesystem::create_directories(settingsFile.parent_path(), ec);

    // Escribir en un archivo temporal exclusivo para garantizar persistencia atómica
    auto tempPath = settingsFile.string() + ".tmp_" + ShaderPackManager::generateUniqueSuffix();

    {
        std::ofstream file(tempPath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) return false;

        file << "# Aeris Shader Settings for " << pack.name << "\n";
        if (!pack.activeSubpack.empty()) {
            file << "subpack = " << pack.activeSubpack << "\n";
        }
        for (auto const& param : pack.parameters) {
            if (param.type != ParameterType::Category) {
                file << param.name << " = " << param.current << "\n";
            }
        }
        file.flush();
        if (!file.good()) {
            file.close();
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

#ifdef _WIN32
    // Reemplazo atómico con flush a disco en Windows con reintentos
    bool replaced = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (MoveFileExW(
                std::filesystem::path(tempPath).wstring().c_str(),
                settingsFile.wstring().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            replaced = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!replaced) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
#else
    std::filesystem::rename(tempPath, settingsFile, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }
    return true;
#endif
}

} // namespace aeris
