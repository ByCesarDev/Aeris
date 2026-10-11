#include "shaders/ShaderPackParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <nlohmann/json.hpp>

namespace aeris {

namespace {

std::string_view trimView(std::string_view s) {
    std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string trim(std::string_view s) {
    return std::string(trimView(s));
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::vector<std::string> splitTokens(std::string_view s) {
    std::vector<std::string> out;
    std::string              current;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == ',' || c == '\r' || c == '\n') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

bool isInteger(std::string_view s) {
    if (s.empty()) return false;
    std::size_t i = (s[0] == '-' || s[0] == '+') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

bool parseDouble(std::string_view s, double& out) {
    std::string tmp(trimView(s));
    if (tmp.empty()) return false;
    char* end = nullptr;
    out       = std::strtod(tmp.c_str(), &end);
    return end != tmp.c_str() && *end == '\0';
}

bool isNumber(std::string_view s) {
    double dummy = 0.0;
    return parseDouble(s, dummy);
}

bool isBoolWord(std::string const& s) {
    std::string v = toLower(s);
    return v == "on" || v == "off" || v == "true" || v == "false" || v == "yes" || v == "no"
        || v == "1" || v == "0";
}

void configureParameterFromTokens(ShaderParameter& p, std::vector<std::string> const& tokens) {
    p.type = ParameterType::Bool;

    if (tokens.empty()) {
        p.current      = "OFF";
        p.defaultValue = "OFF";
        return;
    }

    bool allBool = std::all_of(tokens.begin(), tokens.end(), isBoolWord);
    bool allNum  = std::all_of(tokens.begin(), tokens.end(), isNumber);

    if (allBool) {
        p.type         = ParameterType::Bool;
        p.current      = tokens.front();
        p.defaultValue = tokens.front();
        return;
    }

    if (allNum && (tokens.size() == 2 || tokens.size() == 3)) {
        p.type = ParameterType::Slider;
        parseDouble(tokens[0], p.minVal);
        parseDouble(tokens[1], p.maxVal);
        if (tokens.size() == 3) {
            p.defaultValue = tokens[2];
            p.current      = tokens[2];
        } else {
            p.defaultValue = tokens[0];
            p.current      = tokens[0];
        }
        p.isInteger = isInteger(tokens[0]) && isInteger(tokens[1]);
        p.span      = ParameterSpan::Full; // Sliders usan ancho completo
        return;
    }

    // Enumeración
    p.type = ParameterType::Enum;
    p.options.clear();
    for (auto const& token : tokens) {
        p.options.push_back({token, token});
    }
    p.current      = tokens.front();
    p.defaultValue = tokens.front();
}

std::string subpackNameOf(std::filesystem::path const& p) {
    auto s   = p.generic_string();
    auto key = std::string("/subpacks/");
    auto pos = s.find(key);
    if (pos == std::string::npos) return {};
    auto rest  = s.substr(pos + key.size());
    auto slash = rest.find('/');
    return slash == std::string::npos ? rest : rest.substr(0, slash);
}

} // namespace

bool ShaderPackParser::hasMaterialBin(std::filesystem::path const& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().filename().string().ends_with(".material.bin")) {
            return true;
        }
    }
    return false;
}

ShaderRenderMode ShaderPackParser::detectRenderMode(std::filesystem::path const& binsDir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(binsDir, ec)) return ShaderRenderMode::Forward;

    for (auto const& entry : std::filesystem::directory_iterator(binsDir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto name = toLower(entry.path().filename().string());
        if (!name.ends_with(".material.bin")) continue;
        if (name.find("deferred") != std::string::npos) {
            return ShaderRenderMode::Deferred;
        }
    }
    return ShaderRenderMode::Forward;
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
            auto paramName = key.substr(7); // Quitar "option."
            if (paramName.empty()) continue;

            auto tokens = splitTokens(value);

            ShaderParameter param;
            param.name  = paramName;
            param.label = paramName;
            configureParameterFromTokens(param, tokens);

            nameIndexMap[paramName] = params.size();
            params.push_back(std::move(param));
        } else if (key.starts_with("screen.")) {
            // Jerarquía de pantallas: screen.MAIN.CATEGORY = ITEM1 ITEM2
            auto screenPath = key.substr(7);
            auto tokens     = splitTokens(value);

            ShaderParameter screenParam;
            screenParam.type         = ParameterType::Category;
            screenParam.name         = screenPath;
            screenParam.label        = screenPath;
            screenParam.childNames   = tokens;
            params.push_back(std::move(screenParam));
        } else if (key == "sliders") {
            // Sintaxis legacy: sliders = NAME1 NAME2
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

    // 1. Intentar leer manifest.json (estándar Bedrock Resource Pack)
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

                // Subpacks
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
        std::sort(outSubpacks.begin(), outSubpacks.end(), [](auto const& a, auto const& b) {
            return a.folderName < b.folderName;
        });
    }

    return meta;
}

std::filesystem::path ShaderPackParser::findPrebuiltMaterials(std::filesystem::path const& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return {};

    // 0. Si la raíz del paquete contiene directamente *.material.bin
    if (hasMaterialBin(root)) return root;

    // 1. Verificar si hay directiva explícita en shaders.properties
    auto directive = parsePrebuiltDirective(root / "shaders.properties");
    if (!directive.empty()) {
        auto candidate = root / directive;
        if (hasMaterialBin(candidate)) return candidate;
    }

    // 2. Ubicaciones comunes de paquetes empaquetados
    constexpr std::string_view commonLocations[] = {
        "renderer/materials",
        "pack/renderer/materials",
        "renderer",
        "dist",
        "output"
    };
    for (auto loc : commonLocations) {
        auto candidate = root / loc;
        if (hasMaterialBin(candidate)) return candidate;
    }

    // 3. Búsqueda recursiva poco profunda
    auto dirs = collectPrebuiltDirs(root);
    if (!dirs.empty()) {
        return dirs.front();
    }

    return {};
}

std::vector<std::filesystem::path> ShaderPackParser::collectPrebuiltDirs(std::filesystem::path const& root) {
    std::vector<std::filesystem::path> base;
    std::map<std::string, std::vector<std::filesystem::path>> subpacks;

    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) return {};

    if (hasMaterialBin(root)) {
        base.push_back(root);
    }

    std::filesystem::recursive_directory_iterator it(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec
    );
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
            subpacks[sub].push_back(it->path());
        }
        it.disable_recursion_pending();
    }

    std::sort(base.begin(), base.end());
    std::vector<std::filesystem::path> out = base;

    // Si existen subpacks, elegir la variante alfabéticamente superior (ej: novc < vc)
    if (!subpacks.empty()) {
        auto const& [name, dirs] = *subpacks.rbegin();
        out.insert(out.end(), dirs.begin(), dirs.end());
    }

    return out;
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

    for (auto& param : pack.parameters) {
        auto it = savedMap.find(param.name);
        if (it != savedMap.end()) {
            param.current = it->second;
        }
    }
    return true;
}

bool ShaderPackParser::saveSettings(ShaderPack const& pack, std::filesystem::path const& settingsFile) {
    std::error_code ec;
    std::filesystem::create_directories(settingsFile.parent_path(), ec);

    std::ofstream file(settingsFile, std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;

    file << "# Aeris Shader Settings for " << pack.name << "\n";
    for (auto const& param : pack.parameters) {
        if (param.type != ParameterType::Category) {
            file << param.name << " = " << param.current << "\n";
        }
    }
    file.flush();
    return file.good();
}

} // namespace aeris
