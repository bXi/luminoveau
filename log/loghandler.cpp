
#include "loghandler.h"

#include "fonts/fonthandler.h"

void Log::_addLine(const char *line1, const char *line2) {
    //auto font = Fonts::getFont("assets/fonts/APL386.ttf", 20);

    //headerWidth = std::max(static_cast<int>(MeasureTextEx(font, line1, 20, 1.0f).x), headerWidth);
    //longestLineWidth = std::max(static_cast<int>(MeasureTextEx(font, line2, 20, 1.0f).x), longestLineWidth);

    lines.emplace_back(line1, line2);
}
//*/