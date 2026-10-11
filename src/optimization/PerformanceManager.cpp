#include "optimization/PerformanceManager.h"

namespace aeris {

PerformanceManager& PerformanceManager::getInstance() {
    static PerformanceManager instance;
    return instance;
}

void PerformanceManager::initialize() {
    mLastFpsUpdate = std::chrono::steady_clock::now();
    mFrameCount = 0;
    mFps = 60.0f;
}

void PerformanceManager::recordFrame() {
    if (!mDiagnosticsEnabled) return;

    ++mFrameCount;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mLastFpsUpdate).count();

    if (elapsed >= 1000) {
        mFps = static_cast<float>(mFrameCount * 1000) / static_cast<float>(elapsed);
        mFrameCount = 0;
        mLastFpsUpdate = now;
    }
}

} // namespace aeris
