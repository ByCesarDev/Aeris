#include "shaders/MaterialLoader.h"
#include "shaders/ShaderPackManager.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "miniz/miniz.h"

#include "core/ConfigManager.h"
#include "shaders/ShaderPackParser.h"

namespace aeris {

namespace {

std::string toLowerStr(std::string_view sv) {
    std::string s;
    s.reserve(sv.size());
    for (char c : sv) {
        s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return s;
}

bool hasPackMarker(std::filesystem::path const& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return false;

    if (std::filesystem::is_regular_file(dir / "shaders.properties", ec)) return true;
    if (std::filesystem::is_regular_file(dir / "manifest.json", ec)) return true;
    if (std::filesystem::is_directory(dir / "renderer", ec)) return true;
    if (std::filesystem::is_directory(dir / "subpacks", ec)) return true;

    return false;
}

bool isSafeEntryPath(std::string_view entryName, std::size_t maxDepth) {
    if (entryName.empty()) return false;
    if (entryName.find('\0') != std::string_view::npos) return false;

    // Rechazar rutas absolutas o letras de unidad Windows
    if (entryName.front() == '/' || entryName.front() == '\\') return false;
    if (entryName.size() >= 2 && entryName[1] == ':') return false;

    std::size_t depth = 0;
    std::size_t start = 0;
    while (start < entryName.size()) {
        std::size_t next = entryName.find_first_of("/\\", start);
        std::string_view part = (next == std::string_view::npos)
            ? entryName.substr(start)
            : entryName.substr(start, next - start);

        if (!part.empty()) {
            if (part == "..") return false; // Intento de Path Traversal
            if (part.find(':') != std::string_view::npos) return false; // Stream NTFS
            depth++;
            if (depth > maxDepth) return false;
        }

        if (next == std::string_view::npos) break;
        start = next + 1;
    }
    return true;
}

std::uint32_t computeFileCrc32(std::filesystem::path const& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return 0;

    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, nullptr, 0);
    std::vector<unsigned char> buffer(65536);

    while (stream) {
        stream.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        std::streamsize bytes = stream.gcount();
        if (bytes > 0) {
            crc = mz_crc32(crc, buffer.data(), static_cast<std::size_t>(bytes));
        }
    }
    return static_cast<std::uint32_t>(crc);
}

} // namespace

std::string ShaderPackManager::generateUniqueSuffix() {
    static std::atomic<std::uint64_t> sCounter{0};
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto pid =
#ifdef _WIN32
        GetCurrentProcessId();
#else
        getpid();
#endif
    return std::to_string(pid) + "_" + std::to_string(now) + "_" + std::to_string(++sCounter);
}

bool ShaderPackManager::isPathInside(std::filesystem::path const& base, std::filesystem::path const& target) {
    std::error_code ec;
    if (base.empty() || target.empty()) return false;

    auto cBase   = std::filesystem::weakly_canonical(base, ec);
    if (ec) return false;

    auto cTarget = std::filesystem::weakly_canonical(target, ec);
    if (ec) return false;

    auto rel = cTarget.lexically_relative(cBase);
    if (rel.empty()) return false;

    auto s = rel.generic_string();
    return s != "." && !s.starts_with("..") && !rel.is_absolute();
}

ShaderPackManager& ShaderPackManager::getInstance() {
    static ShaderPackManager instance;
    return instance;
}

bool ShaderPackManager::isSupportedArchive(std::filesystem::path const& file) {
    auto ext = toLowerStr(file.extension().string());
    return ext == ".zip" || ext == ".mcpack";
}

bool ShaderPackManager::validateArchiveIntegrity(
    std::filesystem::path const& archive,
    ArchiveLimits const& limits,
    std::string* outError
) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(archive, ec)) {
        if (outError) *outError = "Archive file not found: " + archive.string();
        return false;
    }

    FILE* fp = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&fp, archive.wstring().c_str(), L"rb") != 0 || !fp) {
        if (outError) *outError = "Could not open archive file";
        return false;
    }
