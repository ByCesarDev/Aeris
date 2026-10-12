#include "ui/UiScreen.h"

#include <algorithm>
#include <cmath>

namespace aeris::ui {

namespace {

// Theme Color Palette (Iris-Inspired Dark Theme with Clean Blue Accents)
constexpr Color kColBgBackdrop    = {0.05f, 0.055f, 0.065f, 0.85f};
constexpr Color kColHeaderBg      = {0.08f, 0.085f, 0.095f, 0.95f};
constexpr Color kColBottomBarBg   = {0.08f, 0.085f, 0.095f, 0.95f};
constexpr Color kColPanelBg       = {0.10f, 0.11f, 0.125f, 0.80f};
constexpr Color kColPanelBorder   = {0.18f, 0.20f, 0.23f, 1.0f};

constexpr Color kColCardNormal    = {0.12f, 0.13f, 0.145f, 0.90f};
constexpr Color kColCardHover     = {0.16f, 0.18f, 0.21f, 0.95f};
constexpr Color kColCardSelected  = {0.18f, 0.24f, 0.32f, 1.0f};
constexpr Color kColCardActiveBorder = {0.23f, 0.51f, 0.96f, 1.0f}; // Iris blue

constexpr Color kColBtnNormal     = {0.15f, 0.165f, 0.185f, 1.0f};
constexpr Color kColBtnHover      = {0.22f, 0.25f, 0.29f, 1.0f};
constexpr Color kColBtnPrimary    = {0.14f, 0.40f, 0.85f, 1.0f}; // Primary action button
constexpr Color kColBtnPrimaryHover = {0.20f, 0.48f, 0.95f, 1.0f};

constexpr Color kColTextTitle     = {0.96f, 0.97f, 0.98f, 1.0f};
constexpr Color kColTextMuted     = {0.60f, 0.63f, 0.68f, 1.0f};
constexpr Color kColTextAccent    = {0.35f, 0.65f, 1.0f, 1.0f};
constexpr Color kColTextSuccess   = {0.30f, 0.85f, 0.45f, 1.0f};
constexpr Color kColTextWarning   = {0.98f, 0.70f, 0.20f, 1.0f};

} // namespace

Screen::Screen() {
    // Default Vanilla card entry
    mPacks.push_back({
        "Vanilla",
        "1.0",
        "Mojang Studios",
        "Renderizado estandar sin modificaciones de shader.",
        "Forward",
        false,
        true
    });
}

void Screen::setVisible(bool value) {
    if (mVisible == value) return;
    mVisible = value;
    if (mVisible) {
        open();
    } else {
        close();
    }
}

void Screen::open() {
    mVisible = true;
    mScrollOffset = 0.0f;
    mShowDiagnostics = false;
}

void Screen::close() {
    mVisible = false;
    if (mHost.closeScreen) {
        mHost.closeScreen();
    }
}

void Screen::refreshPacksList(std::vector<PackCardInfo> packs, std::string const& currentActivePack) {
    mPacks.clear();
    // Always index 0 is Vanilla option
    mPacks.push_back({
        "Vanilla",
        "1.0",
        "Mojang Studios",
        "Renderizado oficial de Minecraft sin shaders activos.",
        "Forward",
        currentActivePack.empty(),
        true,
        false,
        false
    });

    mActivePackName = currentActivePack;
    mShadersEnabled = !currentActivePack.empty();

    for (auto& p : packs) {
        p.isActive = (!currentActivePack.empty() && p.name == currentActivePack);
        mPacks.push_back(p);
    }

    // Select the currently active pack by default
    mSelectedIndex = 0;
    for (size_t i = 0; i < mPacks.size(); ++i) {
        if (mPacks[i].isActive) {
            mSelectedIndex = static_cast<int>(i);
            break;
        }
    }
}

void Screen::setPointer(float x, float y) {
    mPointerX = x;
    mPointerY = y;
}

std::string Screen::tr(std::string_view key, std::string_view fallback) const {
    if (mHost.translate) {
        auto val = mHost.translate(key);
        if (!val.empty() && val != key) return val;
    }
    return fallback.empty() ? std::string(key) : std::string(fallback);
}

void Screen::click() const {
    if (mHost.playClick) {
        mHost.playClick();
    }
}

void Screen::showNotice(std::string message) {
    mNoticeText = std::move(message);
    mNoticeTime = std::chrono::steady_clock::now();
}

void Screen::render(Canvas& canvas, float width, float height) {
    if (!mVisible || width <= 0.0f || height <= 0.0f) return;

    mLayout = buildLayout(width, height, static_cast<int>(mPacks.size()), mScrollOffset, mShowDiagnostics);

    // 1. Overall Dark Backdrop
    canvas.fill(0.0f, 0.0f, width, height, kColBgBackdrop, 0.88f);

    // 2. Header
    drawHeader(canvas, mLayout);

    // 3. Central Content Areas
    drawPackList(canvas, mLayout);

    if (mShowDiagnostics) {
        drawDiagnosticsPanel(canvas, mLayout);
    } else {
        drawDetailsPanel(canvas, mLayout);
    }

    // 4. Bottom Action Bar
    drawBottomBar(canvas, mLayout);

    // 5. Toast Notice (if active)
    drawNotice(canvas, mLayout);

    canvas.flush();
}

void Screen::drawHeader(Canvas& canvas, Layout const& l) {
    canvas.fill(l.headerRect.x0, l.headerRect.y0, l.headerRect.width(), l.headerRect.height(), kColHeaderBg);
    // Divider line
    canvas.fill(0.0f, l.headerRect.y1 - 1.0f, l.screenWidth, 1.0f, kColPanelBorder);

    // Title & Subtitle
    canvas.text(16.0f, 8.0f, 250.0f, "AERIS  //  Shader Manager", kColTextTitle, Align::Left, 1.1f);
    canvas.text(16.0f, 22.0f, 250.0f, "Minecraft Bedrock 1.26.51.01 | RenderDragon", kColTextMuted, Align::Left, 0.8f);

    // Master Shaders Toggle Button
    bool toggleHover = l.masterToggleRect.contains(mPointerX, mPointerY);
    Color toggleBg = mShadersEnabled ? (toggleHover ? kColBtnPrimaryHover : kColBtnPrimary)
                                     : (toggleHover ? kColBtnHover : kColBtnNormal);
    canvas.fill(l.masterToggleRect.x0, l.masterToggleRect.y0, l.masterToggleRect.width(), l.masterToggleRect.height(), toggleBg);

    std::string toggleStr = mShadersEnabled ? "Shaders: ACTIVADOS" : "Shaders: DESACTIVADOS";
    canvas.text(l.masterToggleRect.x0, l.masterToggleRect.y0 + 5.0f, l.masterToggleRect.width(), toggleStr, kColTextTitle, Align::Center, 0.9f);
}

void Screen::drawPackList(Canvas& canvas, Layout const& l) {
    // List background panel
    canvas.fill(l.listArea.x0, l.listArea.y0, l.listArea.width(), l.listArea.height(), kColPanelBg);

    // Cards
    for (int i = 0; i < l.cardCount && i < static_cast<int>(mPacks.size()); ++i) {
        bool isSelected = (i == mSelectedIndex);
        bool isHovered = l.cardRects[i].contains(mPointerX, mPointerY);
        drawPackCard(canvas, l.cardRects[i], mPacks[static_cast<size_t>(i)], isSelected, isHovered);
    }

    // Scrollbar Track & Handle
    if (l.scrollHandle.valid()) {
        canvas.fill(l.scrollTrack.x0, l.scrollTrack.y0, l.scrollTrack.width(), l.scrollTrack.height(), Color{0.08f, 0.09f, 0.10f, 0.6f});
        canvas.fill(l.scrollHandle.x0, l.scrollHandle.y0, l.scrollHandle.width(), l.scrollHandle.height(), Color{0.35f, 0.38f, 0.44f, 0.8f});
    }
}

void Screen::drawPackCard(Canvas& canvas, Rect const& r, PackCardInfo const& card, bool isSelected, bool isHovered) {
    Color bg = isSelected ? kColCardSelected : (isHovered ? kColCardHover : kColCardNormal);
    canvas.fill(r.x0, r.y0, r.width(), r.height(), bg);

    if (isSelected || card.isActive) {
        // Selection left accent bar
        canvas.fill(r.x0, r.y0, 3.0f, r.height(), kColCardActiveBorder);
    }

    float tx = r.x0 + 8.0f;
    float ty = r.y0 + 6.0f;

    // Pack Name
    Color nameCol = card.isActive ? kColTextAccent : kColTextTitle;
    std::string displayName = card.name;
    if (card.isActive) {
        if (card.isBlockedBySafeMode) {
            displayName += "  [MODO SEGURO]";
            nameCol = kColTextWarning;
        } else if (card.hasActiveSubstitutions) {
            displayName += "  [ACTIVO EN GPU]";
            nameCol = kColTextSuccess;
        } else {
            displayName += "  [SELECCIONADO]";
            nameCol = kColTextAccent;
        }
    }
    canvas.text(tx, ty, r.width() - 80.0f, displayName, nameCol, Align::Left, 0.95f);

    // Subtitle / Details
    std::string sub = card.renderMode + " | v" + card.version + " | " + card.author;
    canvas.text(tx, ty + 14.0f, r.width() - 80.0f, sub, kColTextMuted, Align::Left, 0.75f);
}

void Screen::drawDetailsPanel(Canvas& canvas, Layout const& l) {
    canvas.fill(l.detailsArea.x0, l.detailsArea.y0, l.detailsArea.width(), l.detailsArea.height(), kColPanelBg);

    if (mSelectedIndex < 0 || mSelectedIndex >= static_cast<int>(mPacks.size())) return;
    auto const& cur = mPacks[static_cast<size_t>(mSelectedIndex)];

    float px = l.detailsArea.x0 + 12.0f;
    float py = l.detailsArea.y0 + 12.0f;
    float pw = l.detailsArea.width() - 24.0f;

    canvas.text(px, py, pw, cur.name, kColTextTitle, Align::Left, 1.15f);
    py += 18.0f;

    std::string meta = "Autor: " + cur.author + "  |  Version: " + cur.version;
    canvas.text(px, py, pw, meta, kColTextMuted, Align::Left, 0.85f);
    py += 16.0f;

    std::string modeStr = "Modo Grafico: " + cur.renderMode + " (Graficos Sofisticados)";
    canvas.text(px, py, pw, modeStr, kColTextAccent, Align::Left, 0.85f);
    py += 22.0f;

    // Divider
    canvas.fill(px, py, pw, 1.0f, kColPanelBorder);
    py += 10.0f;

    // Description
    canvas.text(px, py, pw, cur.description, kColTextTitle, Align::Left, 0.85f);
    py += 40.0f;

    // Status Box / Real State Notice
    Rect noticeBox{px, py, px + pw, py + 38.0f};
    if (cur.isVanilla) {
        canvas.fill(noticeBox.x0, noticeBox.y0, noticeBox.width(), noticeBox.height(), Color{0.12f, 0.16f, 0.20f, 0.85f});
        canvas.text(px + 8.0f, py + 6.0f, pw - 16.0f, "Modo Vanilla (Sin Shaders):", kColTextTitle, Align::Left, 0.8f);
        canvas.text(px + 8.0f, py + 20.0f, pw - 16.0f, "Renderizado estandar de Minecraft Bedrock sin modificaciones.", kColTextMuted, Align::Left, 0.75f);
    } else if (cur.isActive && cur.isBlockedBySafeMode) {
        canvas.fill(noticeBox.x0, noticeBox.y0, noticeBox.width(), noticeBox.height(), Color{0.25f, 0.18f, 0.05f, 0.85f});
        canvas.text(px + 8.0f, py + 6.0f, pw - 16.0f, "Aviso: Modo Seguro Activado", kColTextWarning, Align::Left, 0.8f);
        canvas.text(px + 8.0f, py + 20.0f, pw - 16.0f, "Paquete seleccionado, pero los overrides estan bloqueados por safeMode.", kColTextMuted, Align::Left, 0.75f);
    } else if (cur.isActive && cur.hasActiveSubstitutions) {
        canvas.fill(noticeBox.x0, noticeBox.y0, noticeBox.width(), noticeBox.height(), Color{0.05f, 0.22f, 0.10f, 0.85f});
        canvas.text(px + 8.0f, py + 6.0f, pw - 16.0f, "Shader Activo en RenderDragon:", kColTextSuccess, Align::Left, 0.8f);
        canvas.text(px + 8.0f, py + 20.0f, pw - 16.0f, "Materiales inyectados en memoria. Cambiar de pack requiere reiniciar mundo/juego.", kColTextMuted, Align::Left, 0.75f);
    } else if (cur.isActive) {
        canvas.fill(noticeBox.x0, noticeBox.y0, noticeBox.width(), noticeBox.height(), Color{0.08f, 0.18f, 0.28f, 0.85f});
        canvas.text(px + 8.0f, py + 6.0f, pw - 16.0f, "Indice Preparado (Pendiente GPU):", kColTextAccent, Align::Left, 0.8f);
        canvas.text(px + 8.0f, py + 20.0f, pw - 16.0f, "Nuevas solicitudes usaran este pack. GPU recargara al entrar a un mundo.", kColTextMuted, Align::Left, 0.75f);
    } else {
        canvas.fill(noticeBox.x0, noticeBox.y0, noticeBox.width(), noticeBox.height(), Color{0.12f, 0.14f, 0.18f, 0.85f});
        canvas.text(px + 8.0f, py + 6.0f, pw - 16.0f, "Paquete Disponible:", kColTextMuted, Align::Left, 0.8f);
        canvas.text(px + 8.0f, py + 20.0f, pw - 16.0f, "Haz clic en Hecho (Esc) para guardar y preparar este shaderpack.", kColTextMuted, Align::Left, 0.75f);
    }
}

void Screen::drawDiagnosticsPanel(Canvas& canvas, Layout const& l) {
    canvas.fill(l.detailsArea.x0, l.detailsArea.y0, l.detailsArea.width(), l.detailsArea.height(), kColPanelBg);

    float px = l.detailsArea.x0 + 12.0f;
    float py = l.detailsArea.y0 + 12.0f;
    float pw = l.detailsArea.width() - 24.0f;

    canvas.text(px, py, pw, "Diagnostico de Aeris", kColTextTitle, Align::Left, 1.15f);
    py += 22.0f;

    DiagnosticsInfo diag{};
    if (mHost.getDiagnostics) {
        diag = mHost.getDiagnostics();
    }

    std::string safeStr = diag.safeMode ? "Modo Seguro: ACTIVO (Sin modificaciones)" : "Modo Seguro: DESACTIVADO (Hooks en vivo)";
    Color safeCol = diag.safeMode ? kColTextWarning : kColTextSuccess;
    canvas.text(px, py, pw, safeStr, safeCol, Align::Left, 0.85f);
    py += 16.0f;

    std::string hookStr = "Hook readFile: " + std::string(diag.hookInstalled ? "Instalado (Activo en memoria)" : "Desconectado");
    canvas.text(px, py, pw, hookStr, kColTextMuted, Align::Left, 0.85f);
    py += 16.0f;

    std::string subStr = "Sustituciones Activas en Sesion: " + std::to_string(diag.activeSubstitutions);
    canvas.text(px, py, pw, subStr, diag.activeSubstitutions > 0 ? kColTextSuccess : kColTextMuted, Align::Left, 0.85f);
    py += 16.0f;

    std::string readsStr = "Lecturas observadas: " + std::to_string(diag.passiveReads) + " (" + std::to_string(diag.materialBinReads) + " .bin)";
    canvas.text(px, py, pw, readsStr, kColTextMuted, Align::Left, 0.85f);
    py += 16.0f;

    std::string errStr = "Errores de validacion binaria: " + std::to_string(diag.validationErrors);
    canvas.text(px, py, pw, errStr, diag.validationErrors > 0 ? kColTextWarning : kColTextMuted, Align::Left, 0.85f);
    py += 24.0f;

    canvas.fill(px, py, pw, 1.0f, kColPanelBorder);
    py += 12.0f;

    canvas.text(px, py, pw, "Materiales Activos Autorizados:", kColTextAccent, Align::Left, 0.85f);
    py += 16.0f;
    canvas.text(px + 8.0f, py, pw, "* UIBlit.material.bin (UI Interface)", kColTextMuted, Align::Left, 0.8f);
    py += 14.0f;
    canvas.text(px + 8.0f, py, pw, "* Cubemap.material.bin / CubemapHDRi.material.bin", kColTextMuted, Align::Left, 0.8f);
    py += 14.0f;
    canvas.text(px + 8.0f, py, pw, "* Sky.material.bin (Cielo Atmosferico)", kColTextMuted, Align::Left, 0.8f);
}

void Screen::drawBottomBar(Canvas& canvas, Layout const& l) {
    canvas.fill(l.bottomBarRect.x0, l.bottomBarRect.y0, l.bottomBarRect.width(), l.bottomBarRect.height(), kColBottomBarBg);
    // Divider line
    canvas.fill(0.0f, l.bottomBarRect.y0, l.screenWidth, 1.0f, kColPanelBorder);

    const char* btnLabels[kMaxActionButtons] = {
        "Importar...",
        "Abrir Carpeta",
        mShowDiagnostics ? "Ver Detalles" : "Diagnostico",
        "Hecho (Esc)"
    };

    for (int i = 0; i < l.buttonCount; ++i) {
        Rect const& r = l.buttons[i];
        bool isHovered = r.contains(mPointerX, mPointerY);
        bool isDoneBtn = (i == static_cast<int>(ButtonId::Done));

        Color bg = isDoneBtn ? (isHovered ? kColBtnPrimaryHover : kColBtnPrimary)
                             : (isHovered ? kColBtnHover : kColBtnNormal);

        canvas.fill(r.x0, r.y0, r.width(), r.height(), bg);
        canvas.text(r.x0, r.y0 + 5.0f, r.width(), btnLabels[i], kColTextTitle, Align::Center, 0.85f);
    }
}

void Screen::drawNotice(Canvas& canvas, Layout const& l) {
    if (mNoticeText.empty()) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - mNoticeTime).count();
    if (elapsed > 3500) {
        mNoticeText.clear();
        return;
    }

