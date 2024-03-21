#include "loghandler.h"

#include "text/texthandler.h"

void Log::_addLine(const char *line1, const char *line2) {
    auto font = Text::GetFont("assets/fonts/APL386.ttf", 20);

    headerWidth = std::max(Text::MeasureText(font, line1), headerWidth);

    longestLineWidth = std::max(Text::MeasureText(font, line2), longestLineWidth);

    lines.emplace_back(line1, line2);
}