#else
    fp = std::fopen(archive.string().c_str(), "rb");
    if (!fp) {
        if (outError) *outError = "Could not open archive file";
        return false;
    }
#endif

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_cfile(&zip, fp, 0, 0)) {
        std::fclose(fp);
        if (outError) *outError = "Not a valid ZIP or MCPACK archive";
        return false;
    }

    auto cleanup = [&]() {
        mz_zip_reader_end(&zip);
        std::fclose(fp);
    };

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    if (numFiles == 0) {
        cleanup();
        if (outError) *outError = "Archive is empty (0 files)";
        return false;
    }

    if (numFiles > limits.maxFiles) {
        cleanup();
        if (outError) *outError = "Archive exceeds maximum file count limit";
        return false;
    }

    std::uint64_t totalUncompressed = 0;
    bool hasRecognizedContent = false;

    for (mz_uint i = 0; i < numFiles; ++i) {
        char filenameBuf[1024];
        if (!mz_zip_reader_get_filename(&zip, i, filenameBuf, sizeof(filenameBuf))) {
            cleanup();
            if (outError) *outError = "Corrupt entry filename in archive";
            return false;
        }

        std::string entryName(filenameBuf);
        if (!isSafeEntryPath(entryName, limits.maxDirectoryDepth)) {
            cleanup();
            if (outError) *outError = "Path traversal or unsafe entry detected in archive: " + entryName;
            return false;
        }

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            cleanup();
            if (outError) *outError = "Corrupt entry stat in archive";
            return false;
        }

        bool isSymlink = ((stat.m_version_made_by >> 8) == 3 && ((stat.m_external_attr >> 16) & 0170000) == 0120000);
        if (isSymlink) {
            cleanup();
            if (outError) *outError = "Symbolic links not permitted in archive: " + entryName;
            return false;
        }

        if (stat.m_uncomp_size > limits.maxSingleFileBytes) {
            cleanup();
            if (outError) *outError = "File exceeds single file size limit: " + entryName;
            return false;
        }

        totalUncompressed += stat.m_uncomp_size;
        if (totalUncompressed > limits.maxTotalUncompressedBytes) {
            cleanup();
            if (outError) *outError = "Archive exceeds total uncompressed size limit";
            return false;
        }

        if (stat.m_comp_size > 0 && stat.m_uncomp_size > 65536) {
            double ratio = static_cast<double>(stat.m_uncomp_size) / static_cast<double>(stat.m_comp_size);
            if (ratio > limits.maxCompressionRatio) {
                cleanup();
                if (outError) *outError = "Archive exceeds safe compression ratio limit (potential zip bomb)";
                return false;
            }
        }

        auto lowerName = toLowerStr(entryName);
        if (lowerName.ends_with("shaders.properties") ||
            lowerName.ends_with("manifest.json") ||
            lowerName.ends_with(".material.bin")) {
            hasRecognizedContent = true;
        }
    }

    cleanup();

    if (!hasRecognizedContent) {
        if (outError) *outError = "Archive does not contain recognized shader pack files (manifest.json, shaders.properties or *.material.bin)";
        return false;
    }

    return true;
}

bool ShaderPackManager::extractArchive(
    std::filesystem::path const& archive,
    std::filesystem::path const& destDir,
    ArchiveLimits const& limits
) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(archive, ec)) return false;

    // Validación previa contra Path Traversal en el destino solicitado
    auto destStr = destDir.generic_string();
    if (destStr.find("..") != std::string::npos) return false;

    FILE* fp = nullptr;
#ifdef _WIN32
    if (_wfopen_s(&fp, archive.wstring().c_str(), L"rb") != 0 || !fp) return false;
#else
    fp = std::fopen(archive.string().c_str(), "rb");
    if (!fp) return false;
