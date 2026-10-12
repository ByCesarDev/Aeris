#include "ui/ShaderManagerScreen.h"
#include "mod/AerisMod.h"
#include "ui/GameCanvas.h"

#include <algorithm>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#include <commdlg.h>
#endif

#include "ll/api/memory/Hook.h"
#include "ll/api/mod/NativeMod.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/thread/ClientThreadExecutor.h"

#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/SceneType.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/SceneFactory.h"
#include "mc/client/gui/screens/SceneStack.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/sound/SoundEngine.h"
#include "mc/deps/input/MouseAction.h"

#include "core/ConfigManager.h"
#include "shaders/ShaderPackManager.h"
#include "shaders/MaterialLoader.h"
#include "localization/LocalizationManager.h"

namespace aeris::ui {

namespace {

thread_local ScreenView* gOurScreenView = nullptr;

#ifdef _WIN32
std::wstring utf8ToWide(std::string const& str) {
    if (str.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), out.data(), n);
    return out;
}

std::string wideToUtf8(std::wstring const& wstr) {
    if (wstr.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string pickPackFileDialog() {
    wchar_t buffer[1024]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = ::GetForegroundWindow();
    ofn.lpstrFilter     = L"Paquetes de Shaders (*.mcpack;*.zip)\0*.mcpack;*.zip\0Todos los archivos (*.*)\0*.*\0\0";
    ofn.lpstrFile       = buffer;
    ofn.nMaxFile        = static_cast<DWORD>(std::size(buffer));
    ofn.Flags           = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!::GetOpenFileNameW(&ofn)) return {};
    return wideToUtf8(buffer);
}
#endif

} // namespace

ShaderManagerScreen& ShaderManagerScreen::getInstance() {
    static ShaderManagerScreen instance;
    return instance;
}

ShaderManagerScreen::ShaderManagerScreen() {
    ScreenHost host{};

    host.translate = [](std::string_view key) {
        return LocalizationManager::getInstance().translate(key);
    };

    host.playClick = []() {
        auto client = ll::service::getClientInstance();
        if (!client) return;
        auto sound = client->getSoundEngine();
        if (SoundEngine* raw = sound.operator->().get(); raw != nullptr) {
            raw->playUI("random.click", 1.0f, 1.0f);
        }
    };

    host.closeScreen = [this]() {
        popPlaceholderScene();
    };

    host.openPacksFolder = []() {
#ifdef _WIN32
        auto dir = AerisMod::getInstance().getPacksDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::wstring cmd = L"explorer.exe \"" + std::filesystem::absolute(dir, ec).wstring() + L"\"";
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
        }
#endif
    };

    host.pickImportFile = [this](std::string& outPath) -> bool {
#ifdef _WIN32
        std::string picked = pickPackFileDialog();
        if (picked.empty()) return false;
        outPath = picked;

        std::string err;
        auto imported = ShaderPackManager::getInstance().importPack(picked, err);
        if (imported) {
            refreshPacks();
            return true;
        } else {
            if (auto mod = ll::mod::NativeMod::current()) {
                mod->getLogger().warn("[AerisUI] Failed to import pack '{}': {}", picked, err);
            }
            return false;
        }
#else
        return false;
#endif
    };

    host.onApplyPack = [](std::string const& packName) {
        auto& configMgr = ConfigManager::getInstance();
        configMgr.config().shaders.activePack = packName;
        configMgr.config().safety.safeMode = false;
        configMgr.config().safety.enableActiveMaterialOverride = true;
        configMgr.save();

        ShaderPackManager::getInstance().selectPack(packName);

        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().info("[AerisUI] Saved active shader pack: '{}'. Restart required for GPU reload.", packName);
        }
    };

    host.onDisableShaders = []() {
        auto& configMgr = ConfigManager::getInstance();
        configMgr.config().shaders.activePack = "";
        configMgr.save();

        ShaderPackManager::getInstance().deactivatePack();

        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().info("[AerisUI] Shaders disabled (Vanilla mode). Restart required for GPU reload.");
        }
    };

    mScreen.setHost(std::move(host));
}

bool ShaderManagerScreen::visible() const {
    std::lock_guard lock(mMutex);
    return mScreen.visible();
}

void ShaderManagerScreen::setPointer(float x, float y) {
    std::lock_guard lock(mMutex);
    mScreen.setPointer(x, y);
}

