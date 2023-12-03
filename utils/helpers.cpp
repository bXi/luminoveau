#include "helpers.h"

#include "state/state.h"

int Helpers::clamp(const int input, const int min, const int max) {
    const int a = (input < min) ? min : input;
    return (a > max ? max : a);
}

float Helpers::mapValues(const float x, const float in_min, const float in_max, const float out_min, const float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float Helpers::getDifficultyModifier(float mod) {
    return 1.0f + ((mod / 10.0f) * (mod / 10.0f) / 1.9f);
}

bool Helpers::lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, Rectangle rect) {

    auto doIntersect = [](vf2d p1, vf2d q1, vf2d p2, vf2d q2) -> bool {
        auto orientation = [](vf2d p, vf2d q, vf2d r) -> int {
            float val = (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);
            if (val == 0) return 0; // Collinear
            return (val > 0) ? 1 : 2; // Clockwise or Counterclockwise
        };

        auto onSegment = [](vf2d p, vf2d q, vf2d r) -> bool {
            return q.x <= std::max(p.x, r.x) && q.x >= std::min(p.x, r.x) &&
                   q.y <= std::max(p.y, r.y) && q.y >= std::min(p.y, r.y);
        };

        // Find the 4 orientations required for general and
        // special cases
        int o1 = orientation(p1, q1, p2);
        int o2 = orientation(p1, q1, q2);
        int o3 = orientation(p2, q2, p1);
        int o4 = orientation(p2, q2, q1);

        // General case
        if (o1 != o2 && o3 != o4)
            return true;

        // Special Cases

        // p1, q1, and p2 are collinear, and p2 lies on segment p1q1
        if (o1 == 0 && onSegment(p1, p2, q1)) return true;

        // p1, q1, and q2 are collinear, and q2 lies on segment p1q1
        if (o2 == 0 && onSegment(p1, q2, q1)) return true;

        // p2, q2, and p1 are collinear, and p1 lies on segment p2q2
        if (o3 == 0 && onSegment(p2, p1, q2)) return true;

        // p2, q2, and q1 are collinear, and q1 lies on segment p2q2
        if (o4 == 0 && onSegment(p2, q1, q2)) return true;

        return false; // Doesn't fall in any of the above cases
    };


    auto lines = Helpers::getLinesFromRectangle(rect);

    for (auto &line: lines) {
        if (doIntersect(line.first, line.second, lineStart, lineEnd)) {
            return true;
        }
    }


    return false;
}

std::vector<std::pair<vf2d, vf2d>> Helpers::getLinesFromRectangle(Rectangle rect) {
    float x = rect.x;
    float y = rect.y;
    vf2d topLeft = {x, y};
    vf2d topRight = {x + rect.width, y};
    vf2d bottomLeft = {x, y + rect.height};
    vf2d bottomRight = {x + rect.width, y + rect.height};

    const std::pair topLine = {topLeft, topRight};
    const std::pair rightLine = {topRight, bottomRight};
    const std::pair bottomLine = {bottomRight, bottomLeft};
    const std::pair leftLine = {bottomLeft, topLeft};

    return std::vector{
            topLine,
            rightLine,
            bottomLine,
            leftLine
    };
}


void Helpers::DrawMainMenu() {
    if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("Exit")) {
				State::SetState("quit");
			}

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Options")) {

			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

}

bool Helpers::randomChance(const float required) {

    std::default_random_engine generator(time(0));
    std::uniform_real_distribution<double> distribution;

    const float chance = distribution(generator);

    if (chance > (required / 100.0f))
        return true;
    return false;

}

int Helpers::GetRandomValue(int min, int max) {

    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator
    std::uniform_int_distribution<> distr(min, max); // define the range

    return distr(gen);

}


const char *Helpers::TextFormat(const char *text, ...) {
#ifndef MAX_TEXTFORMAT_BUFFERS
#define MAX_TEXTFORMAT_BUFFERS 4        // Maximum number of static buffers for text formatting
#endif

    // We create an array of buffers so strings don't expire until MAX_TEXTFORMAT_BUFFERS invocations
    static char buffers[MAX_TEXTFORMAT_BUFFERS][MAX_TEXT_BUFFER_LENGTH] = {0};
    static int index = 0;

    char *currentBuffer = buffers[index];
    memset(currentBuffer, 0, MAX_TEXT_BUFFER_LENGTH);   // Clear buffer before using

    va_list args;
    va_start(args, text);
    vsnprintf(currentBuffer, MAX_TEXT_BUFFER_LENGTH, text, args);
    va_end(args);

    index += 1;     // Move to next buffer for next function call
    if (index >= MAX_TEXTFORMAT_BUFFERS) index = 0;

    return currentBuffer;
}