#endif

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_cfile(&zip, fp, 0, 0)) {
        std::fclose(fp);
        return false;
    }

    auto cleanup = [&]() {
        mz_zip_reader_end(&zip);
        std::fclose(fp);
    };

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    if (numFiles == 0 || numFiles > limits.maxFiles) {
        cleanup();
        return false;
    }

    // 1. Inspección exhaustiva de todas las entradas ANTES de extraer
    std::uint64_t totalUncompressed = 0;
    std::vector<std::string> entryNames;
    entryNames.reserve(numFiles);

    for (mz_uint i = 0; i < numFiles; ++i) {
        char filenameBuf[1024];
        if (!mz_zip_reader_get_filename(&zip, i, filenameBuf, sizeof(filenameBuf))) {
            cleanup();
            return false;
        }

        std::string entryName(filenameBuf);
        if (!isSafeEntryPath(entryName, limits.maxDirectoryDepth)) {
            cleanup();
            return false;
        }

        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            cleanup();
            return false;
        }

        bool isSymlink = ((stat.m_version_made_by >> 8) == 3 && ((stat.m_external_attr >> 16) & 0170000) == 0120000);
        if (isSymlink) {
            cleanup();
            return false;
        }

        if (stat.m_uncomp_size > limits.maxSingleFileBytes) {
            cleanup();
            return false;
        }

        totalUncompressed += stat.m_uncomp_size;
        if (totalUncompressed > limits.maxTotalUncompressedBytes) {
            cleanup();
            return false;
        }

        if (stat.m_comp_size > 0 && stat.m_uncomp_size > 65536) {
            double ratio = static_cast<double>(stat.m_uncomp_size) / static_cast<double>(stat.m_comp_size);
            if (ratio > limits.maxCompressionRatio) {
                cleanup();
                return false;
            }
        }

        entryNames.push_back(std::move(entryName));
    }

    // 2. Extracción aislada en directorio temporal de staging con nombre exclusivo
    auto suffix = generateUniqueSuffix();
    auto stagingDir = destDir.parent_path() / (destDir.filename().string() + ".staging_" + suffix);
    std::filesystem::remove_all(stagingDir, ec);
    std::filesystem::create_directories(stagingDir, ec);
    if (ec) {
        cleanup();
        return false;
    }

    bool extractionOk = true;
    for (mz_uint i = 0; i < numFiles; ++i) {
        auto const& entryName = entryNames[i];
        auto targetPath = stagingDir / entryName;

        if (!isPathInside(stagingDir, targetPath)) {
            extractionOk = false;
            break;
        }

        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            std::filesystem::create_directories(targetPath, ec);
            if (ec) { extractionOk = false; break; }
            continue;
        }

        std::filesystem::create_directories(targetPath.parent_path(), ec);
        if (ec) { extractionOk = false; break; }

        std::size_t uncompSize = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &uncompSize, 0);
        if (!data) {
            extractionOk = false;
            break;
        }

        std::ofstream out(targetPath, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            out.write(reinterpret_cast<char const*>(data), uncompSize);
            out.flush();
            if (!out.good()) extractionOk = false;
        } else {
            extractionOk = false;
        }

        mz_free(data);
        if (!extractionOk) break;
    }

    cleanup();

    if (!extractionOk) {
        std::filesystem::remove_all(stagingDir, ec);
        return false;
    }

    // 3. Verificación de integridad: el contenido debe ser un shaderpack válido
    bool hasValidContent = hasPackMarker(stagingDir) ||
                           hasPackMarker(stagingDir / "src") ||
                           ShaderPackParser::hasMaterialBin(stagingDir) ||
                           !ShaderPackParser::findPrebuiltMaterials(stagingDir).empty();

    if (!hasValidContent) {
        for (auto const& e : std::filesystem::directory_iterator(stagingDir, ec)) {
            if (e.is_directory(ec) && (hasPackMarker(e.path()) || !ShaderPackParser::findPrebuiltMaterials(e.path()).empty())) {
                hasValidContent = true;
                break;
            }
        }
    }

    if (!hasValidContent) {
        std::filesystem::remove_all(stagingDir, ec);
        return false;
    }

    // 4. Publicación atómica: preservar paquete anterior con nombre de backup exclusivo
    auto backupDir = destDir.parent_path() / (destDir.filename().string() + ".backup_" + suffix);
    std::filesystem::remove_all(backupDir, ec);

    bool hadExisting = std::filesystem::exists(destDir, ec);
    if (hadExisting) {
        std::filesystem::rename(destDir, backupDir, ec);
        if (ec) {
            std::filesystem::remove_all(stagingDir, ec);
            return false;
        }
    }

    std::filesystem::rename(stagingDir, destDir, ec);
    if (ec) {
        if (hadExisting) {
            std::error_code restoreEc;
            std::filesystem::rename(backupDir, destDir, restoreEc);
        }
        std::filesystem::remove_all(stagingDir, ec);
        return false;
    }

    if (hadExisting) {
        std::filesystem::remove_all(backupDir, ec);
    }

    return true;
}

