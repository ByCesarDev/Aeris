#include "optimization/PerformanceManager.h"

namespace aeris {

PerformanceManager& PerformanceManager::getInstance() {
    static PerformanceManager instance;
    return instance;
}

void PerformanceManager::initialize() {
    reset();
}

void PerformanceManager::reset() {
    mFps.reset();
    mFrameTimeMs.reset();
    mFrameCount    = 0;
    mLastFpsUpdate = std::chrono::steady_clock::time_point{};
    mLastFrameTime = std::chrono::steady_clock::time_point{};
}

void PerformanceManager::recordFrame() {
    if (!mDiagnosticsEnabled) return;

    auto now = std::chrono::steady_clock::now();

    // Medición de delta de frame individual
    if (mLastFrameTime.time_since_epoch().count() > 0) {
        auto frameDuration = std::chrono::duration_cast<std::chrono::microseconds>(now - mLastFrameTime).count();
        if (frameDuration > 0) {
            float dtMs = static_cast<float>(frameDuration) / 1000.0f;
            mFrameTimeMs = dtMs;
        }
    }
    mLastFrameTime = now;

    // Conteo y agregación de FPS en ventana de 1 segundo
    if (mLastFpsUpdate.time_since_epoch().count() == 0) {
        mLastFpsUpdate = now;
        mFrameCount    = 1;
        return;
    }

    ++mFrameCount;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastFpsUpdate).count();
    if (elapsedMs >= 1000) {
        mFps = (static_cast<float>(mFrameCount) * 1000.0f) / static_cast<float>(elapsedMs);
        mFrameCount    = 0;
        mLastFpsUpdate = now;
    }
}

} // namespace aeris
