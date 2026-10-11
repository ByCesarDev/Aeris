#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "shaders/ShaderPack.h"

namespace aeris {

struct ArchiveLimits {
    std::size_t   maxFiles = 5000;
    std::uint64_t maxTotalUncompressedBytes = 350 * 1024 * 1024; // 350 MB
    std::uint64_t maxSingleFileBytes = 100 * 1024 * 1024;        // 100 MB
    std::size_t   maxDirectoryDepth = 10;
    double        maxCompressionRatio = 200.0;
};

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

    // Selecciona un pack por nombre y persiste la elección de manera transaccional
    bool selectPack(std::string_view name);

    // Desactiva el pack activo (vuelve al estado vanilla) de manera transaccional
    bool deactivatePack();

    // Selecciona explícitamente un subpack y actualiza los materiales
    bool selectSubpack(std::string_view packName, std::string_view subpackName);

    // Importa un archivo .zip o .mcpack de forma transaccional preservando archivos previos ante error
    bool importPack(std::filesystem::path const& sourceFile, std::string& outError);

    // Elimina un paquete verificando que las rutas pertenezcan a los directorios administrados
    bool deletePack(std::string_view name, std::string& outError);

    // Modifica un parámetro del pack validando su valor y guardándolo transaccionalmente
    bool setParameterValue(std::string_view packName, std::string_view paramName, std::string value);

    // Extrae en un directorio temporal aislado exclusivo con inspección y verificación de integridad
    static bool extractArchive(
        std::filesystem::path const& archive,
        std::filesystem::path const& destDir,
        ArchiveLimits const& limits = {}
    );

    // Valida la integridad, seguridad y estructura de un archivo comprimido antes de instalarlo
    static bool validateArchiveIntegrity(
        std::filesystem::path const& archive,
        ArchiveLimits const& limits = {},
        std::string* outError = nullptr
    );

    // Comprueba si un archivo o directorio tiene extensión de shader pack válida (.zip o .mcpack)
    static bool isSupportedArchive(std::filesystem::path const& file);

    // Verifica que una ruta objetivo resida estrictamente dentro de un directorio base permitido
    static bool isPathInside(std::filesystem::path const& base, std::filesystem::path const& target);

    // Genera un sufijo exclusivo (PID + reloj de alta resolución + contador atómico)
    static std::string generateUniqueSuffix();

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

    std::filesystem::path normalizeSourceDir(
        std::filesystem::path const& dir,
        int maxDepth = 3,
        std::unordered_set<std::string>* visited = nullptr
    );

    void copyMaterialsToBins(ShaderPack const& pack);
};

} // namespace aeris
