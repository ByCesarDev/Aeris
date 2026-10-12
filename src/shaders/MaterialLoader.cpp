#include "shaders/MaterialLoader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>

#include "core/ConfigManager.h"

namespace aeris {

namespace {

constexpr std::string_view kExpectedTypeName = "RenderDragon.CompiledMaterialDefinition";

} // namespace

MaterialLoader& MaterialLoader::getInstance() {
    static MaterialLoader instance;
    return instance;
}

bool MaterialLoader::validateMaterialBinary(std::span<const uint8_t> data, std::string* outError) {
    if (data.size() < kRenderDragonHeaderMin) {
        if (outError) *outError = "Material buffer smaller than minimum RenderDragon header (51 bytes)";
        return false;
    }

    // 1. Verificar Magic: 0x0a11da1a (1A DA 11 0A)
    uint32_t magic = static_cast<uint32_t>(data[0])
                   | (static_cast<uint32_t>(data[1]) << 8)
                   | (static_cast<uint32_t>(data[2]) << 16)
                   | (static_cast<uint32_t>(data[3]) << 24);
    if (magic != kRenderDragonMagic) {
        if (outError) *outError = "Invalid magic number (expected RenderDragon 0x0A11DA1A)";
        return false;
    }

    // 2. Longitud de cadena en offset 8 (uint32 Little Endian = 39)
    uint32_t strLen = static_cast<uint32_t>(data[8])
                    | (static_cast<uint32_t>(data[9]) << 8)
                    | (static_cast<uint32_t>(data[10]) << 16)
                    | (static_cast<uint32_t>(data[11]) << 24);
    if (strLen != kExpectedTypeName.size() || (12 + strLen) > data.size()) {
        if (outError) *outError = "Invalid type name length in material header";
        return false;
    }

    // 3. Cadena de tipo de definición en offset 12
    std::string_view typeName(reinterpret_cast<const char*>(data.data() + 12), strLen);
    if (typeName != kExpectedTypeName) {
        if (outError) *outError = "Type name mismatch (expected 'RenderDragon.CompiledMaterialDefinition')";
        return false;
    }

    return true;
}

bool MaterialLoader::isMaterialAuthorized(std::string_view canonicalName) {
    if (canonicalName.empty()) {
        return false;
    }
    auto const& authList = ConfigManager::getInstance().config().safety.authorizedOverrideMaterials;
    for (auto const& auth : authList) {
        if (auth == "*" || auth == "*.*" || auth == "*.material.bin" || canonicalName == auth) {
            return true;
        }
    }
    return false;
}

void MaterialLoader::setActiveShaderPack(std::string packName, std::vector<std::filesystem::path> prebuiltDirs) {
    auto newIndex = std::make_shared<IndexMap>();
    std::error_code ec;

    // Pre-indexación y pre-carga inmutable: escanear directorios y cargar bytes en memoria
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

            // Pre-carga binaria completa en memoria (evita syscalls en el hook de renderizado)
            std::ifstream file(normalCandidate, std::ios::binary);
            if (!file.is_open()) continue;

            std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(fileSize));
            if (!file && !file.eof()) continue;

            // Validación rigurosa de cabecera binaria RenderDragon
            bool isValid = validateMaterialBinary(buffer);

            MaterialEntry matEntry;
            matEntry.canonicalName   = canonicalName;
            matEntry.diskPath        = normalCandidate;
            matEntry.fileSize        = static_cast<size_t>(fileSize);
            matEntry.packName        = packName;
            matEntry.binaryData      = std::move(buffer);
            matEntry.isValidMaterial = isValid;

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

    // Rechazar path traversal y bytes nulos
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
    if (!entry.has_value() || !entry->isValidMaterial || entry->binaryData.empty()) {
        std::lock_guard lock(mMutex);
        mTotalFallbacks++;
        return std::nullopt;
    }

    {
        std::lock_guard lock(mMutex);
        mTotalOverrides++;
        mLastResolvedMaterial = entry->canonicalName;
    }

    return entry->binaryData;
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

uint64_t MaterialLoader::activeSubstitutions() const {
    return mActiveSubstitutions.load(std::memory_order_relaxed);
}

uint64_t MaterialLoader::validationErrors() const {
    return mValidationErrors.load(std::memory_order_relaxed);
}

void MaterialLoader::recordDryRunMatch() {
    mDryRunMatches.fetch_add(1, std::memory_order_relaxed);
}

void MaterialLoader::recordDryRunMiss() {
    mDryRunMisses.fetch_add(1, std::memory_order_relaxed);
}

void MaterialLoader::recordActiveSubstitution() {
    mActiveSubstitutions.fetch_add(1, std::memory_order_relaxed);
}

void MaterialLoader::recordValidationError() {
    mValidationErrors.fetch_add(1, std::memory_order_relaxed);
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
    mActiveSubstitutions.store(0, std::memory_order_relaxed);
    mValidationErrors.store(0, std::memory_order_relaxed);
    mLastResolvedMaterial.clear();
}

} // namespace aeris
