#pragma once

#include <chrono>
#include <string>

namespace aeris {

class PerformanceManager {
public:
    static PerformanceManager& getInstance();

    void initialize();

    [[nodiscard]] bool diagnosticsEnabled() const { return mDiagnosticsEnabled; }
    void setDiagnosticsEnabled(bool enabled) { mDiagnosticsEnabled = enabled; }

    void recordFrame();
    [[nodiscard]] float currentFps() const { return mFps; }

private:
    PerformanceManager() = default;

    bool                                  mDiagnosticsEnabled = true;
    float                                 mFps               = 60.0f;
    int                                   mFrameCount        = 0;
    std::chrono::steady_clock::time_point mLastFpsUpdate{};
};

} // namespace aeris