bool ShaderPackManager::ensureExtracted(std::filesystem::path const& archive, std::filesystem::path const& dest) {
    std::error_code ec;
    auto lastWrite = std::filesystem::last_write_time(archive, ec);
    if (ec) return false;

    auto fileSize = std::filesystem::file_size(archive, ec);
    if (ec) return false;

    auto mtime = static_cast<long long>(lastWrite.time_since_epoch().count());
    auto crc32 = computeFileCrc32(archive);

    auto cacheMarker = dest / ".source.cache";
    if (std::filesystem::is_regular_file(cacheMarker, ec) && std::filesystem::is_directory(dest, ec)) {
        std::ifstream in(cacheMarker);
        long long storedMtime = 0;
        std::uint64_t storedSize = 0;
        std::uint32_t storedCrc  = 0;
        if ((in >> storedMtime >> storedSize >> storedCrc) &&
            storedMtime == mtime &&
            storedSize  == fileSize &&
            storedCrc   == crc32) {
            return true; // Contenido verificado e idéntico
        }
    }

    if (!extractArchive(archive, dest)) {
        return false;
    }

    std::ofstream out(cacheMarker);
    out << mtime << " " << fileSize << " " << crc32;
    return true;
}

std::filesystem::path ShaderPackManager::normalizeSourceDir(
    std::filesystem::path const& dir,
    int maxDepth,
    std::unordered_set<std::string>* visited
) {
    if (maxDepth <= 0) return dir;

    std::unordered_set<std::string> localVisited;
    if (!visited) visited = &localVisited;

    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(dir, ec).generic_string();
    if (visited->count(canonical)) return dir; // Detección y ruptura de ciclo
    visited->insert(canonical);

    if (hasPackMarker(dir)) return dir;
    if (hasPackMarker(dir / "src")) return dir / "src";

    std::vector<std::filesystem::path> subdirs;
    for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_directory(ec)) subdirs.push_back(e.path());
    }

    if (subdirs.size() == 1) {
        auto inner = normalizeSourceDir(subdirs.front(), maxDepth - 1, visited);
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
        return deactivatePack();
    }

    auto* pack = findPack(name);
    if (!pack) return false;

    auto const prevActive = ConfigManager::getInstance().config().shaders.activePack;
    ConfigManager::getInstance().config().shaders.activePack = std::string(name);

    if (!ConfigManager::getInstance().save()) {
        ConfigManager::getInstance().config().shaders.activePack = prevActive;
        return false;
    }

    // Actualizar MaterialLoader con los directorios de binarios precompilados del pack
    std::vector<std::filesystem::path> prebuiltDirs = { pack->binsDir };
    for (auto const& d : ShaderPackParser::collectPrebuiltDirs(pack->sourceDir, pack->activeSubpack)) {
        prebuiltDirs.push_back(d);
    }
    MaterialLoader::getInstance().setActiveShaderPack(pack->name, std::move(prebuiltDirs));

    return true;
}