    float alpha = 1.0f;
    if (elapsed > 2500) {
        alpha = 1.0f - static_cast<float>(elapsed - 2500) / 1000.0f;
    }

    float nw = std::min(l.screenWidth * 0.7f, 320.0f);
    float nh = 24.0f;
    float nx = (l.screenWidth - nw) * 0.5f;
    float ny = l.screenHeight - kBottomBarHeight - nh - 8.0f;

    canvas.fill(nx, ny, nw, nh, Color{0.08f, 0.12f, 0.18f, 0.95f}, alpha);
    canvas.text(nx, ny + 5.0f, nw, mNoticeText, Color{0.35f, 0.75f, 1.0f, alpha}, Align::Center, 0.85f);
}

void Screen::selectPack(int index) {
    if (index < 0 || index >= static_cast<int>(mPacks.size())) return;
    mSelectedIndex = index;
    click();

    auto const& sel = mPacks[static_cast<size_t>(index)];
    if (sel.isVanilla) {
        mShadersEnabled = false;
        showNotice("Vanilla seleccionado. Shaders desactivados.");
    } else {
        mShadersEnabled = true;
        showNotice("Shader '" + sel.name + "' seleccionado.");
    }
}

void Screen::toggleShaders() {
    mShadersEnabled = !mShadersEnabled;
    click();
    if (!mShadersEnabled) {
        mSelectedIndex = 0; // Vanilla
        showNotice("Shaders desactivados (Modo Vanilla)");
    } else {
        if (mSelectedIndex == 0 && mPacks.size() > 1) {
            mSelectedIndex = 1; // Pick first available shaderpack
        }
        showNotice("Shaders activados");
    }
}

