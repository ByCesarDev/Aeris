#pragma once

#include <string>
#include <string_view>

namespace aeris {

class CompatibilityManager {
public:
    static CompatibilityManager& getInstance();

    void initialize();

    [[nodiscard]] bool isVersionCompatible() const { return mVersionCompatible; }
    [[nodiscard]] bool isHotReloadSupported() const { return mHotReloadSupported; }
    [[nodiscard]] bool isGraphicsSwitchSupported() const { return mGraphicsSwitchSupported; }

    [[nodiscard]] std::string const& detectedVersion() const { return mDetectedVersion; }
    [[nodiscard]] std::string_view targetVersion() const { return "1.26.51.01"; }

    void setHotReloadSupported(bool supported) { mHotReloadSupported = supported; }
    void setGraphicsSwitchSupported(bool supported) { mGraphicsSwitchSupported = supported; }

private:
    CompatibilityManager() = default;

    std::string mDetectedVersion = "1.26.51.01";
    bool        mVersionCompatible = true;
    bool        mHotReloadSupported = false;
    bool        mGraphicsSwitchSupported = true;
};

} // namespace aeris
