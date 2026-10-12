#include "TestRunner.h"
#include "ui/UiLayout.h"
#include "ui/UiScreen.h"

using namespace aeris::ui;

class MockCanvas : public Canvas {
public:
    int fillCount = 0;
    int textCount = 0;
    int flushCount = 0;

    void fill(float, float, float, float, Color, float) override {
        fillCount++;
    }

    void text(float, float, float, std::string const&, Color, Align, float) override {
        textCount++;
    }

    void flush() override {
        flushCount++;
    }
};

void runUiTests() {
    std::cout << "\nRunning Aeris UI Layout & State Tests...\n";

    // 1. Layout Responsive Bounds
    {
        Layout l = buildLayout(800.0f, 600.0f, 5);
        check(l.screenWidth == 800.0f, "Layout width must match 800");
        check(l.screenHeight == 600.0f, "Layout height must match 600");
        check(l.headerRect.valid(), "Header rect must be valid");
        check(l.bottomBarRect.valid(), "Bottom bar rect must be valid");
        check(l.listArea.valid(), "List area must be valid");
        check(l.detailsArea.valid(), "Details area must be valid");
        check(l.cardCount == 5, "Card count must be 5");
        check(l.buttonCount == 4, "Must have 4 action buttons");
    }

    // 2. Zero / Invalid Dimension Bounds
    {
        Layout l = buildLayout(0.0f, 0.0f, 3);
        check(!l.headerRect.valid(), "Zero size layout must be invalid");
    }

    // 3. Hit Testing Math
    {
        Layout l = buildLayout(800.0f, 600.0f, 4);
        
        // Hit Header Master Toggle
        float tx = (l.masterToggleRect.x0 + l.masterToggleRect.x1) * 0.5f;
        float ty = (l.masterToggleRect.y0 + l.masterToggleRect.y1) * 0.5f;
        check(l.hitMasterToggle(tx, ty), "Must hit master toggle at center");
        check(!l.hitMasterToggle(0.0f, 0.0f), "Top-left must not hit master toggle");

        // Hit First Pack Card
        float c0x = (l.cardRects[0].x0 + l.cardRects[0].x1) * 0.5f;
        float c0y = (l.cardRects[0].y0 + l.cardRects[0].y1) * 0.5f;
        check(l.hitCard(c0x, c0y) == 0, "Must hit card 0");

        // Hit Done Button (index 3)
        Rect doneRect = l.buttons[3];
        float bx = (doneRect.x0 + doneRect.x1) * 0.5f;
        float by = (doneRect.y0 + doneRect.y1) * 0.5f;
        check(l.hitButton(bx, by) == ButtonId::Done, "Must hit Done action button");
    }

    // 4. Screen State Machine & Mock Canvas
    {
        Screen screen;
        MockCanvas canvas;

        ScreenHost host;
        bool appliedPack = false;
        std::string appliedName;
        host.onApplyPack = [&](std::string const& name) {
            appliedPack = true;
            appliedName = name;
        };
        host.getDiagnostics = []() -> DiagnosticsInfo {
            DiagnosticsInfo d{};
            d.safeMode = false;
            d.hookInstalled = true;
            d.activeSubstitutions = 42;
            d.passiveReads = 100;
            d.materialBinReads = 50;
            return d;
        };
        screen.setHost(std::move(host));

        std::vector<PackCardInfo> testPacks = {
            {"SamplePack", "1.0", "AuthorA", "DescA", "Forward", true, false, false, true},
            {"Daydream", "1.7", "AuthorB", "DescB", "Forward", false, false, true, false}
        };

        screen.refreshPacksList(testPacks, "SamplePack");
        check(screen.shadersEnabled(), "Shaders must be initially enabled with SamplePack active");
        check(screen.selectedIndex() == 1, "Selected index must match active SamplePack (index 1 after Vanilla)");

        // Open and Render
        screen.open();
        check(screen.visible(), "Screen must be visible after open()");

        screen.render(canvas, 640.0f, 480.0f);
        check(canvas.fillCount > 0, "Canvas must receive fill commands");
        check(canvas.textCount > 0, "Canvas must receive text commands");
        check(canvas.flushCount > 0, "Canvas must receive flush command");

        // Switch to diagnostics tab
        // Button index 2 is Diagnostics button
        Rect diagBtn = screen.layout().buttons[2];
        screen.handleMouseButton(1, (diagBtn.x0 + diagBtn.x1) * 0.5f, (diagBtn.y0 + diagBtn.y1) * 0.5f, true);
        check(screen.isDiagnosticsOpen(), "Clicking Diagnostics button must open diagnostics tab");
        screen.render(canvas, 640.0f, 480.0f);

        // Switch back to details
        screen.handleMouseButton(1, (diagBtn.x0 + diagBtn.x1) * 0.5f, (diagBtn.y0 + diagBtn.y1) * 0.5f, true);
        check(!screen.isDiagnosticsOpen(), "Clicking button again must return to details tab");

        // Toggle Master Switch
        screen.handleMouseButton(1, screen.layout().masterToggleRect.x0 + 5.0f, screen.layout().masterToggleRect.y0 + 5.0f, true);
        check(!screen.shadersEnabled(), "After toggle click, shaders must be disabled");
        check(screen.selectedIndex() == 0, "When shaders are disabled, selection must revert to Vanilla (index 0)");

        // Keyboard Arrow Down
        screen.handleKey(40); // VK_DOWN
        check(screen.selectedIndex() == 1, "VK_DOWN must select next pack");

        // Close
        screen.close();
        check(!screen.visible(), "Screen must not be visible after close()");
    }
}