void Screen::applyAndClose() {
    click();
    if (!mShadersEnabled || mSelectedIndex == 0) {
        if (mHost.onDisableShaders) {
            mHost.onDisableShaders();
        }
    } else if (mSelectedIndex < static_cast<int>(mPacks.size())) {
        auto const& sel = mPacks[static_cast<size_t>(mSelectedIndex)];
        if (mHost.onApplyPack) {
            mHost.onApplyPack(sel.name);
        }
    }
    close();
}

bool Screen::handleKey(int keyCode) {
    if (!mVisible) return false;

    // Windows VK codes: VK_ESCAPE = 27, VK_UP = 38, VK_DOWN = 40, VK_RETURN = 13, 'G' = 0x47
    switch (keyCode) {
    case 27: // VK_ESCAPE
        applyAndClose();
        return true;
    case 38: // VK_UP
        if (mSelectedIndex > 0) {
            selectPack(mSelectedIndex - 1);
        }
        return true;
    case 40: // VK_DOWN
        if (mSelectedIndex + 1 < static_cast<int>(mPacks.size())) {
            selectPack(mSelectedIndex + 1);
        }
        return true;
    case 13: // VK_RETURN
        applyAndClose();
        return true;
    default:
        break;
    }
    return false;
}

bool Screen::handleWheel(int delta) {
    if (!mVisible) return false;
    mScrollOffset = std::max(0.0f, mScrollOffset - static_cast<float>(delta) * 0.25f);
    return true;
}

