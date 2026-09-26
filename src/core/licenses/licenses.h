#pragma once

#include <string>
#include <vector>

#include "assets/font/font.h"
#include "core/licenses/licensetext.h"
#include "math/rectangles.h"
#include "math/vectors.h"
#include "types/color.h"

/// @brief Look of the licenses screen. Every field has a working default.
struct LicensesStyle {
    FontAsset *titleFont = nullptr; ///< Title and component names. Null uses the engine default font.
    FontAsset *bodyFont  = nullptr; ///< Everything else. Null uses the engine default font.

    Color backdrop { 0, 0, 0, 170 };      ///< Dims the game behind the screen.
    Color panel { 18, 22, 30, 242 };      ///< Panel background.
    Color divider { 255, 255, 255, 24 };  ///< Hairlines between regions.
    Color text { 228, 232, 240, 255 };    ///< Primary text.
    Color dimText { 140, 150, 166, 255 }; ///< Secondary text: license ids, hints, the subtitle.
    Color accent { 92, 170, 255, 255 };   ///< Selection highlight and scrollbar.

    float scale        = 1.0f;  ///< Multiplies every size. Sizes are tuned for a window 1080 pixels tall and follow its height.
    float safeArea     = 0.05f; ///< Fraction of the window's shorter side kept clear on every edge, for TVs that overscan.
    float cornerRadius = 14.0f; ///< Panel corner radius at scale 1.

    std::string title    = "Third-Party Software";
    std::string subtitle = "This software is built with the following open-source components.";
};

/// @brief One third-party component linked into this build.
struct ThirdPartyNotice {
    std::string name;
    std::string spdx; ///< SPDX license identifier, e.g. "MIT". May be empty.
    std::string version;
    std::string url;
    std::string text; ///< The license file exactly as the component distributes it.
};

/**
 * @brief The third-party licenses screen, and the notice data behind it.
 *
 * Every component the engine links registers its license at build time (see
 * cmake/ThirdPartyNotices.cmake), so the text is compiled into every game and a
 * THIRD_PARTY_NOTICES.txt lands next to every executable. This shows that text to players.
 *
 * On desktop and the web there is nothing to call: `--lumi-licenses` on the command line, or
 * `?licenses` in the page URL, opens the screen at startup. Platforms with neither, like consoles
 * and phones, need a menu item:
 *
 * @code
 * if (licensesSelected)
 *     Licenses::Open();
 * if (Licenses::IsOpen())
 *     return; // the screen owns input until the player backs out
 * @endcode
 *
 * The screen draws on top of everything and reads keyboard, mouse, touch and every connected
 * gamepad. The game still receives that same input, so check IsOpen() before acting on it.
 */
class Licenses {
public:
    /**
     * @brief Opens the screen. Input on the frame it opens is ignored, so the press that selected
     *        the menu item does not also act on the screen.
     *
     * @param style Colors, fonts and sizing.
     */
    static void Open(const LicensesStyle &style = {}) { Get()._open(style); }

    /// @brief Closes the screen. Quits the app instead when it was opened by `--lumi-licenses`.
    static void Close() { Get()._close(); }

    /// @brief Whether the screen is showing and taking input.
    static bool IsOpen() { return Get()._isOpen; }

    /// @brief Every component linked into this build, in registration order.
    static const std::vector<ThirdPartyNotice> &GetNotices() { return Get()._getNotices(); }

    /// @brief All notices as one plain-text document, the same content as THIRD_PARTY_NOTICES.txt.
    static std::string GetNoticesText() { return Get()._getNoticesText(); }

    /**
     * @brief Opens the screen if `--lumi-licenses` is among the arguments (or, on the web, if the
     *        page URL has `?licenses`). The callback main loop calls this already; a game with its
     *        own main() calls it once at startup.
     *
     * @return True when the screen was opened.
     */
    static bool HandleCommandLine(int argc, char *argv[]) { return Get()._handleCommandLine(argc, argv); }

    /// @cond INTERNAL
    /// @brief Reads input and draws the screen. Called by the engine at the end of every frame.
    static void Render() { Get()._render(); }
    /// @endcond

private:
    /// @cond INTERNAL
    struct Layout {
        float width = 0, height = 0, unit = 1, pad = 0;
        bool  stacked = false;

        rectf panel, list, detail, text, closeButton;
        float contentTop = 0, contentBottom = 0;

        float titleSize = 0, subtitleSize = 0, rowSize = 0, rowMetaSize = 0, nameSize = 0, metaSize = 0;
        float bodySize = 0, lineHeight = 0, rowHeight = 0, hintSize = 0;
    };

    struct TextLine {
        std::string text;
        float       y      = 0;
        float       size   = 0;
        bool        dimmed = false;
    };

    enum class View { List, Detail };
    enum class Drag { None, List, Text };
    /// @endcond

    Licenses() = default;
    static Licenses &Get() {
        static Licenses instance;
        return instance;
    }

    void _open(const LicensesStyle &style);
    void _close();
    void _back(const Layout &layout);
    bool _handleCommandLine(int argc, char *argv[]);

    const std::vector<ThirdPartyNotice> &_getNotices();
    std::string                          _getNoticesText();

    void   _render();
    void   _initOverlay();
    void   _loadCatalog();
    Layout _computeLayout() const;
    void   _layoutRows(const Layout &layout);
    void   _layoutText(const Layout &layout);
    void   _handleInput(const Layout &layout, float dt);
    void   _updateScroll(const Layout &layout, float dt);
    void   _select(int index, const Layout &layout);

    void _draw(const Layout &layout);
    void _drawList(const Layout &layout);
    void _drawDetail(const Layout &layout);
    void _drawFooter(const Layout &layout);

    FontAsset &_titleFont() const;
    FontAsset &_bodyFont() const;
    bool       _listVisible(const Layout &layout) const { return !layout.stacked || _view == View::List; }
    bool       _detailVisible(const Layout &layout) const { return !layout.stacked || _view == View::Detail; }

    LicensesStyle _style;
    bool          _isOpen      = false;
    bool          _quitOnClose = false;
    bool          _fbReady     = false;
    int           _openedFrame = -1;

    std::vector<ThirdPartyNotice> _notices;
    LicenseText::Catalog          _catalog;
    bool                          _catalogReady = false;

    int   _selected   = 0;
    View  _view       = View::List;
    float _listScroll = 0, _listTarget = 0;
    float _textScroll = 0, _textTarget = 0, _textVelocity = 0;

    std::vector<std::string> _rowNames;
    std::vector<std::string> _rowLicenses;
    float                    _rowsWidth = -1, _rowsUnit = -1;

    std::vector<TextLine> _lines;
    float                 _contentHeight = 0;
    int                   _linesComponent = -1;
    float                 _linesWidth = -1, _linesUnit = -1;

    int   _repeatDir    = 0;
    float _repeatTimer  = 0;
    bool  _usingGamepad = false;
    int   _lastGamepad  = -1;
    vf2d  _lastMouse { -1, -1 };
    Drag  _drag         = Drag::None;
    float _dragLastY    = 0;
    float _dragDistance = 0;
};
