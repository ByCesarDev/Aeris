#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "shaders/ShaderPack.h"
#include "shaders/ShaderParameter.h"

namespace aeris {

class ShaderPackParser {
public:
    // Analiza shaders.properties y devuelve la lista de parámetros tipados y estructurados
    static std::vector<ShaderParameter> parseProperties(std::filesystem::path const& propertiesFile);

    // Lee la directiva profiles = ... de shaders.properties
    static std::vector<std::string> parseProfiles(std::filesystem::path const& propertiesFile);

    // Lee la directiva prebuilt = ... de shaders.properties (si existe)
    static std::string parsePrebuiltDirective(std::filesystem::path const& propertiesFile);

    // Extrae metadatos y variantes de subpacks desde manifest.json / project.json
    static ShaderPackMetadata parseMetadata(
        std::filesystem::path const& packDir,
        std::vector<SubpackVariant>& outSubpacks
    );

    // Determina si los binarios corresponden a renderizado Deferred o Forward
    static ShaderRenderMode detectRenderMode(std::filesystem::path const& binsDir);

    // Localiza el directorio principal con *.material.bin en el pack
    static std::filesystem::path findPrebuiltMaterials(std::filesystem::path const& root);

    // Recolecta todos los directorios que contienen *.material.bin aplicando reglas de subpacks
    static std::vector<std::filesystem::path> collectPrebuiltDirs(
        std::filesystem::path const& root,
        std::string_view activeSubpack = {}
    );

    // Carga y valida los ajustes guardados de un pack (.txt)
    static bool loadSettings(ShaderPack& pack, std::filesystem::path const& settingsFile);

    // Guarda los ajustes actuales de un pack (.txt)
    static bool saveSettings(ShaderPack const& pack, std::filesystem::path const& settingsFile);

    // Valida que un valor sea admisible según el tipo y límites del parámetro
    static bool validateParameterValue(
        ShaderParameter const& param,
        std::string_view value,
        std::string& outNormalized
    );

    // Comprueba si un directorio contiene al menos un archivo *.material.bin
    static bool hasMaterialBin(std::filesystem::path const& dir);
};

} // namespace aeris
