#include "shaders/ShaderPackManager.h"

#include <algorithm>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include "core/ConfigManager.h"
#include "shaders/ShaderPackParser.h"

namespace aeris {

namespace {

std::string toLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

#ifdef _WIN32
int runHidden(std::wstring cmdline) {
    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            nullptr,
            cmdline.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi
        )) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
#endif

bool hasPackMarker(std::filesystem::path const& dir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(dir / "project.json", ec)
        || std::filesystem::is_regular_file(dir / "shaders.properties", ec)
        || std::filesystem::is_regular_file(dir / "manifest.json", ec)
        || ShaderPackParser::hasMaterialBin(dir);
}

} // namespace

ShaderPackManager& ShaderPackManager::getInstance() {
    static ShaderPackManager instance;
    return instance;
}

bool ShaderPackManager::isSupportedArchive(std::filesystem::path const& file) {
    auto ext = toLowerStr(file.extension().string());
    return ext == ".zip" || ext == ".mcpack";
}

bool ShaderPackManager::extractArchive(std::filesystem::path const& archive, std::filesystem::path const& destDir) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(archive, ec)) return false;

    // Validación contra Path Traversal en rutas de origen y destino
    auto destStr = destDir.generic_string();
    if (destStr.find("..") != std::string::npos) return false;

    std::filesystem::create_directories(destDir, ec);
    if (ec) return false;

#ifdef _WIN32
    // Windows 10/11 incluye tar.exe nativo capaz de descomprimir archivos zip y mcpack
    std::wstring cmd = L"tar.exe -xf \"" + archive.wstring() + L"\" -C \"" + destDir.wstring() + L"\"";
    int code = runHidden(std::move(cmd));
    if (code != 0) return false;

    // Verificación de integridad post-extracción
    return std::filesystem::is_directory(destDir, ec);
#else
    return false;
#endif
}

bool ShaderPackManager::ensureExtracted(std::filesystem::path const& archive, std::filesystem::path const& dest) {
    std::error_code ec;
    auto lastWrite = std::filesystem::last_write_time(archive, ec);
    if (ec) return false;

    auto mtime = static_cast<long long>(lastWrite.time_since_epoch().count());
    auto marker = dest / ".source.timestamp";

    if (std::filesystem::is_regular_file(marker, ec)) {
        std::ifstream in(marker);
        long long storedTime = 0;
        if ((in >> storedTime) && storedTime == mtime) {
            return true; // Ya extraído y actualizado
        }
    }

    std::filesystem::remove_all(dest, ec);
    std::filesystem::create_directories(dest, ec);

    if (!extractArchive(archive, dest)) {
        return false;
    }

    std::ofstream out(marker);
    out << mtime;
    return true;
}

std::filesystem::path ShaderPackManager::normalizeSourceDir(std::filesystem::path const& dir) {
    std::error_code ec;
    if (hasPackMarker(dir)) return dir;

    // Paquetes estructurados en subcarpeta src/
    if (hasPackMarker(dir / "src")) return dir / "src";

    // Envoltorio de un solo subdirectorio (ej: repo-main/)
    std::vector<std::filesystem::path> subdirs;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_directory(ec)) subdirs.push_back(e.path());
    }
    if (subdirs.size() == 1) {
        auto inner = normalizeSourceDir(subdirs.front());
        if (hasPackMarker(inner) || std::filesystem::is_directory(inner / "renderer", ec)) {
            return inner;
        }
    }
    return dir;
}

