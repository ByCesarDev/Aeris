#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <string>

#include "ui/UiScreen.h"

class MinecraftUIRenderContext;
class IClientInstance;
class AbstractScene;
class ScreenView;

namespace aeris::ui {

class ShaderManagerScreen {
public:
    static ShaderManagerScreen& getInstance();

    [[nodiscard]] bool visible() const;
    void toggle();
    void open();
    void open(IClientInstance& client);
    void close();

    void refreshPacks();

    void render(MinecraftUIRenderContext& context, float screenWidth, float screenHeight);
    void afterFrame(MinecraftUIRenderContext& context);

    void setPointer(float x, float y);

    bool handleKey(int keyCode);
    bool handleMouse(int action, float px, float py, bool down, int wheelDelta = 0);

    [[nodiscard]] bool isPlaceholderScene(AbstractScene const* scene) const;
    void notifyPlaceholderScenePopped(AbstractScene const* scene);

private:
    ShaderManagerScreen();

    void openOnClientThread();
    void pushPlaceholderScene(IClientInstance& client);
    void popPlaceholderScene();
    void popOnClientThread(std::shared_ptr<AbstractScene> scene);
    [[nodiscard]] bool ownsTopLocked() const;
    void abandonSceneLocked();

    Screen mScreen;

    mutable std::recursive_mutex mMutex;
    IClientInstance*             mClient = nullptr;
    std::shared_ptr<AbstractScene> mScene;
    std::shared_ptr<AbstractScene> mRetiredScene;

    bool mOpenRequested = false;
    bool mOpenScheduled = false;
    bool mWantPopScene  = false;
    int  mPopRetries    = 0;
    std::chrono::steady_clock::time_point mScenePushedAt{};
};

void installNativeUiSuppressHooks();
void uninstallNativeUiSuppressHooks();
bool isOurUiPass(ScreenView const& view);

} // namespace aeris::ui