void ShaderManagerScreen::refreshPacks() {
    std::lock_guard lock(mMutex);
    auto pDir = AerisMod::getInstance().getPacksDir();
    auto gDir = AerisMod::getInstance().getGeneratedDir();
    ShaderPackManager::getInstance().scan(pDir, gDir);
    auto const& allPacks = ShaderPackManager::getInstance().packs();

    std::string currentActive = ConfigManager::getInstance().config().shaders.activePack;

    std::vector<PackCardInfo> cards;
    for (auto const& p : allPacks) {
        cards.push_back({
            p.name,
            p.metadata.version.empty() ? "1.0.0" : p.metadata.version,
            p.metadata.author.empty() ? "Comunidad" : p.metadata.author,
            p.metadata.description.empty() ? "Paquete de shaders compatible con RenderDragon." : p.metadata.description,
            p.renderMode == ShaderRenderMode::Deferred ? "Deferred" : "Forward",
            p.name == currentActive,
            false
        });
    }

    mScreen.refreshPacksList(std::move(cards), currentActive);
}

void ShaderManagerScreen::toggle() {
    std::lock_guard lock(mMutex);
    if (mScreen.visible()) {
        close();
    } else {
        open();
    }
}

void ShaderManagerScreen::open(IClientInstance& client) {
    {
        std::lock_guard lock(mMutex);
        mClient = &client;
    }
    open();
}

void ShaderManagerScreen::open() {
    bool schedule = false;
    {
        std::lock_guard lock(mMutex);
        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().info(
                "[AerisUI] [2/4] ShaderManagerScreen::open() requested (visible={}, mOpenRequested={}, mScene={:p}, mWantPopScene={})",
                mScreen.visible(), mOpenRequested, static_cast<void*>(mScene.get()), mWantPopScene
            );
        }
        if (mScreen.visible() || mOpenRequested || mOpenScheduled || mScene || mWantPopScene) {
            return;
        }
        mOpenRequested = true;
        mOpenScheduled = true;
        schedule       = true;
    }

    if (schedule) {
        refreshPacks();
        auto client = ll::service::getClientInstance();
        if (client) {
            openOnClientThread();
        } else {
            ll::thread::ClientThreadExecutor::getDefault().execute([this] { openOnClientThread(); });
        }
    }
}

void ShaderManagerScreen::close() {
    std::lock_guard lock(mMutex);
    mOpenRequested = false;
    mOpenScheduled = false;

    if (!mScreen.visible() && !mScene) {
        return;
    }

    if (mScreen.visible()) {
        mScreen.setVisible(false);
    } else {
        popPlaceholderScene();
    }
}

void ShaderManagerScreen::openOnClientThread() {
    std::lock_guard lock(mMutex);
    mOpenScheduled = false;
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] openOnClientThread executing (mOpenRequested={}, mScreen.visible={})", mOpenRequested, mScreen.visible());
    }
    if (!mOpenRequested || mScreen.visible() || mScene || mWantPopScene) return;

    IClientInstance* client = mClient;
    if (client == nullptr) client = ll::service::getClientInstance().as_ptr();
    if (client == nullptr) {
        mOpenRequested = false;
        return;
    }
    mClient = client;
    pushPlaceholderScene(*client);
    if (mScene == nullptr) mOpenRequested = false;
}

void ShaderManagerScreen::pushPlaceholderScene(IClientInstance& client) {
    auto scene = client.getSceneFactory().createCommonDialogInfoScreen("", "");
    if (!scene) {
        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().warn("[AerisUI] createCommonDialogInfoScreen returned null");
        }
        return;
    }

    mScene         = std::move(scene);
    mScenePushedAt = std::chrono::steady_clock::now();
    mScreen.open();

    auto         sceneStack = client.getCurrentSceneStack();
    ISceneStack* stack      = sceneStack.operator->();
    if (stack == nullptr) {
        abandonSceneLocked();
        return;
    }

    std::string screensBefore;
    for (auto const& name : stack->getScreenNames()) {
        if (!screensBefore.empty()) screensBefore += ", ";
        screensBefore += name;
    }
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] pushPlaceholderScene: current SceneStack screens before push: [{}]", screensBefore);
    }

    // Check if the pause screen is already in the SceneStack
    bool const hasPauseScreen = stack->isOnSceneStack(::ui::SceneType::PauseScene)
                             || stack->isOnSceneStack("pause_screen");

    // If opened from gameplay (e.g. via hotkey 'G' or '/aeris ui') and not already paused,
    // push the native pause screen first to freeze the game world, then push our GUI immediately on top.
    bool const inWorld = (client.getLocalPlayer() != nullptr) || stack->isOnSceneStack("hud_screen");
    if (inWorld && !hasPauseScreen) {
        auto pauseScene = client.getSceneFactory().createPauseScreen(false);
        if (pauseScene) {
            if (auto mod = ll::mod::NativeMod::current()) {
                mod->getLogger().info("[AerisUI] Pushing underlying pause_screen to freeze game world before opening shaders GUI");
            }
            stack->pushScreen(pauseScene, false);
        }
    }

    stack->pushScreen(mScene, false);

    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] pushPlaceholderScene: placeholder scene pushed ({:p})", static_cast<void*>(mScene.get()));
    }
}

