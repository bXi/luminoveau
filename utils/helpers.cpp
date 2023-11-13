#include "helpers.h"


int Helpers::clamp(const int input, const int min, const int max)
{
	const int a = (input < min) ? min : input;
	return (a > max ? max : a);
}

float Helpers::mapValues(const float x, const float in_min, const float in_max, const float out_min, const float out_max)
{
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

float Helpers::getDifficultyModifier(float mod)
{
	return 1.0f + ((mod /  10.0f)* (mod / 10.0f) / 1.9f);
}

bool Helpers::lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, Rectangle rect)
{

	auto lines = Helpers::getLinesFromRectangle(rect);

	for (auto& line : lines)
	{
//		if (CheckCollisionLines(line.first, line.second, lineStart, lineEnd, nullptr))
//		{
//			return true;
//		}
	}


	return false;
}

std::vector<std::pair<vf2d, vf2d>> Helpers::getLinesFromRectangle(Rectangle rect)
{
	float x = rect.x;
	float y = rect.y;
	vf2d topLeft ={ x, y };
	vf2d topRight ={ x + rect.width, y };
	vf2d bottomLeft ={ x, y + rect.height };
	vf2d bottomRight ={ x + rect.width, y + rect.height };

	const std::pair topLine = { topLeft, topRight };
	const std::pair rightLine = { topRight, bottomRight };
	const std::pair bottomLine = { bottomRight, bottomLeft };
	const std::pair leftLine = {  bottomLeft, topLeft };

	return std::vector {
		topLine,
		rightLine,
		bottomLine,
		leftLine
	};
}




void Helpers::DrawMainMenu()
{


}

bool Helpers::randomChance(const float required) 	{

    std::default_random_engine generator(time(0));
    std::uniform_real_distribution<double> distribution;

    const float chance = distribution(generator);

    if (chance > (required / 100.0f))
        return true;
    return false;

}

int Helpers::GetRandomValue(int min, int max) 	{

    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator
    std::uniform_int_distribution<> distr(min, max); // define the range

    return distr(gen);

}


const char *Helpers::TextFormat(const char *text, ...)
{
#ifndef MAX_TEXTFORMAT_BUFFERS
    #define MAX_TEXTFORMAT_BUFFERS 4        // Maximum number of static buffers for text formatting
#endif

    // We create an array of buffers so strings don't expire until MAX_TEXTFORMAT_BUFFERS invocations
    static char buffers[MAX_TEXTFORMAT_BUFFERS][MAX_TEXT_BUFFER_LENGTH] = { 0 };
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


