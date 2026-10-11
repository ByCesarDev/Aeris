#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aeris {

/**
 * @brief Pure C++ RenderDragon material loader and path resolver.
 * 
 * Manages material overrides independently from any game hooking APIs.
 * Resolves requested material paths (e.g., "data/renderer/materials/RenderChunk.material.bin")
 * against the prebuilt binary directories of the currently active shaderpack.
 * Enforces path canonicalization, size boundaries, and thread-safe fallback to vanilla.
 */
class MaterialLoader {
public:
    static constexpr size_t kMaxMaterialSizeBytes = 64 * 1024 * 1024; // 64 MB max material size

    static MaterialLoader& getInstance();

    /**
     * @brief Activates a shaderpack by registering its name and prebuilt binary search directories.
     * @param packName Name of the active shaderpack.
     * @param prebuiltDirs Ordered list of directories containing .material.bin files (e.g. subpack variants).
     */
    void setActiveShaderPack(std::string packName, std::vector<std::filesystem::path> prebuiltDirs);

    /**
     * @brief Immediately deactivates all material overrides and restores pure vanilla passthrough.
     */
    void restoreVanilla();

    /**
     * @brief Checks if material redirection is currently active.
     */
    [[nodiscard]] bool isOverrideActive() const;

    /**
     * @brief Returns the name of the currently active shaderpack, or empty string if vanilla.
     */
    [[nodiscard]] std::string activePackName() const;

    /**
     * @brief Returns the prebuilt search directories currently registered.
     */
    [[nodiscard]] std::vector<std::filesystem::path> activePrebuiltDirs() const;

    /**
     * @brief Normalizes a requested game path to a safe canonical material filename.
     * 
     * Converts paths like "data/renderer/materials/RenderChunk.material.bin" or
     * "data\\renderer\\materials\\RenderChunk.material.bin" to "RenderChunk.material.bin".
     * Validates that the filename ends with ".material.bin" and contains no directory traversal elements.
     * 
     * @param requestPath Raw path requested by the game engine.
     * @return Canonical filename, or empty string if invalid/non-material.
     */
    static std::string normalizeMaterialName(std::string_view requestPath);

    /**
     * @brief Resolves a material request from the active shaderpack.
     * 
     * If an override is found, verified, and passes security boundaries, returns the binary bytes.
     * If no override is active, or the file is missing/corrupted/too large, returns std::nullopt (vanilla fallback).
     * 
     * @param requestPath Game material path.
     * @return Raw material bytes if overridden, or std::nullopt to fall back to vanilla.
     */
    std::optional<std::vector<uint8_t>> resolveMaterial(std::string_view requestPath);

    // Telemetry & diagnostics
    [[nodiscard]] size_t      totalRequests() const;
    [[nodiscard]] size_t      totalOverrides() const;
    [[nodiscard]] size_t      totalFallbacks() const;
    [[nodiscard]] std::string lastResolvedMaterial() const;
    void                      resetStats();

private:
    MaterialLoader() = default;

    mutable std::mutex                  mMutex;
    bool                                mActive = false;
    std::string                         mActivePackName;
    std::vector<std::filesystem::path>  mPrebuiltDirs;

    // Telemetry
    size_t      mTotalRequests  = 0;
    size_t      mTotalOverrides = 0;
    size_t      mTotalFallbacks = 0;
    std::string mLastResolvedMaterial;
};

} // namespace aeris
