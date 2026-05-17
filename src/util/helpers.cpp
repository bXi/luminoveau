#include "helpers.h"

#include "assets/assethandler.h"

#include "SDL3/SDL.h"

#include <iostream>
#include <sys/stat.h>

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#elif defined(__linux__) && !defined(__ANDROID__)
#include <sys/sysinfo.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(__ANDROID__)
#include <sys/sysinfo.h>
#include <jni.h>
#include <android/log.h>
#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

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

bool Helpers::lineIntersectsRectangle(vf2d lineStart, vf2d lineEnd, rectf rect) {

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

std::vector<std::pair<vf2d, vf2d>> Helpers::getLinesFromRectangle(rectf rect) {
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


bool Helpers::randomChance(const float required) {

    std::default_random_engine generator(time(0));
    std::uniform_real_distribution<double> distribution;

    const float chance = distribution(generator);

    if (chance > (required / 100.0f))
        return true;
    return false;

}

int Helpers::GetRandomValue(int min, int max) {
    static std::mt19937 gen = []() {
        std::random_device rd;
        return std::mt19937(rd());
    }();
    std::uniform_int_distribution<> distr(min, max);
    return distr(gen);
}


const char *Helpers::TextFormat(const char *text, ...) {
#ifndef MAX_TEXTFORMAT_BUFFERS
#define MAX_TEXTFORMAT_BUFFERS 4        // Maximum number of static buffers for text formatting
#endif

    // We create an array of buffers so strings don't expire until MAX_TEXTFORMAT_BUFFERS invocations
    static char buffers[MAX_TEXTFORMAT_BUFFERS][MAX_TEXT_BUFFER_LENGTH] = {{0}};
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

uint64_t Helpers::GetTotalSystemMemory() {
#if defined(_WIN32) || defined(_WIN64)
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) {
        return statex.ullTotalPhys;
    } else {
        return 0; // Failed to get memory status
    }
#elif defined(__linux__) && !defined(__ANDROID__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    } else {
        return 0; // Failed to get memory status
    }
#elif defined(__APPLE__)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t totalMemory = 0;
    size_t length = sizeof(totalMemory);
    if (sysctl(mib, 2, &totalMemory, &length, NULL, 0) == 0) {
        return totalMemory;
    } else {
        return 0; // Failed to get memory status
    }
#elif defined(__ANDROID__)
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return info.totalram * info.mem_unit;
    } else {
        return 0; // Failed to get memory status
    }
#elif defined(__EMSCRIPTEN__)
    return EM_ASM_INT({
        return HEAP8.length;
    });
#else
    return 0; // Unsupported platform
#endif
}

