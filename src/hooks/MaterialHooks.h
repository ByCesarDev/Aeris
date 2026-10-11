#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace aeris {

/**
 * @brief Native hook manager for RenderDragon material interception.
 * 
 * Provides safe detours for the client engine's internal readFile and endFrame functions.
 * All memory alterations are gated behind CompatibilityManager checks.
 * If signature verification fails, hooks are safely bypassed without modifying game memory.
 */
class MaterialHooks {
public:
    static MaterialHooks& getInstance();
    ~MaterialHooks();

    /**
     * @brief Installs the readFile and frame hooks if signatures are verified.
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
    [[nodiscard]] bool isReloadAvailable() const;
    [[nodiscard]] std::string lastError() const;

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

    mutable std::mutex mMutex;
    bool               mInstalled       = false;
    bool               mReloadAvailable = false;
    bool               mReloadPending   = false;
    std::string        mLastError;
};

} // namespace aeris
