#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aeris {

struct MaterialEntry {
    std::string           canonicalName; // e.g. "RenderChunk.material.bin"
    std::filesystem::path diskPath;      // verified disk path
    size_t                fileSize = 0;  // cached file size
    std::string           packName;      // owner shaderpack name
};

/**
 * @brief Pure C++ RenderDragon material loader and path resolver.
 * 
 * Manages material overrides independently from any game hooking APIs.
 * Resolves requested material paths (e.g., "data/renderer/materials/RenderChunk.material.bin")
 * against an immutable in-memory pre-indexed map of the active shaderpack.
 * Enforces path canonicalization, size boundaries, and thread-safe lock-free lookups.
 */
class MaterialLoader {
public:
    static constexpr size_t kMaxMaterialSizeBytes = 64 * 1024 * 1024; // 64 MB max material size

    static MaterialLoader& getInstance();

    /**
     * @brief Activates a shaderpack by registering its name and prebuilt binary search directories.
     * Pre-indexes all .material.bin files into an immutable lookup map for zero-disk-syscall lookups.
     * @param packName Name of the active shaderpack.
     * @param prebuiltDirs Ordered list of directories containing .material.bin files (subpack variants).
     */
    void setActiveShaderPack(std::string packName, std::vector<std::filesystem::path> prebuiltDirs);

    /**
     * @brief Immediately deactivates all material overrides and restores pure vanilla passthrough.
     */
    void restoreVanilla();

    [[nodiscard]] bool isOverrideActive() const;
    [[nodiscard]] std::string activePackName() const;
    [[nodiscard]] std::vector<std::filesystem::path> activePrebuiltDirs() const;

    /**
     * @brief Normalizes a requested game path to a safe canonical material filename.
     * Rejects directory traversal (..), absolute paths, colon syntax, null bytes, and non-.material.bin files.
     */
    static std::string normalizeMaterialName(std::string_view requestPath);

    /**
     * @brief Fast O(1) lock-free lookup in the pre-indexed material map.
     * Does not perform disk I/O during lookup.
     */
    [[nodiscard]] std::optional<MaterialEntry> findMaterial(std::string_view requestPath) const;

    /**
     * @brief Reads binary bytes for a material from disk if indexed.
     */
    std::optional<std::vector<uint8_t>> resolveMaterial(std::string_view requestPath);

    // Telemetry & metrics
    [[nodiscard]] size_t      totalRequests() const;
    [[nodiscard]] size_t      totalOverrides() const;
    [[nodiscard]] size_t      totalFallbacks() const;
    [[nodiscard]] uint64_t    dryRunMatches() const;
    [[nodiscard]] uint64_t    dryRunMisses() const;
    [[nodiscard]] std::string lastResolvedMaterial() const;
    void                      recordDryRunMatch();
    void                      recordDryRunMiss();
    void                      resetStats();

private:
    MaterialLoader() = default;

    using IndexMap = std::unordered_map<std::string, MaterialEntry>;

    mutable std::mutex                  mMutex;
    bool                                mActive = false;
    std::string                         mActivePackName;
    std::vector<std::filesystem::path>  mPrebuiltDirs;
    std::atomic<std::shared_ptr<const IndexMap>> mIndex;

    // Telemetry
    size_t                mTotalRequests  = 0;
    size_t                mTotalOverrides = 0;
    size_t                mTotalFallbacks = 0;
    std::atomic<uint64_t> mDryRunMatches{0};
    std::atomic<uint64_t> mDryRunMisses{0};
    std::string           mLastResolvedMaterial;
};

} // namespace aeris