std::string Helpers::Slugify(std::string input) {
std::unordered_map<std::string, std::string> charMap {
		// latin
		{"À", "A"}, {"Á", "A"}, {"Â", "A"}, {"Ã", "A"}, {"Ä", "A"}, {"Å", "A"}, {"Æ", "AE"}, {
		"Ç", "C"}, {"È", "E"}, {"É", "E"}, {"Ê", "E"}, {"Ë", "E"}, {"Ì", "I"}, {"Í", "I"}, {
		"Î", "I"}, {"Ï", "I"}, {"Ð", "D"}, {"Ñ", "N"}, {"Ò", "O"}, {"Ó", "O"}, {"Ô", "O"}, {
		"Õ", "O"}, {"Ö", "O"}, {"Ő", "O"}, {"Ø", "O"}, {"Ù", "U"}, {"Ú", "U"}, {"Û", "U"}, {
		"Ü", "U"}, {"Ű", "U"}, {"Ý", "Y"}, {"Þ", "TH"}, {"ß", "ss"}, {"à", "a"}, {"á", "a"}, {
		"â", "a"}, {"ã", "a"}, {"ä", "a"}, {"å", "a"}, {"æ", "ae"}, {"ç", "c"}, {"è", "e"}, {
		"é", "e"}, {"ê", "e"}, {"ë", "e"}, {"ì", "i"}, {"í", "i"}, {"î", "i"}, {"ï", "i"}, {
		"ð", "d"}, {"ñ", "n"}, {"ò", "o"}, {"ó", "o"}, {"ô", "o"}, {"õ", "o"}, {"ö", "o"}, {
		"ő", "o"}, {"ø", "o"}, {"ù", "u"}, {"ú", "u"}, {"û", "u"}, {"ü", "u"}, {"ű", "u"}, {
		"ý", "y"}, {"þ", "th"}, {"ÿ", "y"}, {"ẞ", "SS"},
		// greek
		{"α", "a"}, {"β", "b"}, {"γ", "g"}, {"δ", "d"}, {"ε", "e"}, {"ζ", "z"}, {"η", "h"}, {"θ", "8"}, {
		"ι", "i"}, {"κ", "k"}, {"λ", "l"}, {"μ", "m"}, {"ν", "n"}, {"ξ", "3"}, {"ο", "o"}, {"π", "p"}, {
		"ρ", "r"}, {"σ", "s"}, {"τ", "t"}, {"υ", "y"}, {"φ", "f"}, {"χ", "x"}, {"ψ", "ps"}, {"ω", "w"}, {
		"ά", "a"}, {"έ", "e"}, {"ί", "i"}, {"ό", "o"}, {"ύ", "y"}, {"ή", "h"}, {"ώ", "w"}, {"ς", "s"}, {
		"ϊ", "i"}, {"ΰ", "y"}, {"ϋ", "y"}, {"ΐ", "i"}, {
		"Α", "A"}, {"Β", "B"}, {"Γ", "G"}, {"Δ", "D"}, {"Ε", "E"}, {"Ζ", "Z"}, {"Η", "H"}, {"Θ", "8"}, {
		"Ι", "I"}, {"Κ", "K"}, {"Λ", "L"}, {"Μ", "M"}, {"Ν", "N"}, {"Ξ", "3"}, {"Ο", "O"}, {"Π", "P"}, {
		"Ρ", "R"}, {"Σ", "S"}, {"Τ", "T"}, {"Υ", "Y"}, {"Φ", "F"}, {"Χ", "X"}, {"Ψ", "PS"}, {"Ω", "W"}, {
		"Ά", "A"}, {"Έ", "E"}, {"Ί", "I"}, {"Ό", "O"}, {"Ύ", "Y"}, {"Ή", "H"}, {"Ώ", "W"}, {"Ϊ", "I"}, {
		"Ϋ", "Y"},
		// turkish
		{"ş", "s"}, {"Ş", "S"}, {"ı", "i"}, {"İ", "I"}, {"ç", "c"}, {"Ç", "C"}, {"ü", "u"}, {"Ü", "U"}, {
		"ö", "o"}, {"Ö", "O"}, {"ğ", "g"}, {"Ğ", "G"},
		// russian
		{"а", "a"}, {"б", "b"}, {"в", "v"}, {"г", "g"}, {"д", "d"}, {"е", "e"}, {"ё", "yo"}, {"ж", "zh"}, {
		"з", "z"}, {"и", "i"}, {"й", "j"}, {"к", "k"}, {"л", "l"}, {"м", "m"}, {"н", "n"}, {"о", "o"}, {
		"п", "p"}, {"р", "r"}, {"с", "s"}, {"т", "t"}, {"у", "u"}, {"ф", "f"}, {"х", "h"}, {"ц", "c"}, {
		"ч", "ch"}, {"ш", "sh"}, {"щ", "sh"}, {"ъ", "u"}, {"ы", "y"}, {"ь", ""}, {"э", "e"}, {"ю", "yu"}, {
		"я", "ya"}, {
		"А", "A"}, {"Б", "B"}, {"В", "V"}, {"Г", "G"}, {"Д", "D"}, {"Е", "E"}, {"Ё", "Yo"}, {"Ж", "Zh"}, {
		"З", "Z"}, {"И", "I"}, {"Й", "J"}, {"К", "K"}, {"Л", "L"}, {"М", "M"}, {"Н", "N"}, {"О", "O"}, {
		"П", "P"}, {"Р", "R"}, {"С", "S"}, {"Т", "T"}, {"У", "U"}, {"Ф", "F"}, {"Х", "H"}, {"Ц", "C"}, {
		"Ч", "Ch"}, {"Ш", "Sh"}, {"Щ", "Sh"}, {"Ъ", "U"}, {"Ы", "Y"}, {"Ь", ""}, {"Э", "E"}, {"Ю", "Yu"}, {
		"Я", "Ya"},
		// ukranian
		{"Є", "Ye"}, {"І", "I"}, {"Ї", "Yi"}, {"Ґ", "G"}, {"є", "ye"}, {"і", "i"}, {"ї", "yi"}, {"ґ", "g"},
		// czech
		{"č", "c"}, {"ď", "d"}, {"ě", "e"}, {"ň", "n"}, {"ř", "r"}, {"š", "s"}, {"ť", "t"}, {"ů", "u"},
		{"ž", "z"}, {"Č", "C"}, {"Ď", "D"}, {"Ě", "E"}, {"Ň", "N"}, {"Ř", "R"}, {"Š", "S"}, {"Ť", "T"},
		{"Ů", "U"}, {"Ž", "Z"},
		// polish
		{"ą", "a"}, {"ć", "c"}, {"ę", "e"}, {"ł", "l"}, {"ń", "n"}, {"ó", "o"}, {"ś", "s"}, {"ź", "z"},
		{"ż", "z"}, {"Ą", "A"}, {"Ć", "C"}, {"Ę", "e"}, {"Ł", "L"}, {"Ń", "N"}, {"Ś", "S"},
		{"Ź", "Z"}, {"Ż", "Z"},
		// latvian
		{"ā", "a"}, {"č", "c"}, {"ē", "e"}, {"ģ", "g"}, {"ī", "i"}, {"ķ", "k"}, {"ļ", "l"}, {"ņ", "n"},
		{"š", "s"}, {"ū", "u"}, {"ž", "z"}, {"Ā", "A"}, {"Č", "C"}, {"Ē", "E"}, {"Ģ", "G"}, {"Ī", "i"},
		{"Ķ", "k"}, {"Ļ", "L"}, {"Ņ", "N"}, {"Š", "S"}, {"Ū", "u"}, {"Ž", "Z"},
		// currency
		{"€", "euro"}, {"₢", "cruzeiro"}, {"₣", "french franc"}, {"£", "pound"},
		{"₤", "lira"}, {"₥", "mill"}, {"₦", "naira"}, {"₧", "peseta"}, {"₨", "rupee"},
		{"₩", "won"}, {"₪", "new shequel"}, {"₫", "dong"}, {"₭", "kip"}, {"₮", "tugrik"},
		{"₯", "drachma"}, {"₰", "penny"}, {"₱", "peso"}, {"₲", "guarani"}, {"₳", "austral"},
		{"₴", "hryvnia"}, {"₵", "cedi"}, {"¢", "cent"}, {"¥", "yen"}, {"元", "yuan"},
		{"円", "yen"}, {"﷼", "rial"}, {"₠", "ecu"}, {"¤", "currency"}, {"฿", "baht"}, {"$", "dollar"},
		// symbols
		{"©", "(c)"}, {"œ", "oe"}, {"Œ", "OE"}, {"∑", "sum"}, {"®", "(r)"}, {"†", "+"},
		{"“", "\""}, {"∂", "d"}, {"ƒ", "f"}, {"™", "tm"},
		{"℠", "sm"}, {"…", "..."}, {"˚", "o"}, {"º", "o"}, {"ª", "a"}, {"•", "*"},
		{"∆", "delta"}, {"∞", "infinity"}, {"♥", "love"}, {"&", "and"}, {"|", "or"},
		{"<", "less"}, {">", "greater"}
	};

	// loop every character in charMap
	for(auto kv : charMap)
	{
		// check if key is in string
		if(input.find(kv.first) != std::string::npos)
		{
			// replace key with value
			input.replace(input.find(kv.first), kv.first.length(), kv.second);
		}
	}

	std::regex e1("[^\\w\\s$*_+~.()\'\"-]");
	input = std::regex_replace(input, e1, "");

	std::regex e2("^\\s+|\\s+$");
	input = std::regex_replace(input, e2, "");

	std::regex e3("[-\\s]+");
	input = std::regex_replace(input, e3, "-");

	std::regex e4("#-$");
	input = std::regex_replace(input, e4, "");

	return input;
}

time_t Helpers::GetFileModificationTime(const std::string& filepath) {
    struct stat fileInfo;
    if (stat(filepath.c_str(), &fileInfo) == 0) {
        return fileInfo.st_mtime;
    }
    return 0;  // Return 0 if file doesn't exist or error
}

