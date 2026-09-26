#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

/// @cond INTERNAL
/**
 * Turns raw license files into text that reads well on a screen. It has no engine dependencies,
 * so the whole pipeline runs without a window.
 *
 *   Build()          groups components whose licenses differ only in their copyright lines, so
 *                    the MIT text is shown once with a list of holders instead of once per library
 *   Reflow()         undoes the hard wrapping at 80 columns, keeping list items, headings and
 *                    copyright lines on lines of their own
 *   Wrap()           breaks one reflowed paragraph to a pixel width
 *   ToFontCharset()  maps typographic punctuation to ASCII, because the engine's fonts are baked
 *                    for U+0020..U+017F and anything outside it would silently disappear
 */
class LicenseText {
public:
    /// @brief A component as registered at build time.
    struct Source {
        std::string_view name;
        std::string_view spdx;
        std::string_view version;
        std::string_view url;
        std::string_view text;
    };

    /// @brief A component ready to display.
    struct Component {
        std::string              name;
        std::string              spdx;
        std::string              version;
        std::string              url;
        std::vector<std::string> copyrights;      ///< Filled only for shared texts; an unshared text keeps them inline.
        size_t                   textIndex   = 0; ///< Index into Catalog::texts.
        size_t                   sharedCount = 1; ///< Number of components that display the same text.
    };

    /// @brief Every component plus the deduplicated texts they point at.
    struct Catalog {
        std::vector<Component>                components;
        std::vector<std::vector<std::string>> texts; ///< Reflowed paragraphs; '\n' inside one marks a forced line break.
    };

    using MeasureFn = std::function<float(std::string_view)>;

    static Catalog Build(const std::vector<Source> &sources);

    static std::vector<std::string> Reflow(std::string_view text);

    static std::vector<std::string> Wrap(std::string_view paragraph, float maxWidth, const MeasureFn &measure);

    static std::string ToFontCharset(std::string_view utf8);
};
/// @endcond
