#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

#include "core/SignatureVerifier.h"

namespace aeris {

/**
 * @brief Native hook manager for RenderDragon material interception.
 * 
 * Provides safe detours for the client engine's internal readFile and endFrame functions.
 * All memory alterations are gated behind CompatibilityManager checks and signature verification.
 * If signature verification fails or is ambiguous, hooks are safely bypassed without touching memory.
 */
class MaterialHooks {
public:
    static MaterialHooks& getInstance();
    ~MaterialHooks();

    /**
     * @brief Inspects and validates binary signatures against module memory without installing detours.
     * Evaluates uniqueness, module bounds, and executable permissions.
     */
    void verifySignatures();

    /**
     * @brief Installs the readFile and frame hooks if safe mode is disabled and signatures are verified.
     * @return true if primary redirection hook was successfully installed, false otherwise.
     */
    bool install();

    /**
     * @brief Uninstalls all active memory detours and restores original function pointers.
     */
    void uninstall();

    /**
     * @brief Immediately cancels overrides and restores vanilla materials.
     */
    void restoreVanilla();

    [[nodiscard]] bool isInstalled() const;
    [[nodiscard]] bool areSignaturesVerified() const;
    [[nodiscard]] bool isPassiveHookActive() const;
    [[nodiscard]] bool isDryRunActive() const;
    [[nodiscard]] bool isReloadAvailable() const;
    [[nodiscard]] uint64_t passiveReadCount() const;
    [[nodiscard]] uint64_t materialBinReadCount() const;
    [[nodiscard]] uint64_t dryRunMatches() const;
    [[nodiscard]] uint64_t dryRunMisses() const;
    [[nodiscard]] std::string lastError() const;

    void recordPassiveRead(std::string_view pathStr, size_t bytesRead);
    void recordDryRunCheck(std::string_view pathStr, size_t vanillaBytes);

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

    mutable std::mutex mMutex;
    bool                  mInstalled          = false;
    bool                  mSignaturesVerified = false;
    bool                  mPassiveHookActive  = false;
    bool                  mDryRunActive       = false;
    bool                  mReloadAvailable    = false;
    bool                  mReloadPending      = false;
    std::atomic<uint64_t> mPassiveReadCount{0};
    std::atomic<uint64_t> mMaterialBinReadCount{0};
    std::string           mLastError;
};

} // namespace aeris