void ShaderManagerScreen::popPlaceholderScene() {
    std::shared_ptr<AbstractScene> scene;
    {
        std::lock_guard lock(mMutex);
        if (mWantPopScene) return;
        scene = mScene;
        if (!scene) {
            mOpenRequested = false;
            return;
        }
        mWantPopScene = true;
        mPopRetries   = 0;
    }

    auto client = ll::service::getClientInstance();
    if (client) {
        popOnClientThread(scene);
    } else {
        ll::thread::ClientThreadExecutor::getDefault().execute([this, scene] { popOnClientThread(scene); });
    }
}

void ShaderManagerScreen::popOnClientThread(std::shared_ptr<AbstractScene> scene) {
    std::lock_guard lock(mMutex);
    if (!mWantPopScene || mScene != scene) return;
    if (mScreen.visible()) {
        mWantPopScene = false;
        return;
    }

    IClientInstance* client = mClient;
    if (client == nullptr) client = ll::service::getClientInstance().as_ptr();
    if (client == nullptr) {
        abandonSceneLocked();
        return;
    }

    auto         sceneStack = client->getCurrentSceneStack();
    ISceneStack* stack      = sceneStack.operator->();
    if (stack == nullptr) {
        abandonSceneLocked();
        return;
    }

    if (stack->getTopScene() != scene.get()) {
        if (++mPopRetries <= 40) {
            ll::thread::ClientThreadExecutor::getDefault().executeAfter(
                [this, scene] { popOnClientThread(scene); },
                std::chrono::milliseconds(50)
            );
        } else {
            abandonSceneLocked();
        }
        return;
    }

    stack->schedulePopScreen(1);
    mWantPopScene = false;
    mPopRetries   = 0;
}

bool ShaderManagerScreen::ownsTopLocked() const {
    if (mClient == nullptr || !mScene) return false;
    auto         sceneStack = mClient->getCurrentSceneStack();
    ISceneStack* stack      = sceneStack.operator->();
    return stack != nullptr && stack->getTopScene() == mScene.get();
}

void ShaderManagerScreen::abandonSceneLocked() {
    mRetiredScene  = std::move(mScene);
    mOpenRequested = false;
    mOpenScheduled = false;
    mWantPopScene  = false;
    mPopRetries    = 0;
    mScreen.setVisible(false);
}

bool ShaderManagerScreen::isPlaceholderScene(AbstractScene const* scene) const {
    std::lock_guard lock(mMutex);
    return scene != nullptr && mScene.get() == scene;
}

void ShaderManagerScreen::notifyPlaceholderScenePopped(AbstractScene const* scene) {
    std::lock_guard lock(mMutex);
    if (scene == nullptr || mScene.get() != scene) return;

    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] notifyPlaceholderScenePopped {:p}", static_cast<void const*>(scene));
    }
    mRetiredScene  = std::move(mScene);
    mOpenRequested = false;
    mOpenScheduled = false;
    mWantPopScene  = false;
    mPopRetries    = 0;
    mScreen.setVisible(false);
}

void ShaderManagerScreen::render(MinecraftUIRenderContext& context, float screenWidth, float screenHeight) {
    std::lock_guard lock(mMutex);
    if (!mScreen.visible() || screenWidth <= 0.0f || screenHeight <= 0.0f) return;

    if (mClient == nullptr) mClient = &context.mClient;

    GameCanvas canvas(context);
    mScreen.render(canvas, screenWidth, screenHeight);
}

