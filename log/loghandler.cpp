
#include "loghandler.h"

#include "fonts/fonthandler.h"

void Log::_addLine(const char *line1, const char *line2) {
    //auto font = Fonts::getFont("assets/fonts/APL386.ttf", 20);

    headerWidth = std::max(Fonts::MeasureText("fonts/APL386.ttf", 20, line1), headerWidth);

    longestLineWidth = std::max(Fonts::MeasureText("fonts/APL386.ttf", 20, line2), longestLineWidth);

    lines.emplace_back(line1, line2);
}
//*/