void Screen::handleMouseMove(float x, float y, bool down) {
    mPointerX = x;
    mPointerY = y;
    mMouseDown = down;
}

bool Screen::handleMouseButton(int button, float x, float y, bool down) {
    if (!mVisible) return false;
    mPointerX = x;
    mPointerY = y;
    mMouseDown = down;

    if (!down || button != 1) { // Left click down only
        return false;
    }

    // 1. Check Master Toggle
    if (mLayout.hitMasterToggle(x, y)) {
        toggleShaders();
        return true;
    }

    // 2. Check Action Buttons
    ButtonId btn = mLayout.hitButton(x, y);
    if (btn != ButtonId::None) {
        click();
        switch (btn) {
        case ButtonId::Import:
            if (mHost.pickImportFile) {
                std::string chosenPath;
                if (mHost.pickImportFile(chosenPath)) {
                    showNotice("Importacion procesada.");
                }
            }
            break;
        case ButtonId::OpenFolder:
            if (mHost.openPacksFolder) {
                mHost.openPacksFolder();
            }
            break;
        case ButtonId::Diagnostics:
            mShowDiagnostics = !mShowDiagnostics;
            break;
        case ButtonId::Done:
            applyAndClose();
            break;
        default:
            break;
        }
        return true;
    }

    // 3. Check Pack Cards
    int cardIdx = mLayout.hitCard(x, y);
    if (cardIdx >= 0) {
        selectPack(cardIdx);
        return true;
    }

    return true; // Consume click inside UI window
}

} // namespace aeris::ui
