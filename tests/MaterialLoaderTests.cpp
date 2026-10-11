#include "TestRunner.h"
#include "shaders/MaterialLoader.h"

#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

void runMaterialLoaderTests() {
    std::cout << "\nRunning MaterialLoader Tests...\n";

    auto& loader = aeris::MaterialLoader::getInstance();
    loader.restoreVanilla();
    loader.resetStats();

    // =========================================================================
    // 1. Normalización de rutas de materiales (normalizeMaterialName)
    // =========================================================================
    check(aeris::MaterialLoader::normalizeMaterialName("data/renderer/materials/RenderChunk.material.bin") == "RenderChunk.material.bin",
        "Unix style material path must normalize to filename");

    check(aeris::MaterialLoader::normalizeMaterialName("data\\renderer\\materials\\RenderChunk.material.bin") == "RenderChunk.material.bin",
        "Windows style backslash path must normalize to filename");

    check(aeris::MaterialLoader::normalizeMaterialName("renderer/materials\\SunMoon.material.bin") == "SunMoon.material.bin",
        "Mixed separator path must normalize to filename");

    check(aeris::MaterialLoader::normalizeMaterialName("DeferredClouds.material.bin") == "DeferredClouds.material.bin",
        "Direct material filename must normalize to itself");

    check(aeris::MaterialLoader::normalizeMaterialName("data/renderer/materials/shaders.properties").empty(),
        "Non-material file must normalize to empty string");

    check(aeris::MaterialLoader::normalizeMaterialName("data/renderer/materials/RenderChunk.material.bin.bak").empty(),
        "File with wrong trailing extension must normalize to empty string");

    check(aeris::MaterialLoader::normalizeMaterialName(".material.bin").empty(),
        "Bare extension without name must normalize to empty string");

    check(aeris::MaterialLoader::normalizeMaterialName("").empty(),
        "Empty request path must normalize to empty string");

    check(aeris::MaterialLoader::normalizeMaterialName("data/renderer/materials/test..secret.material.bin").empty(),
        "Filename containing double dots must be rejected");

    // =========================================================================
    // 2. Estado inicial y fallback sin shaderpack activo
    // =========================================================================
    check(!loader.isOverrideActive(), "MaterialLoader must initially not have active override");
    check(loader.activePackName().empty(), "Initial active pack name must be empty");
    check(loader.activePrebuiltDirs().empty(), "Initial prebuilt directories must be empty");

    auto initialRes = loader.resolveMaterial("data/renderer/materials/RenderChunk.material.bin");
    check(!initialRes.has_value(), "resolveMaterial without active pack must return nullopt (vanilla fallback)");

    // =========================================================================
    // 3. Resolución con archivos reales y fallback
    // =========================================================================
    auto tempDir = std::filesystem::temp_directory_path() / "aeris_mat_loader_tests";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    auto pack1Dir = tempDir / "subpack_forward";
    auto pack2Dir = tempDir / "subpack_deferred";
    std::filesystem::create_directories(pack1Dir);
    std::filesystem::create_directories(pack2Dir);

    // Crear material A en pack1
    std::vector<uint8_t> chunkBytes = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    {
        std::ofstream out(pack1Dir / "RenderChunk.material.bin", std::ios::binary);
        out.write(reinterpret_cast<char const*>(chunkBytes.data()), static_cast<std::streamsize>(chunkBytes.size()));
    }

    // Crear material B en ambos packs (pack1 tiene prioridad)
    std::vector<uint8_t> skyBytes1 = {0xAA, 0xBB};
    std::vector<uint8_t> skyBytes2 = {0xCC, 0xDD};
    {
        std::ofstream out(pack1Dir / "Sky.material.bin", std::ios::binary);
        out.write(reinterpret_cast<char const*>(skyBytes1.data()), static_cast<std::streamsize>(skyBytes1.size()));
    }
    {
        std::ofstream out(pack2Dir / "Sky.material.bin", std::ios::binary);
        out.write(reinterpret_cast<char const*>(skyBytes2.data()), static_cast<std::streamsize>(skyBytes2.size()));
    }

    // Crear material C exclusivamente en pack2
    std::vector<uint8_t> cloudsBytes2 = {0x55, 0x66, 0x77};
    {
        std::ofstream out(pack2Dir / "Clouds.material.bin", std::ios::binary);
        out.write(reinterpret_cast<char const*>(cloudsBytes2.data()), static_cast<std::streamsize>(cloudsBytes2.size()));
    }

    // Crear archivo de 0 bytes (corrupto/vacío)
    {
        std::ofstream out(pack1Dir / "Empty.material.bin", std::ios::binary);
    }

    // Registrar pack activo con ambos subpacks en orden
    loader.setActiveShaderPack("TestShaderPack", {pack1Dir, pack2Dir});
    check(loader.isOverrideActive(), "setActiveShaderPack must activate override");
    check(loader.activePackName() == "TestShaderPack", "Active pack name must match");
    check(loader.activePrebuiltDirs().size() == 2, "Must register 2 prebuilt search directories");

    // Resolver RenderChunk (existe en pack1)
    auto resChunk = loader.resolveMaterial("data/renderer/materials/RenderChunk.material.bin");
    check(resChunk.has_value(), "resolveMaterial for existing RenderChunk must return value");
    check(*resChunk == chunkBytes, "Resolved RenderChunk bytes must exactly match file content");

    // Resolver Sky (existe en ambos, debe tomar pack1 por prioridad de orden)
    auto resSky = loader.resolveMaterial("data/renderer/materials/Sky.material.bin");
    check(resSky.has_value(), "resolveMaterial for Sky must return value");
    check(*resSky == skyBytes1, "Resolved Sky must prioritize first registered subpack");

    // Resolver Clouds (existe en pack2)
    auto resClouds = loader.resolveMaterial("data\\renderer\\materials\\Clouds.material.bin");
    check(resClouds.has_value(), "resolveMaterial for Clouds in second subpack must return value");
    check(*resClouds == cloudsBytes2, "Resolved Clouds bytes must match second subpack content");

    // Archivo de 0 bytes debe fallar y hacer fallback a vanilla
    auto resEmpty = loader.resolveMaterial("data/renderer/materials/Empty.material.bin");
    check(!resEmpty.has_value(), "Empty (0 bytes) material file must be rejected with vanilla fallback");

    // Archivo no existente en el pack debe hacer fallback a vanilla
    auto resMissing = loader.resolveMaterial("data/renderer/materials/Missing.material.bin");
    check(!resMissing.has_value(), "Missing material in active pack must fall back to vanilla");

    // =========================================================================
    // 4. Telemetría y estadísticas
    // =========================================================================
    check(loader.totalRequests() >= 5, "Total requests counter must track queries");
    check(loader.totalOverrides() == 3, "Total overrides counter must equal successfully overridden materials");
    check(loader.totalFallbacks() >= 2, "Total fallbacks counter must record vanilla fallbacks");
    check(loader.lastResolvedMaterial() == "Clouds.material.bin", "Last resolved material must record Clouds");

    // =========================================================================
    // 5. Desactivación con restoreVanilla
    // =========================================================================
    loader.restoreVanilla();
    check(!loader.isOverrideActive(), "restoreVanilla must deactivate override");
    check(loader.activePackName().empty(), "restoreVanilla must clear pack name");
    check(loader.activePrebuiltDirs().empty(), "restoreVanilla must clear prebuilt dirs");

    auto resAfterRestore = loader.resolveMaterial("data/renderer/materials/RenderChunk.material.bin");
    check(!resAfterRestore.has_value(), "After restoreVanilla, requests must immediately return nullopt");

    // =========================================================================
    // 6. Concurrencia multihilo
    // =========================================================================
    loader.setActiveShaderPack("ThreadTestPack", {pack1Dir, pack2Dir});
    std::vector<std::thread> workers;
    std::atomic<int> successCount{0};

    for (int i = 0; i < 8; ++i) {
        workers.emplace_back([&loader, &successCount]() {
            for (int j = 0; j < 50; ++j) {
                auto r1 = loader.resolveMaterial("data/renderer/materials/RenderChunk.material.bin");
                if (r1.has_value()) successCount++;
                auto r2 = loader.resolveMaterial("data/renderer/materials/Missing.material.bin");
                if (!r2.has_value()) successCount++;
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }

    check(successCount == 8 * 50 * 2, "Multithreaded concurrent queries must complete without race conditions");


    // =========================================================================
    // 7. Pruebas de la Etapa 3.3: Normalización extendida, seguridad y pre-indexación
    // =========================================================================
    check(aeris::MaterialLoader::normalizeMaterialName("data/renderer/materials/CubemapHDRi.material.bin") == "CubemapHDRi.material.bin",
        "CubemapHDRi with full data/renderer/materials/ prefix must normalize");

    check(aeris::MaterialLoader::normalizeMaterialName("data\\renderer\\materials\\UIText.material.bin") == "UIText.material.bin",
        "UIText with Windows backslashes must normalize");

    check(aeris::MaterialLoader::normalizeMaterialName("/data/renderer/materials/RenderChunk.material.bin").empty(),
        "Leading slash absolute path must be rejected");

    check(aeris::MaterialLoader::normalizeMaterialName("C:/Windows/System32/RenderChunk.material.bin").empty(),
        "Drive letter path must be rejected");

    check(aeris::MaterialLoader::normalizeMaterialName("data/renderer/materials/../RenderChunk.material.bin").empty(),
        "Path traversal in directory components must be rejected");

    check(aeris::MaterialLoader::normalizeMaterialName("data/renderer/materials/test\0bad.material.bin").empty(),
        "Null byte in path must be rejected");

    // Pre-indexing test: fast findMaterial without disk I/O
    loader.setActiveShaderPack("TestPack", {pack1Dir, pack2Dir});
    auto findChunk = loader.findMaterial("data/renderer/materials/RenderChunk.material.bin");
    check(findChunk.has_value(), "Pre-indexed RenderChunk must be found via findMaterial");
    check(findChunk->canonicalName == "RenderChunk.material.bin", "Entry canonicalName must match");
    check(findChunk->fileSize == chunkBytes.size(), "Cached fileSize must match original bytes size");
    check(findChunk->packName == "TestPack", "Entry packName must match active pack");

    auto findMissing = loader.findMaterial("data/renderer/materials/NonExistent.material.bin");
    check(!findMissing.has_value(), "Missing material in findMaterial must return nullopt");

    // Dry-run telemetry tests
    check(loader.dryRunMatches() == 0, "Initial dryRunMatches must be 0");
    check(loader.dryRunMisses() == 0, "Initial dryRunMisses must be 0");

    loader.recordDryRunMatch();
    loader.recordDryRunMatch();
    loader.recordDryRunMiss();
    check(loader.dryRunMatches() == 2, "dryRunMatches must record 2 matches");
    check(loader.dryRunMisses() == 1, "dryRunMisses must record 1 miss");

    loader.restoreVanilla();

    std::filesystem::remove_all(tempDir);
}
