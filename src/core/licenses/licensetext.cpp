#include "core/licenses/licensetext.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <unordered_map>

namespace {

constexpr uint32_t LAST_FONT_CODEPOINT = 0x17F;

// A shared text needs enough body to be worth sharing. Below this, two licenses that are nothing
// but a copyright line and a URL would collapse into one entry that says nothing.
constexpr size_t MIN_SHARED_BODY = 100;

// Lines indented this far are centered headings, not wrapped prose.
constexpr size_t CENTERED_INDENT = 8;

uint32_t decodeUtf8(std::string_view s, size_t &i) {
    unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80)
        return c;

    int      extra     = 0;
    uint32_t codepoint = 0;
    if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        extra     = 1;
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        extra     = 2;
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        extra     = 3;
    } else {
        return 0xFFFD;
    }

    for (int k = 0; k < extra; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            return 0xFFFD;
        codepoint = (codepoint << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return codepoint;
}

void appendUtf8(std::string &out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

size_t indentOf(std::string_view line) {
    size_t n = 0;
    while (n < line.size() && line[n] == ' ')
        ++n;
    return n;
}

bool startsWith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t                        start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string lowercase(std::string_view s) {
    std::string out(s);
    for (char &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// Source headers quoted inside a license keep their comment markers; they are not part of the text.
std::string_view stripCommentMarker(std::string_view line) {
    line = trim(line);
    if (startsWith(line, "//"))
        line.remove_prefix(2);
    else if (startsWith(line, "**"))
        line.remove_prefix(2);
    else if (startsWith(line, "* ") || line == "*")
        line.remove_prefix(1);
    return trim(line);
}

// A notice, not a sentence about copyright: "Copyright (c) 2018 Name", "Copyright 2020 Khronos",
// "(C) 2017 Name", "© 2024 Name". License bodies are full of lines that merely begin with the
// word ("copyright notice that is included..."), so a year, or a placeholder for one, must follow.
bool isCopyrightLine(std::string_view line) {
    std::string_view content = stripCommentMarker(line);
    if (content.empty() || content.size() > 200)
        return false;

    const std::string lower = lowercase(content);
    if (lower.find("[yyyy]") != std::string::npos)
        return false; // the Apache appendix's fill-in-the-blanks template

    std::string_view rest   = lower;
    bool             marker = false;
    if (startsWith(rest, "portions "))
        rest.remove_prefix(9);
    if (startsWith(rest, "copyright")) {
        rest.remove_prefix(9);
        rest   = trim(rest);
        marker = true;
    }
    if (startsWith(rest, "(c)")) {
        rest.remove_prefix(3);
        marker = true;
    } else if (startsWith(rest, "\xC2\xA9")) {
        rest.remove_prefix(2);
        marker = true;
    }
    rest = trim(rest);

    return marker && !rest.empty() && (std::isdigit(static_cast<unsigned char>(rest[0])) || rest[0] == '<');
}

bool isTitleLine(std::string_view line) {
    std::string lower = lowercase(trim(line));
    return lower == "mit license" || lower == "the mit license" || lower == "mit license (mit)"
        || lower == "the mit license (mit)";
}

bool isRule(std::string_view line) {
    line = trim(line);
    if (line.size() < 3)
        return false;
    return std::all_of(line.begin(), line.end(), [](char c) {
        return c == '-' || c == '=' || c == '_' || c == '*' || c == '~';
    });
}

// "- item", "* item", "1. item", "a) item", "(b) item", "iii. item"
bool isListItem(std::string_view line) {
    if (line.size() < 2)
        return false;
    if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ')
        return true;

    size_t i = 0;
    if (line[0] == '(')
        i = 1;
    size_t alnumStart = i;
    while (i < line.size() && i - alnumStart < 4 && std::isalnum(static_cast<unsigned char>(line[i])))
        ++i;
    size_t alnum = i - alnumStart;
    if (alnum == 0 || alnum > 3 || i >= line.size())
        return false;
    if (line[i] != '.' && line[i] != ')')
        return false;
    return i + 1 < line.size() && line[i + 1] == ' ';
}

void appendCollapsed(std::string &out, std::string_view text) {
    for (char c : text) {
        if (c == ' ' && !out.empty() && (out.back() == ' ' || out.back() == '\n'))
            continue;
        out += c;
    }
}

// Letters and digits only, lowercased: two copies of the same license that were wrapped or
// punctuated differently still produce the same key.
std::string groupingKey(std::string_view body) {
    std::string key;
    key.reserve(body.size());
    for (char c : body) {
        unsigned char u = static_cast<unsigned char>(c);
        if (std::isalnum(u))
            key += static_cast<char>(std::tolower(u));
    }
    return key;
}

} // namespace

std::string LicenseText::ToFontCharset(std::string_view utf8) {
    std::string out;
    out.reserve(utf8.size());

    for (size_t i = 0; i < utf8.size();) {
        uint32_t cp = decodeUtf8(utf8, i);
        switch (cp) {
        case '\r':
        case 0xFEFF:
            continue;
        case '\t':
        case 0xA0:
            out += ' ';
            continue;
        case 0x2018:
        case 0x2019:
        case 0x201A:
        case 0x2032:
            out += '\'';
            continue;
        case 0x201C:
        case 0x201D:
        case 0x201E:
        case 0x2033:
            out += '"';
            continue;
        case 0x2010:
        case 0x2011:
        case 0x2012:
        case 0x2013:
        case 0x2014:
        case 0x2015:
        case 0x2212:
            out += '-';
            continue;
        case 0x2026:
            out += "...";
            continue;
        case 0x2022:
        case 0x2023:
        case 0x25CF:
        case 0x25E6:
            out += '*';
            continue;
        default:
            break;
        }

        if (cp == '\n' || (cp >= 0x20 && cp < 0x7F) || (cp >= 0xA0 && cp <= LAST_FONT_CODEPOINT))
            appendUtf8(out, cp);
        else if (cp >= 0x20 && !(cp >= 0x7F && cp < 0xA0))
            out += '?';
    }
    return out;
}

std::vector<std::string> LicenseText::Reflow(std::string_view text) {
    std::vector<std::string>      paragraphs;
    std::vector<std::string_view> block;

    auto flush = [&]() {
        if (block.empty())
            return;

        bool commented = std::all_of(block.begin(), block.end(), [](std::string_view l) {
            return startsWith(trim(l), "//");
        });

        size_t longest = 0;
        for (std::string_view l : block)
            longest = std::max(longest, trim(l).size());

        std::string      paragraph;
        std::string_view previous;
        bool             previousCentered = false;
        for (std::string_view raw : block) {
            std::string_view line     = trim(raw);
            const bool       centered = indentOf(raw) >= CENTERED_INDENT;
            if (commented) {
                line.remove_prefix(2);
                line = trim(line);
                if (line.empty()) {
                    if (!paragraph.empty())
                        paragraphs.push_back(std::move(paragraph));
                    paragraph.clear();
                    previous = {};
                    continue;
                }
            }

            if (!paragraph.empty()) {
                // A line well short of the block's longest was ended on purpose: a heading, an
                // address, the last line before a list. Prose wrapped at a column never is.
                const bool previousEndedEarly = longest >= 40 && previous.size() < longest * 6 / 10;
                const bool forcedBreak        = centered || previousCentered || previousEndedEarly
                    || isListItem(line) || isCopyrightLine(line) || isCopyrightLine(previous)
                    || (!previous.empty() && previous.back() == ':');
                paragraph += forcedBreak ? '\n' : ' ';
            }
            appendCollapsed(paragraph, line);
            previous         = line;
            previousCentered = centered;
        }

        if (!paragraph.empty())
            paragraphs.push_back(std::move(paragraph));
        block.clear();
    };

    for (std::string_view line : splitLines(text)) {
        if (trim(line).empty() || isRule(line)) {
            flush();
            continue;
        }
        block.push_back(line);
    }
    flush();

    return paragraphs;
}

std::vector<std::string> LicenseText::Wrap(std::string_view paragraph, float maxWidth, const MeasureFn &measure) {
    std::vector<std::string> lines;
    const float              spaceWidth = measure(" ");

    for (std::string_view segment : splitLines(paragraph)) {
        std::string line;
        float       lineWidth = 0.0f;

        size_t pos = 0;
        while (pos < segment.size()) {
            size_t end = segment.find(' ', pos);
            if (end == std::string_view::npos)
                end = segment.size();
            std::string_view word = segment.substr(pos, end - pos);
            pos                   = end + 1;
            if (word.empty())
                continue;

            float wordWidth = measure(word);

            // URLs and other unbreakable runs wider than the column are cut where they overflow.
            if (wordWidth > maxWidth) {
                if (!line.empty()) {
                    lines.push_back(std::move(line));
                    line.clear();
                }
                std::string piece;
                float       pieceWidth = 0.0f;
                for (size_t i = 0; i < word.size();) {
                    size_t start = i;
                    decodeUtf8(word, i);
                    std::string_view glyph      = word.substr(start, i - start);
                    float            glyphWidth = measure(glyph);
                    if (!piece.empty() && pieceWidth + glyphWidth > maxWidth) {
                        lines.push_back(std::move(piece));
                        piece.clear();
                        pieceWidth = 0.0f;
                    }
                    piece += glyph;
                    pieceWidth += glyphWidth;
                }
                line      = std::move(piece);
                lineWidth = pieceWidth;
                continue;
            }

            if (line.empty()) {
                line      = word;
                lineWidth = wordWidth;
            } else if (lineWidth + spaceWidth + wordWidth > maxWidth) {
                lines.push_back(std::move(line));
                line      = word;
                lineWidth = wordWidth;
            } else {
                line += ' ';
                line += word;
                lineWidth += spaceWidth + wordWidth;
            }
        }
        lines.push_back(std::move(line));
    }

    return lines;
}

LicenseText::Catalog LicenseText::Build(const std::vector<Source> &sources) {
    struct Prepared {
        std::string              display;
        std::string              body;
        std::string              key;
        std::vector<std::string> copyrights;
    };

    std::vector<Prepared>                                prepared(sources.size());
    std::unordered_map<std::string, std::vector<size_t>> groups;

    for (size_t i = 0; i < sources.size(); ++i) {
        Prepared &p = prepared[i];
        p.display   = ToFontCharset(sources[i].text);

        for (std::string_view line : splitLines(p.display)) {
            if (isCopyrightLine(line)) {
                p.copyrights.emplace_back(stripCommentMarker(line));
                continue;
            }
            if (isTitleLine(line))
                continue;
            p.body.append(line);
            p.body += '\n';
        }

        p.key = groupingKey(p.body);
        if (p.key.size() >= MIN_SHARED_BODY)
            groups[p.key].push_back(i);
    }

    Catalog                                 catalog;
    std::unordered_map<std::string, size_t> textForKey;

    for (size_t i = 0; i < sources.size(); ++i) {
        Prepared &p = prepared[i];

        Component component;
        component.name    = ToFontCharset(sources[i].name);
        component.spdx    = ToFontCharset(sources[i].spdx);
        component.version = ToFontCharset(sources[i].version);
        component.url     = ToFontCharset(sources[i].url);

        auto group = groups.find(p.key);
        if (group != groups.end() && group->second.size() > 1) {
            auto existing = textForKey.find(p.key);
            if (existing == textForKey.end()) {
                existing = textForKey.emplace(p.key, catalog.texts.size()).first;
                catalog.texts.push_back(Reflow(p.body));
            }
            component.textIndex   = existing->second;
            component.copyrights  = std::move(p.copyrights);
            component.sharedCount = group->second.size();
        } else {
            component.textIndex = catalog.texts.size();
            catalog.texts.push_back(Reflow(p.display));
        }

        catalog.components.push_back(std::move(component));
    }

    return catalog;
}