void ShaderManagerScreen::afterFrame(MinecraftUIRenderContext& context) {
    std::lock_guard lock(mMutex);
    mRetiredScene.reset();

    if (!mScreen.visible()) return;
    if (mClient != nullptr && &context.mClient != mClient) return;

    if (mScene && !ownsTopLocked()) {
        if (std::chrono::steady_clock::now() - mScenePushedAt > std::chrono::seconds(3)) {
            abandonSceneLocked();
        }
    }
}

bool ShaderManagerScreen::handleKey(int keyCode) {
    std::lock_guard lock(mMutex);
    if (!mScreen.visible()) return false;
    if (mScene && !ownsTopLocked()) return false;
    return mScreen.handleKey(keyCode);
}

bool ShaderManagerScreen::handleMouse(int action, float px, float py, bool down, int wheelDelta) {
    std::lock_guard lock(mMutex);
    if (!mScreen.visible()) return false;
    if (mScene && !ownsTopLocked()) return false;

    if (action == MouseAction::ActionWheel) {
        return mScreen.handleWheel(wheelDelta);
    }
    if (action == MouseAction::ActionLeft || action == MouseAction::ActionRight) {
        return mScreen.handleMouseButton(action, px, py, down);
    }
    mScreen.handleMouseMove(px, py, down);
    return true;
}

bool isOurUiPass(ScreenView const& view) {
    return gOurScreenView != nullptr && gOurScreenView == &view;
}

// Hooks for suppressing the placeholder dialog visual appearance
LL_TYPE_INSTANCE_HOOK(
    AerisSceneRender,
    ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$render,
    void,
    ::ScreenContext&           screenContext,
    ::FrameRenderObject const& renderObj
) {
    bool const ours = ShaderManagerScreen::getInstance().isPlaceholderScene(this);
    if (ours) {
        static bool sLoggedRenderFirst = false;
        if (!sLoggedRenderFirst) {
            sLoggedRenderFirst = true;
            if (auto mod = ll::mod::NativeMod::current()) {
                mod->getLogger().info("[AerisUI] [3/4] Minecraft actively rendering placeholder scene {:p} on screen!", static_cast<void*>(this));
            }
        }
    }
    ScreenView* const viewBefore = gOurScreenView;
    gOurScreenView               = ours ? mScreenView.get() : nullptr;
    origin(screenContext, renderObj);
    gOurScreenView = viewBefore;
}

LL_TYPE_INSTANCE_HOOK(
    AerisSceneEntrance,
    ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$onScreenEntrance,
    void,
    bool isRevisiting,
    bool doScreenTransitions
) {
    bool const ours = ShaderManagerScreen::getInstance().isPlaceholderScene(this);
    if (ours) {
        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().info("[AerisUI] Placeholder scene {:p} entered (isRevisiting={})", static_cast<void*>(this), isRevisiting);
        }
    }
    origin(isRevisiting, ours ? false : doScreenTransitions);
}

LL_TYPE_INSTANCE_HOOK(
    AerisSceneExit,
    ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$onScreenExit,
    void,
    bool                               isPopping,
    bool                               doScreenTransitions,
    ::std::shared_ptr<::AbstractScene> pushedScene
) {
    bool const ours = ShaderManagerScreen::getInstance().isPlaceholderScene(this);
    if (ours) {
        if (auto mod = ll::mod::NativeMod::current()) {
            mod->getLogger().info("[AerisUI] Placeholder scene {:p} exited (isPopping={})", static_cast<void*>(this), isPopping);
        }
    }
    origin(isPopping, ours ? false : doScreenTransitions, std::move(pushedScene));
    if (ours && isPopping) {
        ShaderManagerScreen::getInstance().notifyPlaceholderScenePopped(this);
    }
}

LL_TYPE_INSTANCE_HOOK(
    AerisSceneWorldBehind,
    ll::memory::HookPriority::Normal,
    UIScene,
    &UIScene::$renderGameBehind,
    bool
) {
    if (ShaderManagerScreen::getInstance().isPlaceholderScene(this)) return true;
    return origin();
}

void installNativeUiSuppressHooks() {
    AerisSceneRender::hook();
    AerisSceneEntrance::hook();
    AerisSceneExit::hook();
    AerisSceneWorldBehind::hook();
    if (auto mod = ll::mod::NativeMod::current()) {
        mod->getLogger().info("[AerisUI] Placeholder scene hooks installed successfully");
    }
}

void uninstallNativeUiSuppressHooks() {
    AerisSceneWorldBehind::unhook();
    AerisSceneExit::unhook();
    AerisSceneEntrance::unhook();
    AerisSceneRender::unhook();
}

} // namespace aeris::ui