bool ShaderPackManager::deactivatePack() {
    auto const prevActive = ConfigManager::getInstance().config().shaders.activePack;
    ConfigManager::getInstance().config().shaders.activePack.clear();

    if (!ConfigManager::getInstance().save()) {
        ConfigManager::getInstance().config().shaders.activePack = prevActive;
        return false;
    }

    MaterialLoader::getInstance().restoreVanilla();
    return true;
}

bool ShaderPackManager::selectSubpack(std::string_view packName, std::string_view subpackName) {
    auto* pack = findPackMutable(packName);
    if (!pack) return false;

    bool exists = false;
    for (auto const& sub : pack->subpacks) {
        if (sub.folderName == subpackName) {
            exists = true;
            break;
        }
    }
    if (!exists) return false;

    auto prevSubpack = pack->activeSubpack;
    pack->activeSubpack = std::string(subpackName);

    if (!ShaderPackParser::saveSettings(*pack, pack->settingsPath)) {
        pack->activeSubpack = std::move(prevSubpack); // Rollback
        return false;
    }

    copyMaterialsToBins(*pack);
    return true;
}

bool ShaderPackManager::setParameterValue(std::string_view packName, std::string_view paramName, std::string value) {
    auto* pack = findPackMutable(packName);
    if (!pack) return false;

    for (auto& p : pack->parameters) {
        if (p.name == paramName) {
            std::string normalized;
            if (!ShaderPackParser::validateParameterValue(p, value, normalized)) {
                return false;
            }

            auto prev = p.current;
            p.current = std::move(normalized);

            if (!ShaderPackParser::saveSettings(*pack, pack->settingsPath)) {
                p.current = std::move(prev); // Rollback
                return false;
            }
            return true;
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

    // 1. Validar integridad de la fuente ANTES de tocar el destino
    std::string archiveErr;
    if (!validateArchiveIntegrity(sourceFile, {}, &archiveErr)) {
        outError = "Archive validation failed: " + archiveErr;
        return false;
    }

    std::filesystem::create_directories(mPacksRoot, ec);
    auto dest = mPacksRoot / sourceFile.filename();

    // 2. Copiar primero hacia un archivo temporal exclusivo
    auto suffix = generateUniqueSuffix();
    auto tempImportFile = mPacksRoot / (sourceFile.filename().string() + ".import_tmp_" + suffix);
    std::filesystem::copy_file(sourceFile, tempImportFile, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "Failed to copy to staging: " + ec.message();
        std::filesystem::remove(tempImportFile, ec);
        return false;
    }

    // 3. Si el destino ya existe, crear backup temporal exclusivo
    bool hadExisting = std::filesystem::exists(dest, ec);
    auto backupFile = mPacksRoot / (sourceFile.filename().string() + ".backup_" + suffix);
    if (hadExisting) {
        std::filesystem::rename(dest, backupFile, ec);
        if (ec) {
            outError = "Failed to backup existing pack: " + ec.message();
            std::filesystem::remove(tempImportFile, ec);
            return false;
        }
    }

    // 4. Mover atómicamente el nuevo archivo hacia el destino
    std::filesystem::rename(tempImportFile, dest, ec);
    if (ec) {
        outError = "Failed to finalize import: " + ec.message();
        if (hadExisting) {
            std::error_code restoreEc;
            std::filesystem::rename(backupFile, dest, restoreEc);
        }
        std::filesystem::remove(tempImportFile, ec);
        return false;
    }

    // 5. Limpiar backup si todo salió bien
    if (hadExisting) {
        std::filesystem::remove(backupFile, ec);
    }

    scan(mPacksRoot, mGeneratedRoot);
    return true;
}

bool ShaderPackManager::deletePack(std::string_view name, std::string& outError) {
    if (name.empty() || name == "." || name == ".." ||
        name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        outError = "Invalid pack name";
        return false;
    }

    auto* pack = findPack(name);
    if (!pack) {
        outError = "Pack not found: " + std::string(name);
        return false;
    }

    std::error_code ec;

    if (!pack->archivePath.empty()) {
        if (!isPathInside(mPacksRoot, pack->archivePath)) {
            outError = "Security violation: archive path outside managed packs directory";
            return false;
        }
        std::filesystem::remove(pack->archivePath, ec);
    } else {
        if (!isPathInside(mPacksRoot, pack->sourceDir) && !isPathInside(mGeneratedRoot, pack->sourceDir)) {
            outError = "Security violation: source directory outside managed directories";
            return false;
        }
        std::filesystem::remove_all(pack->sourceDir, ec);
    }

    auto genPackDir = mGeneratedRoot / std::string(name);
    if (std::filesystem::exists(genPackDir, ec)) {
        if (!isPathInside(mGeneratedRoot, genPackDir)) {
            outError = "Security violation: generated path outside managed root";
            return false;
        }
        std::filesystem::remove_all(genPackDir, ec);
    }

    if (!pack->settingsPath.empty() && std::filesystem::exists(pack->settingsPath, ec)) {
        if (!isPathInside(mPacksRoot, pack->settingsPath)) {
            outError = "Security violation: settings file outside managed packs directory";
            return false;
        }
        std::filesystem::remove(pack->settingsPath, ec);
    }

    if (ConfigManager::getInstance().config().shaders.activePack == name) {
        deactivatePack();
    }

    scan(mPacksRoot, mGeneratedRoot);
    return true;
}

void ShaderPackManager::copyMaterialsToBins(ShaderPack const& pack) {
    std::error_code ec;
    std::vector<std::filesystem::path> prebuiltRoots;
    prebuiltRoots.push_back(pack.sourceDir);

    std::vector<std::filesystem::path> dirs;
    auto addDir = [&](std::filesystem::path const& d) {
        std::error_code e;
        if (!std::filesystem::is_directory(d, e) || !ShaderPackParser::hasMaterialBin(d)) return;
        if (std::find(dirs.begin(), dirs.end(), d) != dirs.end()) return;
        dirs.push_back(d);
    };

    if (!pack.prebuiltDir.empty()) addDir(pack.prebuiltDir);

    for (auto const& root : prebuiltRoots) {
        for (auto const& d : ShaderPackParser::collectPrebuiltDirs(root, pack.activeSubpack)) {
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
        if (toLowerStr(stemName) == "vanilla") continue;

        auto packGenDir = generatedRoot / stemName;
        auto srcDir     = packGenDir / "src";
        auto binsDir    = packGenDir / "compiled";

        if (!ensureExtracted(entry.path(), srcDir)) {
            continue;
        }

        auto normalizedSrc = normalizeSourceDir(srcDir);
        mPacks.push_back(
            loadPack(normalizedSrc, entry.path(), binsDir, packsRoot / (entry.path().filename().string() + ".txt"), srcDir)
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
        pack.name = packArchive.filename().string();
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

    // Si existen subpacks, seleccionar por defecto el primer subpack declarado por el autor
    if (!pack.subpacks.empty()) {
        pack.activeSubpack = pack.subpacks.front().folderName;
    }

    // Propiedades y perfiles
    pack.profiles   = ShaderPackParser::parseProfiles(sourceDir / "shaders.properties");
    pack.parameters = ShaderPackParser::parseProperties(sourceDir / "shaders.properties");

    // Cargar y validar ajustes guardados del usuario (.txt) incluyendo subpack activo
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

    // Sincronizar materiales precompilados al directorio binsDir (compiled/)
    if (!ShaderPackParser::hasMaterialBin(pack.binsDir)) {
        copyMaterialsToBins(pack);
    }

    // Detección de render mode (Forward vs Deferred)
    pack.renderMode = ShaderPackParser::detectRenderMode(
        ShaderPackParser::hasMaterialBin(pack.binsDir) ? pack.binsDir : pack.prebuiltDir
    );

    return pack;
}

} // namespace aeris
