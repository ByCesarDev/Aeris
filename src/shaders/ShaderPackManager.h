#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "shaders/ShaderPack.h"

namespace aeris {

class ShaderPackManager {
public:
    static ShaderPackManager& getInstance();

    // Escanea data/shaderpacks/ y prepara data/generated/
    void scan(std::filesystem::path const& packsRoot, std::filesystem::path const& generatedRoot);

    // Lista de packs detectados
    [[nodiscard]] std::vector<ShaderPack> const& packs() const { return mPacks; }

    // Busca un pack por nombre
    [[nodiscard]] ShaderPack const* findPack(std::string_view name) const;
    [[nodiscard]] ShaderPack*       findPackMutable(std::string_view name);

    // Devuelve el pack actualmente activo según ConfigManager (nullptr si ninguno o "OFF")
    [[nodiscard]] ShaderPack const* activePack() const;

    // Selecciona un pack por nombre y persiste la elección en la configuración
    bool selectPack(std::string_view name);

    // Desactiva el pack activo (vuelve al estado vanilla)
    void deactivatePack();

    // Importa un archivo .zip o .mcpack hacia data/shaderpacks/
    bool importPack(std::filesystem::path const& sourceFile, std::string& outError);

    // Elimina un paquete de shaders y sus datos generados
    bool deletePack(std::string_view name, std::string& outError);

    // Modifica un parámetro del pack y persiste el cambio en su archivo .txt
    bool setParameterValue(std::string_view packName, std::string_view paramName, std::string value);

    // Extrae de forma segura un archivo .zip o .mcpack validando que no haya path traversal
    static bool extractArchive(std::filesystem::path const& archive, std::filesystem::path const& destDir);

    // Comprueba si un archivo o directorio tiene extensión de shader pack válida (.zip o .mcpack)
    static bool isSupportedArchive(std::filesystem::path const& file);

    void clear() { mPacks.clear(); }

private:
    ShaderPackManager() = default;

    std::vector<ShaderPack> mPacks;
    std::filesystem::path   mPacksRoot;
    std::filesystem::path   mGeneratedRoot;

    ShaderPack loadPack(
        std::filesystem::path const& sourceDir,
        std::filesystem::path const& packArchive,
        std::filesystem::path const& binsDir,
        std::filesystem::path const& settingsPath,
        std::filesystem::path const& containerDir = {}
    );

    bool ensureExtracted(std::filesystem::path const& archive, std::filesystem::path const& dest);
    std::filesystem::path normalizeSourceDir(std::filesystem::path const& dir);
};

} // namespace aeris
