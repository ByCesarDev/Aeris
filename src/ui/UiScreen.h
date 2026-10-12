#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ui/UiCanvas.h"
#include "ui/UiLayout.h"

namespace aeris::ui {

struct PackCardInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string renderMode; // "Forward" or "Deferred"
    bool        isActive = false;
    bool        isVanilla = false;
};

struct ScreenHost {
    std::function<std::string(std::string_view)> translate;
    std::function<void(std::string const&)>      log;
    std::function<void()>                        playClick;
    std::function<void()>                        closeScreen;
    std::function<void()>                        openPacksFolder;
    std::function<bool(std::string& outPath)>    pickImportFile;
    std::function<void(std::string const& name)> onApplyPack;
    std::function<void()>                        onDisableShaders;
};

class Screen {
public:
    Screen();

    void setHost(ScreenHost host) { mHost = std::move(host); }

    [[nodiscard]] bool visible() const { return mVisible; }
    void setVisible(bool value);
    void open();
    void close();

    void refreshPacksList(std::vector<PackCardInfo> packs, std::string const& currentActivePack);

    void setPointer(float x, float y);
    void render(Canvas& canvas, float width, float height);

    bool handleKey(int keyCode); // Windows VK keys (VK_ESCAPE, VK_UP, VK_DOWN, etc.)
    bool handleWheel(int delta);
    void handleMouseMove(float x, float y, bool down);
    bool handleMouseButton(int button, float x, float y, bool down);

    [[nodiscard]] int selectedIndex() const { return mSelectedIndex; }
    [[nodiscard]] bool shadersEnabled() const { return mShadersEnabled; }
    [[nodiscard]] bool isDiagnosticsOpen() const { return mShowDiagnostics; }
    [[nodiscard]] Layout const& layout() const { return mLayout; }

    void showNotice(std::string message);

private:
    std::string tr(std::string_view key, std::string_view fallback = "") const;
    void click() const;

    void drawHeader(Canvas& canvas, Layout const& l);
    void drawPackList(Canvas& canvas, Layout const& l);
    void drawPackCard(Canvas& canvas, Rect const& r, PackCardInfo const& card, bool isSelected, bool isHovered);
    void drawDetailsPanel(Canvas& canvas, Layout const& l);
    void drawDiagnosticsPanel(Canvas& canvas, Layout const& l);
    void drawBottomBar(Canvas& canvas, Layout const& l);
    void drawNotice(Canvas& canvas, Layout const& l);

    void selectPack(int index);
    void toggleShaders();
    void applyAndClose();

    bool                       mVisible          = false;
    bool                       mShadersEnabled   = true;
    bool                       mShowDiagnostics  = false;
    int                        mSelectedIndex    = 0;
    float                      mScrollOffset     = 0.0f;
    float                      mPointerX         = -1.0f;
    float                      mPointerY         = -1.0f;
    bool                       mMouseDown        = false;

    std::vector<PackCardInfo>  mPacks;
    std::string                mActivePackName;

    std::string                mNoticeText;
    std::chrono::steady_clock::time_point mNoticeTime{};

    Layout                     mLayout;
    ScreenHost                 mHost;
};

} // namespace aeris::ui
