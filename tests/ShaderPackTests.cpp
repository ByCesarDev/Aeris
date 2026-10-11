#include "TestRunner.h"
#include "shaders/ShaderPack.h"
#include "shaders/ShaderPackManager.h"
#include "shaders/ShaderPackParser.h"
#include "shaders/ShaderParameter.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "miniz/miniz.h"

namespace {

// Helper para crear archivos zip de prueba de forma controlada
bool createTestZip(
    std::filesystem::path const& zipPath,
    std::vector<std::pair<std::string, std::string>> const& entries
) {
    std::error_code ec;
    std::filesystem::create_directories(zipPath.parent_path(), ec);

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));

#ifdef _WIN32
    FILE* fp = nullptr;
    if (_wfopen_s(&fp, zipPath.wstring().c_str(), L"wb") != 0 || !fp) return false;
#else
    FILE* fp = std::fopen(zipPath.string().c_str(), "wb");
    if (!fp) return false;
#endif

    if (!mz_zip_writer_init_cfile(&zip, fp, 0)) {
        std::fclose(fp);
        return false;
    }

    for (auto const& [name, content] : entries) {
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), content.data(), content.size(), static_cast<mz_uint>(MZ_DEFAULT_COMPRESSION))) {
            mz_zip_writer_end(&zip);
            std::fclose(fp);
            return false;
        }
    }

    bool ok = mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    std::fclose(fp);
    return ok;
}

} // namespace

