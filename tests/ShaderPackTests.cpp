#include "TestRunner.h"
#include "shaders/ShaderPack.h"
#include "shaders/ShaderPackManager.h"
#include "shaders/ShaderPackParser.h"
#include "shaders/ShaderParameter.h"

#include <filesystem>
#include <fstream>

void runShaderPackTests() {
    std::cout << "\nRunning ShaderPackManager & Parser Tests...\n";

    auto tempDir = std::filesystem::temp_directory_path() / "aeris_test_shaderpacks";
    std::filesystem::create_directories(tempDir);

    // 1. Prueba de formatos de archivo soportados (.zip y .mcpack)
    check(aeris::ShaderPackManager::isSupportedArchive("Pack.zip"), ".zip must be supported");
    check(aeris::ShaderPackManager::isSupportedArchive("Shader.mcpack"), ".mcpack must be supported");
    check(aeris::ShaderPackManager::isSupportedArchive("SHADER.MCPACK"), "Uppercase .MCPACK must be supported");
    check(!aeris::ShaderPackManager::isSupportedArchive("pack.txt"), ".txt must not be supported");
    check(!aeris::ShaderPackManager::isSupportedArchive("pack.tar"), ".tar must not be supported");

    // 2. Prueba de protección contra Path Traversal
    check(!aeris::ShaderPackManager::extractArchive("test.zip", tempDir / ".." / "escaped"),
        "extractArchive must reject destination paths containing ..");

    // 3. Prueba de parser de shaders.properties
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

    // Verificar VOLUMETRIC_CLOUDS (Bool)
    auto itCloud = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "VOLUMETRIC_CLOUDS"; });
    check(itCloud != params.end(), "VOLUMETRIC_CLOUDS must be parsed");
    if (itCloud != params.end()) {
        check(itCloud->type == aeris::ParameterType::Bool, "VOLUMETRIC_CLOUDS must be of type Bool");
        check(itCloud->current == "OFF", "Empty boolean must default to OFF");
    }

    // Verificar SKY_INTENSITY (Slider Float)
    auto itSky = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "SKY_INTENSITY"; });
    check(itSky != params.end(), "SKY_INTENSITY must be parsed");
    if (itSky != params.end()) {
        check(itSky->type == aeris::ParameterType::Slider, "SKY_INTENSITY must be of type Slider");
        check(!itSky->isInteger, "SKY_INTENSITY must be float slider");
        check(itSky->minVal == 0.0 && itSky->maxVal == 10.0, "SKY_INTENSITY range must be 0.0 to 10.0");
        check(itSky->defaultValue == "5.0", "SKY_INTENSITY default must be 5.0");
    }

    // Verificar CLOUD_STEPS (Slider Integer)
    auto itSteps = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "CLOUD_STEPS"; });
    check(itSteps != params.end(), "CLOUD_STEPS must be parsed");
    if (itSteps != params.end()) {
        check(itSteps->isInteger, "CLOUD_STEPS must be integer slider");
    }

    // Verificar QUALITY (Enum)
    auto itQual = std::find_if(params.begin(), params.end(), [](auto const& p) { return p.name == "QUALITY"; });
    check(itQual != params.end(), "QUALITY must be parsed");
    if (itQual != params.end()) {
        check(itQual->type == aeris::ParameterType::Enum, "QUALITY must be of type Enum");
        check(itQual->options.size() == 4, "QUALITY must have 4 options");
        check(itQual->current == "low", "QUALITY must default to first option (low)");
    }

    // Verificar perfiles y directiva prebuilt
    auto profiles = aeris::ShaderPackParser::parseProfiles(propsFile);
    check(profiles.size() == 2 && profiles[0] == "windows", "parseProfiles must extract profiles");
    check(aeris::ShaderPackParser::parsePrebuiltDirective(propsFile) == "custom/output", "parsePrebuiltDirective must match");

    // 4. Prueba de metadatos desde manifest.json
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
    check(subpacks[0].folderName == "novc" && subpacks[1].folderName == "vc", "Subpack folder names must match");

    // 5. Prueba de detección de modo de renderizado (Deferred vs Forward)
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

    // 6. Prueba de persistencia de ajustes (.txt)
    aeris::ShaderPack testPack;
    testPack.name = "TestPack";
    testPack.parameters = params;
    testPack.settingsPath = tempDir / "TestPack.txt";

    // Modificar un parámetro
    testPack.parameters[0].current = "ON";
    check(aeris::ShaderPackParser::saveSettings(testPack, testPack.settingsPath), "saveSettings must succeed");

    // Crear un pack nuevo y cargar los ajustes
    aeris::ShaderPack loadedPack;
    loadedPack.name = "TestPack";
    loadedPack.parameters = params; // copia con valores por defecto
    check(loadedPack.parameters[0].current == "OFF", "Before load, param must be default OFF");

    check(aeris::ShaderPackParser::loadSettings(loadedPack, testPack.settingsPath), "loadSettings must succeed");
    check(loadedPack.parameters[0].current == "ON", "After load, param must reflect saved value ON");

    // 7. Prueba integral de escaneo con ShaderPackManager
    auto scanPacksRoot = tempDir / "shaderpacks";
    auto scanGenRoot   = tempDir / "generated";
    std::filesystem::create_directories(scanPacksRoot);

    auto packFolder1 = scanPacksRoot / "PackOne";
    auto packFolder2 = scanPacksRoot / "PackTwo";
    std::filesystem::create_directories(packFolder1);
    std::filesystem::create_directories(packFolder2);

    // Añadir marker y binario a cada carpeta
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

    // Probar selección de pack activo
    check(spMgr.selectPack("PackOne"), "selectPack(PackOne) must succeed");
    check(spMgr.activePack() != nullptr && spMgr.activePack()->name == "PackOne", "activePack must be PackOne");

    spMgr.deactivatePack();
    check(spMgr.activePack() == nullptr, "deactivatePack must clear active pack");

    std::filesystem::remove_all(tempDir);
}