ShaderPack const* ShaderPackManager::findPack(std::string_view name) const {
    for (auto const& p : mPacks) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

ShaderPack* ShaderPackManager::findPackMutable(std::string_view name) {
    for (auto& p : mPacks) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

ShaderPack const* ShaderPackManager::activePack() const {
    auto const& activeName = ConfigManager::getInstance().config().shaders.activePack;
    if (activeName.empty() || activeName == "OFF" || activeName == "NONE") {
        return nullptr;
    }
    return findPack(activeName);
}

bool ShaderPackManager::selectPack(std::string_view name) {
    if (name.empty() || name == "OFF" || name == "NONE") {
        deactivatePack();
        return true;
    }

    auto* pack = findPack(name);
    if (!pack) return false;

    ConfigManager::getInstance().config().shaders.activePack = std::string(name);
    return ConfigManager::getInstance().save();
}

void ShaderPackManager::deactivatePack() {
    ConfigManager::getInstance().config().shaders.activePack.clear();
    ConfigManager::getInstance().save();
}

bool ShaderPackManager::setParameterValue(std::string_view packName, std::string_view paramName, std::string value) {
    auto* pack = findPackMutable(packName);
    if (!pack) return false;

    for (auto& p : pack->parameters) {
        if (p.name == paramName) {
            p.current = std::move(value);
            return ShaderPackParser::saveSettings(*pack, pack->settingsPath);
        }
    }
    return false;
}

bool ShaderPackManager::importPack(std::filesystem::path const& sourceFile, std::string& outError) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(sourceFile, ec)) {
        outError = "File does not exist: " + sourceFile.string();
        return false;
    }

    if (!isSupportedArchive(sourceFile)) {
        outError = "Unsupported format: only .zip and .mcpack are supported";
        return false;
    }

    std::filesystem::create_directories(mPacksRoot, ec);
    auto dest = mPacksRoot / sourceFile.filename();

    std::filesystem::copy_file(sourceFile, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "Failed to copy file: " + ec.message();
        return false;
    }

    scan(mPacksRoot, mGeneratedRoot);
    return true;
}

bool ShaderPackManager::deletePack(std::string_view name, std::string& outError) {
    auto* pack = findPack(name);
    if (!pack) {
        outError = "Pack not found: " + std::string(name);
        return false;
    }

    std::error_code ec;
    if (!pack->archivePath.empty()) {
        std::filesystem::remove(pack->archivePath, ec);
    } else {
        std::filesystem::remove_all(pack->sourceDir, ec);
    }

    // Limpiar directorio generado y archivo de ajustes
    std::filesystem::remove_all(mGeneratedRoot / std::string(name), ec);
    std::filesystem::remove(pack->settingsPath, ec);

    if (ConfigManager::getInstance().config().shaders.activePack == name) {
        deactivatePack();
    }

    scan(mPacksRoot, mGeneratedRoot);
    return true;
}

void ShaderPackManager::scan(std::filesystem::path const& packsRoot, std::filesystem::path const& generatedRoot) {
    mPacksRoot     = packsRoot;
    mGeneratedRoot = generatedRoot;
    mPacks.clear();

    std::error_code ec;
    if (!std::filesystem::is_directory(packsRoot, ec)) {
        std::filesystem::create_directories(packsRoot, ec);
        return;
    }

    for (auto const& entry : std::filesystem::directory_iterator(packsRoot, ec)) {
        auto fileName = entry.path().filename().string();
        if (fileName.empty() || fileName[0] == '.' || fileName[0] == '_') continue;

        // Caso 1: Directorio descomprimido
        if (entry.is_directory(ec)) {
            if (toLowerStr(fileName) == "vanilla") continue;
            if (!hasPackMarker(entry.path())) continue;

            auto bins = generatedRoot / fileName / "compiled";
            mPacks.push_back(
                loadPack(entry.path(), {}, bins, packsRoot / (fileName + ".txt"))
            );
            continue;
        }

        // Caso 2: Archivo comprimido (.zip o .mcpack)
        if (!entry.is_regular_file(ec)) continue;
        if (!isSupportedArchive(entry.path())) continue;

        auto stemName = entry.path().stem().string();
        auto srcDir   = generatedRoot / stemName / "src";
        auto binsDir  = generatedRoot / stemName / "compiled";

        if (!ensureExtracted(entry.path(), srcDir)) {
            // Incluso si la extracción falla, creamos la entrada para que sea visible en la UI
            mPacks.push_back(
                loadPack(srcDir, entry.path(), binsDir, packsRoot / (fileName + ".txt"), srcDir)
            );
            continue;
        }

        mPacks.push_back(
            loadPack(normalizeSourceDir(srcDir), entry.path(), binsDir, packsRoot / (fileName + ".txt"), srcDir)
        );
    }
}