void runShaderPackTests() {
    std::cout << "\nRunning ShaderPackManager & Parser Tests...\n";

    auto tempDir = std::filesystem::temp_directory_path() / "aeris_test_shaderpacks";
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);

    // =========================================================================
    // 1. Prueba de formatos de archivo soportados (.zip y .mcpack)
    // =========================================================================
    check(aeris::ShaderPackManager::isSupportedArchive("Pack.zip"), ".zip must be supported");
    check(aeris::ShaderPackManager::isSupportedArchive("Shader.mcpack"), ".mcpack must be supported");
    check(aeris::ShaderPackManager::isSupportedArchive("SHADER.MCPACK"), "Uppercase .MCPACK must be supported");
    check(!aeris::ShaderPackManager::isSupportedArchive("pack.txt"), ".txt must not be supported");
    check(!aeris::ShaderPackManager::isSupportedArchive("pack.tar"), ".tar must not be supported");

    // =========================================================================
    // 2. Verificación estricta de límites de directorios (isPathInside)
    // =========================================================================
    auto baseDir = tempDir / "managed_root";
    std::filesystem::create_directories(baseDir);

    check(aeris::ShaderPackManager::isPathInside(baseDir, baseDir / "packs" / "test.zip"), "Child in subfolder must be inside base");
    check(aeris::ShaderPackManager::isPathInside(baseDir, baseDir / "shader.txt"), "Child in root must be inside base");
    check(!aeris::ShaderPackManager::isPathInside(baseDir, tempDir / "other" / "file.txt"), "Outside sibling must not be inside base");
    check(!aeris::ShaderPackManager::isPathInside(baseDir, baseDir / ".." / "evil.txt"), "Path traversal child must not be inside base");
    check(!aeris::ShaderPackManager::isPathInside(baseDir, baseDir), "Directory itself must not be inside itself");
    check(!aeris::ShaderPackManager::isPathInside(baseDir, {}), "Empty path must not be inside base");

    // =========================================================================
    // 3. Validación exhaustiva de parámetros (validateParameterValue)
    // =========================================================================
    std::string norm;

    // 3.1. Booleanos
    aeris::ShaderParameter boolParam;
    boolParam.type = aeris::ParameterType::Bool;

    check(aeris::ShaderPackParser::validateParameterValue(boolParam, "ON", norm) && norm == "ON", "Bool 'ON' must validate");
    check(aeris::ShaderPackParser::validateParameterValue(boolParam, "true", norm) && norm == "ON", "Bool 'true' must normalize to ON");
    check(aeris::ShaderPackParser::validateParameterValue(boolParam, "1", norm) && norm == "ON", "Bool '1' must normalize to ON");
    check(aeris::ShaderPackParser::validateParameterValue(boolParam, "off", norm) && norm == "OFF", "Bool 'off' must normalize to OFF");
    check(aeris::ShaderPackParser::validateParameterValue(boolParam, "false", norm) && norm == "OFF", "Bool 'false' must normalize to OFF");
    check(aeris::ShaderPackParser::validateParameterValue(boolParam, "0", norm) && norm == "OFF", "Bool '0' must normalize to OFF");
    check(!aeris::ShaderPackParser::validateParameterValue(boolParam, "maybe", norm), "Invalid bool string must fail");
    check(!aeris::ShaderPackParser::validateParameterValue(boolParam, "2", norm), "Invalid bool number must fail");
    check(!aeris::ShaderPackParser::validateParameterValue(boolParam, "", norm), "Empty bool must fail");

    // 3.2. Sliders
    aeris::ShaderParameter floatSlider;
    floatSlider.type = aeris::ParameterType::Slider;
    floatSlider.minVal = 0.0;
    floatSlider.maxVal = 10.0;
    floatSlider.isInteger = false;

    check(aeris::ShaderPackParser::validateParameterValue(floatSlider, "5.5", norm), "In-range float slider must validate");
    check(aeris::ShaderPackParser::validateParameterValue(floatSlider, "0.0", norm), "Boundary min float slider must validate");
    check(aeris::ShaderPackParser::validateParameterValue(floatSlider, "10.0", norm), "Boundary max float slider must validate");
    check(!aeris::ShaderPackParser::validateParameterValue(floatSlider, "-0.1", norm), "Below-min float slider must fail");
    check(!aeris::ShaderPackParser::validateParameterValue(floatSlider, "10.1", norm), "Above-max float slider must fail");
    check(!aeris::ShaderPackParser::validateParameterValue(floatSlider, "not_a_number", norm), "Corrupt string slider must fail");

    aeris::ShaderParameter intSlider;
    intSlider.type = aeris::ParameterType::Slider;
    intSlider.minVal = 10.0;
    intSlider.maxVal = 100.0;
    intSlider.isInteger = true;

    check(aeris::ShaderPackParser::validateParameterValue(intSlider, "50", norm) && norm == "50", "Integer slider must validate");
    check(!aeris::ShaderPackParser::validateParameterValue(intSlider, "50.5", norm), "Fractional value on integer slider must fail");

    // 3.3. Enums
    aeris::ShaderParameter enumParam;
    enumParam.type = aeris::ParameterType::Enum;
    enumParam.options = {{"low", "Low"}, {"medium", "Medium"}, {"high", "High"}};

    check(aeris::ShaderPackParser::validateParameterValue(enumParam, "medium", norm) && norm == "medium", "Enum valid option must pass");
    check(!aeris::ShaderPackParser::validateParameterValue(enumParam, "ultra", norm), "Enum invalid option must fail");

    // 3.4. Colores
    aeris::ShaderParameter colorParam;
    colorParam.type = aeris::ParameterType::Color;

    check(aeris::ShaderPackParser::validateParameterValue(colorParam, "#FFAA00", norm), "Valid hex color must pass");
    check(!aeris::ShaderPackParser::validateParameterValue(colorParam, "#GGAABB", norm), "Hex color with non-hex char must fail");
    check(!aeris::ShaderPackParser::validateParameterValue(colorParam, "#123", norm), "Short hex color must fail");
    check(!aeris::ShaderPackParser::validateParameterValue(colorParam, "FFAA00", norm), "Hex color without # must fail");

    // =========================================================================
    // 4. Prueba del parser de shaders.properties
    // =========================================================================
    auto propsFile = tempDir / "test.properties";
    {
        std::ofstream out(propsFile);
        out << "# Test shaders.properties\n"
            << "option.VOLUMETRIC_CLOUDS =\n"
            << "option.SKY_INTENSITY = 0.0 10.0 5.0\n"
            << "option.CLOUD_STEPS = 16 128 64\n"
            << "option.QUALITY = low medium high ultra\n"
            << "screen = MAIN\n"
            << "screen.MAIN = SKY CLOUDS\n"
            << "screen.MAIN.SKY = SKY_INTENSITY\n"
            << "sliders = SUN_SCALE\n"
            << "prebuilt = custom/output\n"
            << "profiles = windows gles\n";
    }

    auto params = aeris::ShaderPackParser::parseProperties(propsFile);
    check(!params.empty(), "parseProperties must return parsed parameters");

    auto itCloud = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "VOLUMETRIC_CLOUDS"; });
    check(itCloud != params.end(), "VOLUMETRIC_CLOUDS must be parsed");
    if (itCloud != params.end()) {
        check(itCloud->type == aeris::ParameterType::Bool, "VOLUMETRIC_CLOUDS must be of type Bool");
        check(itCloud->current == "OFF", "Empty boolean must default to OFF");
    }

    auto itSky = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "SKY_INTENSITY"; });
    check(itSky != params.end(), "SKY_INTENSITY must be parsed");
    if (itSky != params.end()) {
        check(itSky->type == aeris::ParameterType::Slider, "SKY_INTENSITY must be of type Slider");
        check(!itSky->isInteger, "SKY_INTENSITY must be float slider");
        check(itSky->minVal == 0.0 && itSky->maxVal == 10.0, "SKY_INTENSITY range must be 0.0 to 10.0");
        check(itSky->defaultValue == "5.0", "SKY_INTENSITY default must be 5.0");
    }

    auto itSteps = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "CLOUD_STEPS"; });
    check(itSteps != params.end(), "CLOUD_STEPS must be parsed");
    if (itSteps != params.end()) {
        check(itSteps->isInteger, "CLOUD_STEPS must be integer slider");
    }

    auto itQual = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "QUALITY"; });
    check(itQual != params.end(), "QUALITY must be parsed");
    if (itQual != params.end()) {
        check(itQual->type == aeris::ParameterType::Enum, "QUALITY must be of type Enum");
        check(itQual->options.size() == 4, "QUALITY must have 4 options");
        check(itQual->current == "low", "QUALITY must default to first option (low)");
    }

    auto profiles = aeris::ShaderPackParser::parseProfiles(propsFile);
    check(profiles.size() == 2 && profiles[0] == "windows", "parseProfiles must extract profiles");
    check(aeris::ShaderPackParser::parsePrebuiltDirective(propsFile) == "custom/output", "parsePrebuiltDirective must match");

    // =========================================================================
    // 5. Metadatos y preservación del orden explícito de subpacks
    // =========================================================================
    auto metaDir = tempDir / "meta_pack";
    std::filesystem::create_directories(metaDir);
    {
        std::ofstream manifest(metaDir / "manifest.json");
        manifest << "{\n"
                 << "  \"header\": {\n"
                 << "    \"name\": \"Aeris Sample Shader\",\n"
                 << "    \"description\": \"A sample shader pack\",\n"
                 << "    \"version\": [1, 2, 0]\n"
                 << "  },\n"
                 << "  \"subpacks\": [\n"
                 << "    { \"folder_name\": \"novc\", \"name\": \"No Volumetric Clouds\" },\n"
                 << "    { \"folder_name\": \"vc\", \"name\": \"Volumetric Clouds\" }\n"
                 << "  ]\n"
                 << "}\n";
    }

    std::vector<aeris::SubpackVariant> subpacks;
    auto meta = aeris::ShaderPackParser::parseMetadata(metaDir, subpacks);
    check(meta.name == "Aeris Sample Shader", "parseMetadata must read header name");
    check(meta.version == "1.2.0", "parseMetadata must format version array to string");
    check(subpacks.size() == 2, "parseMetadata must extract 2 subpacks");
    check(subpacks[0].folderName == "novc" && subpacks[1].folderName == "vc",
        "Author-defined subpack order must be preserved without alphabetical reordering");

    // =========================================================================
    // 6. Seguridad en extracción ZIP y protección Zip Slip / Bombs
    // =========================================================================
    auto archivesDir = tempDir / "archives";
    auto extractDest = tempDir / "extracted_pack";
    std::filesystem::create_directories(archivesDir);

    // 6.1. Archivo malicioso con Path Traversal ("../evil.txt")
    auto evilZip = archivesDir / "evil_traversal.zip";
    createTestZip(evilZip, {
        {"shaders.properties", "option.TEST=ON\n"},
        {"../evil.txt", "MALICIOUS PAYLOAD"}
    });

    check(!aeris::ShaderPackManager::extractArchive(evilZip, extractDest),
        "extractArchive must reject zip containing path traversal entry");
    check(!std::filesystem::exists(tempDir / "evil.txt"),
        "Malicious payload must not be written outside target directory");

    // 6.2. Archivo corrupto (no es formato zip)
    auto corruptZip = archivesDir / "corrupt.zip";
    {
        std::ofstream corruptOut(corruptZip, std::ios::binary);
        corruptOut << "RANDOM_CORRUPT_BYTES_NOT_A_ZIP";
    }
    check(!aeris::ShaderPackManager::extractArchive(corruptZip, extractDest),
        "extractArchive must reject corrupt file");

    // 6.3. Archivo que supera el límite de cantidad de archivos (Zip Bomb)
    auto bombZip = archivesDir / "bomb.zip";
    createTestZip(bombZip, {
        {"shaders.properties", "option.T=ON\n"},
        {"file1.txt", "data1"},
        {"file2.txt", "data2"}
    });

    aeris::ArchiveLimits tinyLimits;
    tinyLimits.maxFiles = 2; // Solo permite 2 archivos; el zip tiene 3
    check(!aeris::ShaderPackManager::extractArchive(bombZip, extractDest, tinyLimits),
        "extractArchive must enforce maxFiles limit against archive bombs");

    // 6.4. Archivo que supera el límite de tamaño individual
    aeris::ArchiveLimits sizeLimits;
    sizeLimits.maxSingleFileBytes = 5; // Limitar a 5 bytes
    check(!aeris::ShaderPackManager::extractArchive(bombZip, extractDest, sizeLimits),
        "extractArchive must enforce maxSingleFileBytes limit");

    // 6.5. Extracción de paquete válido
    auto validZip = archivesDir / "valid_shader.zip";
    createTestZip(validZip, {
        {"shaders.properties", "option.SUN_LIGHT=ON\n"},
        {"manifest.json", "{\"header\":{\"name\":\"Valid Pack\"}}"}
    });

    check(aeris::ShaderPackManager::extractArchive(validZip, extractDest),
        "extractArchive must succeed for valid shader archive");
    check(std::filesystem::is_regular_file(extractDest / "shaders.properties"),
        "Extracted destination must contain shaders.properties after valid extraction");
    check(!std::filesystem::exists(extractDest.parent_path() / (extractDest.filename().string() + ".staging_tmp")),
        "Staging directory must be cleaned up after extraction");

    // =========================================================================
    // 7. Detección de modos Forward vs Deferred
    // =========================================================================
    auto forwardDir = tempDir / "forward_bins";
    auto deferredDir = tempDir / "deferred_bins";
    std::filesystem::create_directories(forwardDir);
    std::filesystem::create_directories(deferredDir);

    {
        std::ofstream f(forwardDir / "RenderChunk.material.bin");
        f << "bin";
        std::ofstream d(deferredDir / "RenderChunkDeferred.material.bin");
        d << "bin";
    }

    check(aeris::ShaderPackParser::detectRenderMode(forwardDir) == aeris::ShaderRenderMode::Forward,
        "Folder with RenderChunk.material.bin must detect Forward mode");
    check(aeris::ShaderPackParser::detectRenderMode(deferredDir) == aeris::ShaderRenderMode::Deferred,
        "Folder with *Deferred*.material.bin must detect Deferred mode");

    // =========================================================================
    // 8. Persistencia y validación al cargar ajustes (.txt)
    // =========================================================================
    aeris::ShaderPack testPack;
    testPack.name = "TestPack";
    testPack.parameters = params;
    testPack.settingsPath = tempDir / "TestPack.txt";
    testPack.activeSubpack = "novc";

    // Guardar ajustes válidos
    testPack.parameters[0].current = "ON";
    check(aeris::ShaderPackParser::saveSettings(testPack, testPack.settingsPath), "saveSettings must succeed");

    // Cargar en un pack fresco
    aeris::ShaderPack loadedPack;
    loadedPack.name = "TestPack";
    loadedPack.parameters = params;
    loadedPack.subpacks = subpacks;

    check(aeris::ShaderPackParser::loadSettings(loadedPack, testPack.settingsPath), "loadSettings must succeed");
    check(loadedPack.parameters[0].current == "ON", "After load, param must reflect saved value ON");
    check(loadedPack.activeSubpack == "novc", "After load, subpack must reflect saved subpack 'novc'");

    // Probar archivo de configuración con valores corruptos/inválidos
    auto corruptTxt = tempDir / "CorruptSettings.txt";
    {
        std::ofstream cOut(corruptTxt);
        cOut << "VOLUMETRIC_CLOUDS = invalid_bool_string\n"
             << "SKY_INTENSITY = 99999.0\n"; // Fuera del rango [0.0, 10.0]
    }

    aeris::ShaderPack corruptLoadedPack;
    corruptLoadedPack.name = "CorruptTest";
    corruptLoadedPack.parameters = params; // defaults: VOLUMETRIC_CLOUDS=OFF, SKY_INTENSITY=5.0

    check(aeris::ShaderPackParser::loadSettings(corruptLoadedPack, corruptTxt), "loadSettings with corrupt values must not throw");
    check(corruptLoadedPack.parameters[0].current == "OFF", "Corrupt bool must be rejected and preserve default OFF");
    auto itSkyCorrupt = std::find_if(corruptLoadedPack.parameters.begin(), corruptLoadedPack.parameters.end(),
        [](auto const& p) { return p.name == "SKY_INTENSITY"; });
    if (itSkyCorrupt != corruptLoadedPack.parameters.end()) {
        check(itSkyCorrupt->current == "5.0", "Out-of-range slider must be rejected and preserve default 5.0");
    }

    // =========================================================================
    // 9. Pruebas de integración del gestor de paquetes (ShaderPackManager)
    // =========================================================================
    auto scanPacksRoot = tempDir / "shaderpacks";
    auto scanGenRoot   = tempDir / "generated";
    std::filesystem::create_directories(scanPacksRoot);

    auto packFolder1 = scanPacksRoot / "PackOne";
    auto packFolder2 = scanPacksRoot / "PackTwo";
    std::filesystem::create_directories(packFolder1);
    std::filesystem::create_directories(packFolder2);

    {
        std::ofstream p1(packFolder1 / "shaders.properties");
        p1 << "option.BLOOM = ON\n";
        std::ofstream b1(packFolder1 / "Standard.material.bin");
        b1 << "mat";

        std::ofstream p2(packFolder2 / "shaders.properties");
        p2 << "option.RAYTRACING = OFF\n";
        std::ofstream b2(packFolder2 / "TerrainDeferred.material.bin");
        b2 << "mat";
    }

    aeris::ShaderPackManager& spMgr = aeris::ShaderPackManager::getInstance();
    spMgr.scan(scanPacksRoot, scanGenRoot);

    check(spMgr.packs().size() == 2, "ShaderPackManager must discover both directory packs");

    auto const* foundP1 = spMgr.findPack("PackOne");
    auto const* foundP2 = spMgr.findPack("PackTwo");
    check(foundP1 != nullptr && foundP2 != nullptr, "findPack must locate both packs by name");

    if (foundP1) {
        check(foundP1->renderMode == aeris::ShaderRenderMode::Forward, "PackOne must be Forward");
        check(!foundP1->parameters.empty() && foundP1->parameters[0].name == "BLOOM", "PackOne BLOOM param parsed");
    }
    if (foundP2) {
        check(foundP2->renderMode == aeris::ShaderRenderMode::Deferred, "PackTwo must be Deferred");
    }

    // 9.1. Transaccionalidad al modificar parámetros
    check(spMgr.setParameterValue("PackOne", "BLOOM", "OFF"), "Valid parameter update must succeed");
    check(foundP1->parameters[0].current == "OFF", "Parameter value must be updated in memory");
    check(!spMgr.setParameterValue("PackOne", "BLOOM", "invalid_value"), "Invalid parameter update must be rejected");
    check(foundP1->parameters[0].current == "OFF", "Parameter must preserve previous value when rejected");

    // 9.2. Transaccionalidad en selección y desactivación
    check(spMgr.selectPack("PackOne"), "selectPack(PackOne) must succeed");
    check(spMgr.activePack() != nullptr && spMgr.activePack()->name == "PackOne", "activePack must be PackOne");
    check(!spMgr.selectPack("NonExistentPack"), "selectPack with invalid pack must fail");
    check(spMgr.activePack() != nullptr && spMgr.activePack()->name == "PackOne", "Failed selection must not alter active pack");

    spMgr.deactivatePack();
    check(spMgr.activePack() == nullptr, "deactivatePack must clear active pack");

    // 9.3. Seguridad en deletePack
    std::string deleteErr;
    check(!spMgr.deletePack("../outside", deleteErr), "deletePack must reject path traversal names");
    check(!spMgr.deletePack("..", deleteErr), "deletePack must reject '..' name");

    check(spMgr.deletePack("PackOne", deleteErr), "deletePack on valid managed pack must succeed");
    check(spMgr.findPack("PackOne") == nullptr, "Deleted pack must no longer be present");
    check(!std::filesystem::exists(packFolder1), "Pack folder must be deleted from disk");

    std::filesystem::remove_all(tempDir);
}
