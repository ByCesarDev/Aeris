#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace aeris {

/**
 * @brief Native hook manager for RenderDragon runtime detours.
 * 
 * Supports passive observation (Etapa 3.2), dry-run simulation (Etapa 3.3)
 * and selective active material substitution (Etapa 3.4).
 * Enforces non-reentrant mutex patterns, memory boundary protection,
 * and zero-disk-syscall detours.
 */
class MaterialHooks {
public:
    static MaterialHooks& getInstance();
    ~MaterialHooks();

    // Hook lifecycle
    bool install();
    void uninstall();
    void restoreVanilla();
    void verifySignatures();

    // State inspection
    [[nodiscard]] bool areSignaturesVerified() const;
    [[nodiscard]] bool isInstalled() const;
    [[nodiscard]] bool isPassiveHookActive() const;
    [[nodiscard]] bool isDryRunActive() const;
    [[nodiscard]] bool isOverrideActive() const;
    [[nodiscard]] bool isReloadAvailable() const;
    [[nodiscard]] std::string lastError() const;

    // Telemetry & metrics
    [[nodiscard]] uint64_t passiveReadCount() const;
    [[nodiscard]] uint64_t materialBinReadCount() const;
    [[nodiscard]] uint64_t dryRunMatches() const;
    [[nodiscard]] uint64_t dryRunMisses() const;
    [[nodiscard]] uint64_t activeSubstitutions() const;
    [[nodiscard]] uint64_t validationErrors() const;

    void recordPassiveRead(std::string_view pathStr, size_t bytesRead);
    void recordDryRunCheck(std::string_view pathStr, size_t vanillaBytes);
    void tryApplyMaterialOverride(std::string* ret, std::string_view pathStr);

    /**
     * @brief Queues a material cache flush to execute on the client render thread at the next endFrame.
     */
    void queueReload();

    /**
     * @brief Consumes a pending reload flag (thread-safe).
     */
    bool consumeReload();

private:
    MaterialHooks() = default;

    void verifySignaturesLocked();

    mutable std::mutex    mMutex;
    bool                  mInstalled          = false;
    bool                  mSignaturesVerified = false;
    bool                  mPassiveHookActive  = false;
    bool                  mDryRunActive       = false;
    bool                  mOverrideActive     = false;
    bool                  mReloadAvailable    = false;
    bool                  mReloadPending      = false;
    std::atomic<uint64_t> mPassiveReadCount{0};
    std::atomic<uint64_t> mMaterialBinReadCount{0};
    std::string           mLastError;
};

} // namespace aeris