ShaderPack ShaderPackManager::loadPack(
    std::filesystem::path const& sourceDir,
    std::filesystem::path const& packArchive,
    std::filesystem::path const& binsDir,
    std::filesystem::path const& settingsPath,
    std::filesystem::path const& containerDir
) {
    ShaderPack pack;
    if (!packArchive.empty()) {
        pack.name = packArchive.stem().string();
    } else {
        pack.name = sourceDir.filename().string();
    }

    pack.sourceDir    = sourceDir;
    pack.archivePath  = packArchive;
    pack.binsDir      = binsDir;
    pack.settingsPath = settingsPath;

    // Metadatos y subpacks
    pack.metadata = ShaderPackParser::parseMetadata(sourceDir, pack.subpacks);
    if (pack.metadata.name.empty()) {
        pack.metadata.name = pack.name;
    }

    // Propiedades y perfiles
    pack.profiles   = ShaderPackParser::parseProfiles(sourceDir / "shaders.properties");
    pack.parameters = ShaderPackParser::parseProperties(sourceDir / "shaders.properties");

    // Cargar ajustes guardados del usuario (.txt)
    ShaderPackParser::loadSettings(pack, settingsPath);

    // Búsqueda de materiales precompilados
    std::vector<std::filesystem::path> prebuiltRoots;
    prebuiltRoots.push_back(sourceDir);
    if (!containerDir.empty() && containerDir != sourceDir) {
        std::error_code e;
        if (std::filesystem::is_directory(containerDir, e)) {
            prebuiltRoots.push_back(containerDir);
        }
    }

    for (auto const& root : prebuiltRoots) {
        pack.prebuiltDir = ShaderPackParser::findPrebuiltMaterials(root);
        if (!pack.prebuiltDir.empty()) break;
    }

    // Copiar materiales precompilados al directorio binsDir (compiled/)
    if (!ShaderPackParser::hasMaterialBin(pack.binsDir)) {
        std::error_code ec;
        std::vector<std::filesystem::path> dirs;
        auto addDir = [&](std::filesystem::path const& d) {
            std::error_code e;
            if (!std::filesystem::is_directory(d, e) || !ShaderPackParser::hasMaterialBin(d)) return;
            if (std::find(dirs.begin(), dirs.end(), d) != dirs.end()) return;
            dirs.push_back(d);
        };

        if (!pack.prebuiltDir.empty()) addDir(pack.prebuiltDir);
        for (auto const& root : prebuiltRoots) {
            for (auto const& d : ShaderPackParser::collectPrebuiltDirs(root)) {
                addDir(d);
            }
        }

        if (!dirs.empty()) {
            std::filesystem::create_directories(pack.binsDir, ec);
            for (auto const& dir : dirs) {
                for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
                    if (!e.is_regular_file(ec)) continue;
                    auto filename = e.path().filename().string();
                    if (filename.ends_with(".material.bin")) {
                        std::filesystem::copy_file(
                            e.path(),
                            pack.binsDir / filename,
                            std::filesystem::copy_options::overwrite_existing,
                            ec
                        );
                    }
                }
            }
        }
    }

    // Detección de render mode (Forward vs Deferred)
    pack.renderMode = ShaderPackParser::detectRenderMode(
        ShaderPackParser::hasMaterialBin(pack.binsDir) ? pack.binsDir : pack.prebuiltDir
    );

    return pack;
}

} // namespace aeris
