#include "shaders/MaterialLoader.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace aeris {

MaterialLoader& MaterialLoader::getInstance() {
    static MaterialLoader instance;
    return instance;
}

void MaterialLoader::setActiveShaderPack(std::string packName, std::vector<std::filesystem::path> prebuiltDirs) {
    auto newIndex = std::make_shared<IndexMap>();
    std::error_code ec;

    // Pre-indexación inmutable: escanear directorios ordenados y construir mapa en memoria
    for (auto const& dir : prebuiltDirs) {
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }

        auto normalDir = std::filesystem::weakly_canonical(dir, ec);
        if (ec) continue;

        for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;

            auto filename = entry.path().filename().string();
            if (!filename.ends_with(".material.bin")) continue;

            std::string canonicalName = normalizeMaterialName(filename);
            if (canonicalName.empty()) continue;

            // Prioridad de subpack: el primer directorio que declare el material gana
            if (newIndex->find(canonicalName) != newIndex->end()) {
                continue;
            }

            auto normalCandidate = std::filesystem::weakly_canonical(entry.path(), ec);
            if (ec) continue;

            auto dirStr       = normalDir.string();
            auto candidateStr = normalCandidate.string();
            if (!candidateStr.starts_with(dirStr)) {
                continue; // Escape de ruta rechazado
            }

            auto fileSize = entry.file_size(ec);
            if (ec || fileSize == 0 || fileSize > kMaxMaterialSizeBytes) {
                continue;
            }

            MaterialEntry matEntry;
            matEntry.canonicalName = canonicalName;
            matEntry.diskPath      = normalCandidate;
            matEntry.fileSize      = static_cast<size_t>(fileSize);
            matEntry.packName      = packName;

            (*newIndex)[canonicalName] = std::move(matEntry);
        }
    }

    std::lock_guard lock(mMutex);
    mActivePackName = std::move(packName);
    mPrebuiltDirs   = std::move(prebuiltDirs);
    mIndex.store(std::shared_ptr<const IndexMap>(newIndex));
    mActive         = !mActivePackName.empty() && !newIndex->empty();
}

void MaterialLoader::restoreVanilla() {
    std::lock_guard lock(mMutex);
    mActive = false;
    mActivePackName.clear();
    mPrebuiltDirs.clear();
    mIndex.store(std::shared_ptr<const IndexMap>());
}

bool MaterialLoader::isOverrideActive() const {
    std::lock_guard lock(mMutex);
    return mActive;
}

std::string MaterialLoader::activePackName() const {
    std::lock_guard lock(mMutex);
    return mActivePackName;
}

std::vector<std::filesystem::path> MaterialLoader::activePrebuiltDirs() const {
    std::lock_guard lock(mMutex);
    return mPrebuiltDirs;
}

std::string MaterialLoader::normalizeMaterialName(std::string_view requestPath) {
    if (requestPath.empty()) {
        return "";
    }

    // Convertir separadores de Windows a Unix
    std::string normalized(requestPath);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    // Debe terminar en .material.bin
    constexpr std::string_view kSuffix = ".material.bin";
    if (normalized.size() < kSuffix.size() || !normalized.ends_with(kSuffix)) {
        return "";
    }

    // Rechazar rutas absolutas o de unidad de disco (ej: C:/ o /root)
    if (normalized.starts_with('/') || normalized.find(':') != std::string::npos) {
        return "";
    }

    // Rechazar path traversal
    if (normalized.find("..") != std::string::npos || normalized.find('\0') != std::string::npos) {
        return "";
    }

    // Extraer únicamente el nombre de archivo final (soporta "data/renderer/materials/X.material.bin")
    auto slashPos = normalized.find_last_of('/');
    std::string filename = (slashPos == std::string::npos) ? normalized : normalized.substr(slashPos + 1);

    if (filename.empty() || filename == kSuffix) {
        return "";
    }

    // Doble verificación en el nombre de archivo
    if (filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos) {
        return "";
    }

    return filename;
}

std::optional<MaterialEntry> MaterialLoader::findMaterial(std::string_view requestPath) const {
    std::string normalizedName = normalizeMaterialName(requestPath);
    if (normalizedName.empty()) {
        return std::nullopt;
    }

    auto idx = mIndex.load();
    if (!idx || idx->empty()) {
        return std::nullopt;
    }

    auto it = idx->find(normalizedName);
    if (it != idx->end()) {
        return it->second;
    }

    return std::nullopt;
}

std::optional<std::vector<uint8_t>> MaterialLoader::resolveMaterial(std::string_view requestPath) {
    {
        std::lock_guard lock(mMutex);
        mTotalRequests++;
    }

    auto entry = findMaterial(requestPath);
    if (!entry.has_value()) {
        std::lock_guard lock(mMutex);
        mTotalFallbacks++;
        return std::nullopt;
    }

    // Lectura binaria segura del archivo pre-indexado
    std::ifstream file(entry->diskPath, std::ios::binary);
    if (!file.is_open()) {
        std::lock_guard lock(mMutex);
        mTotalFallbacks++;
        return std::nullopt;
    }

    std::vector<uint8_t> buffer(entry->fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(entry->fileSize));
    if (!file && !file.eof()) {
        std::lock_guard lock(mMutex);
        mTotalFallbacks++;
        return std::nullopt;
    }

    {
        std::lock_guard lock(mMutex);
        mTotalOverrides++;
        mLastResolvedMaterial = entry->canonicalName;
    }

    return buffer;
}

size_t MaterialLoader::totalRequests() const {
    std::lock_guard lock(mMutex);
    return mTotalRequests;
}

size_t MaterialLoader::totalOverrides() const {
    std::lock_guard lock(mMutex);
    return mTotalOverrides;
}

size_t MaterialLoader::totalFallbacks() const {
    std::lock_guard lock(mMutex);
    return mTotalFallbacks;
}

uint64_t MaterialLoader::dryRunMatches() const {
    return mDryRunMatches.load(std::memory_order_relaxed);
}

uint64_t MaterialLoader::dryRunMisses() const {
    return mDryRunMisses.load(std::memory_order_relaxed);
}

void MaterialLoader::recordDryRunMatch() {
    mDryRunMatches.fetch_add(1, std::memory_order_relaxed);
}

void MaterialLoader::recordDryRunMiss() {
    mDryRunMisses.fetch_add(1, std::memory_order_relaxed);
}

std::string MaterialLoader::lastResolvedMaterial() const {
    std::lock_guard lock(mMutex);
    return mLastResolvedMaterial;
}

void MaterialLoader::resetStats() {
    std::lock_guard lock(mMutex);
    mTotalRequests  = 0;
    mTotalOverrides = 0;
    mTotalFallbacks = 0;
    mDryRunMatches.store(0, std::memory_order_relaxed);
    mDryRunMisses.store(0, std::memory_order_relaxed);
    mLastResolvedMaterial.clear();
}

} // namespace aeris
