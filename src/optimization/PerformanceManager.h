#pragma once

#include <chrono>
#include <optional>

namespace aeris {

class PerformanceManager {
public:
    static PerformanceManager& getInstance();

    void initialize();
    void reset();

    [[nodiscard]] bool diagnosticsEnabled() const { return mDiagnosticsEnabled; }
    void setDiagnosticsEnabled(bool enabled) { mDiagnosticsEnabled = enabled; }

    // Registra un fotograma real cuando se invoca desde el pipeline de renderizado
    void recordFrame();

    [[nodiscard]] bool hasMeasurement() const { return mFps.has_value(); }
    [[nodiscard]] std::optional<float> currentFps() const { return mFps; }
    [[nodiscard]] std::optional<float> currentFrameTimeMs() const { return mFrameTimeMs; }

private:
    PerformanceManager() = default;

    bool                                  mDiagnosticsEnabled = true;
    std::optional<float>                  mFps;
    std::optional<float>                  mFrameTimeMs;
    int                                   mFrameCount = 0;
    std::chrono::steady_clock::time_point mLastFpsUpdate{};
    std::chrono::steady_clock::time_point mLastFrameTime{};
};

} // namespace aeris
