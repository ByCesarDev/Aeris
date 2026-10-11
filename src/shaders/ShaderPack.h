#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "shaders/ShaderParameter.h"

namespace aeris {

enum class ShaderRenderMode {
    Forward,
    Deferred,
    Unknown
};

inline char const* renderModeToString(ShaderRenderMode mode) {
    switch (mode) {
    case ShaderRenderMode::Deferred: return "Deferred";
    case ShaderRenderMode::Forward:  return "Forward";
    default:                         return "Unknown";
    }
}

struct SubpackVariant {
    std::string folderName;  // Nombre de la carpeta dentro de subpacks/ (ej: "vc", "novc")
    std::string displayName; // Nombre legible (ej: "Volumetric Clouds")
};

struct ShaderPackMetadata {
    std::string name;
    std::string author;
    std::string version;
    std::string description;
};

struct ShaderPack {
    std::string           name;
    ShaderPackMetadata    metadata;
    std::filesystem::path archivePath;  // Ruta a .zip o .mcpack (vacía si es directorio)
    std::filesystem::path sourceDir;    // Directorio de trabajo en data/generated/<pack>/src/
    std::filesystem::path binsDir;      // Directorio de salida en data/generated/<pack>/compiled/
    std::filesystem::path prebuiltDir;  // Directorio con *.material.bin precompilados detectados
    std::filesystem::path settingsPath; // Ruta del archivo de configuración (.txt)

    bool                         isValid = true;
    ShaderRenderMode             renderMode = ShaderRenderMode::Forward;
    std::vector<SubpackVariant>  subpacks;
    std::vector<std::string>     profiles;
    std::vector<ShaderParameter> parameters;
    std::string                  activeSubpack;

    [[nodiscard]] bool hasCompiledMaterials() const {
        return !prebuiltDir.empty();
    }
};

} // namespace aeris
