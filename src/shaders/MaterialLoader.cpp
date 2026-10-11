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
    std::lock_guard lock(mMutex);
    mActivePackName = std::move(packName);
    mPrebuiltDirs   = std::move(prebuiltDirs);
    mActive         = !mActivePackName.empty() && !mPrebuiltDirs.empty();
}

void MaterialLoader::restoreVanilla() {
    std::lock_guard lock(mMutex);
    mActive = false;
    mActivePackName.clear();
    mPrebuiltDirs.clear();
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

    // Extraer únicamente el nombre de archivo final
    auto slashPos = normalized.find_last_of('/');
    std::string filename = (slashPos == std::string::npos) ? normalized : normalized.substr(slashPos + 1);

    if (filename.empty() || filename == kSuffix) {
        return "";
    }

    // Rechazar componentes de path traversal o caracteres prohibidos
    if (filename.find("..") != std::string::npos ||
        filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.find(':') != std::string::npos ||
        filename.find('\0') != std::string::npos) {
        return "";
    }

    return filename;
}

std::optional<std::vector<uint8_t>> MaterialLoader::resolveMaterial(std::string_view requestPath) {
    std::string normalizedName = normalizeMaterialName(requestPath);
    if (normalizedName.empty()) {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> dirs;
    {
        std::lock_guard lock(mMutex);
        mTotalRequests++;
        if (!mActive || mPrebuiltDirs.empty()) {
            mTotalFallbacks++;
            return std::nullopt;
        }
        dirs = mPrebuiltDirs;
    }

    std::error_code ec;
    for (auto const& dir : dirs) {
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }

        auto candidate = dir / normalizedName;
        if (!std::filesystem::is_regular_file(candidate, ec)) {
            continue;
        }

        // Validación estricta de seguridad: el archivo debe residir dentro del directorio prebuilt
        auto normalDir       = std::filesystem::weakly_canonical(dir, ec);
        auto normalCandidate = std::filesystem::weakly_canonical(candidate, ec);
        if (ec) {
            continue;
        }

        auto dirStr       = normalDir.string();
        auto candidateStr = normalCandidate.string();
        if (!candidateStr.starts_with(dirStr)) {
            // Intento de escape de ruta detectado
            continue;
        }

        // Límite de tamaño para evitar ataques DoS de memoria
        auto fileSize = std::filesystem::file_size(candidate, ec);
        if (ec || fileSize == 0 || fileSize > kMaxMaterialSizeBytes) {
            continue;
        }

        // Lectura binaria segura
        std::ifstream file(candidate, std::ios::binary);
        if (!file.is_open()) {
            continue;
        }

        std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));
        if (!file && !file.eof()) {
            continue;
        }

        {
            std::lock_guard lock(mMutex);
            mTotalOverrides++;
            mLastResolvedMaterial = normalizedName;
        }

        return buffer;
    }

    {
        std::lock_guard lock(mMutex);
        mTotalFallbacks++;
    }

    return std::nullopt;
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

std::string MaterialLoader::lastResolvedMaterial() const {
    std::lock_guard lock(mMutex);
    return mLastResolvedMaterial;
}

void MaterialLoader::resetStats() {
    std::lock_guard lock(mMutex);
    mTotalRequests  = 0;
    mTotalOverrides = 0;
    mTotalFallbacks = 0;
    mLastResolvedMaterial.clear();
}

} // namespace aeris
