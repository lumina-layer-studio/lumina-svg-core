// Recorder algorithms relocated from Lumina's pinned LunaSVG patch.
// Parsing compatibility portions derive from LunaSVG (MIT); see
// THIRD_PARTY_NOTICES.md and THIRD_PARTY_LICENSES/LunaSVG.txt.
#include "lumina_svg_core/recorder/numeric.h"
#include "text.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace lumina_svg_core::recorder {

using namespace detail;

double exactRecordingNumber(std::string_view input, double fallback)
{
    stripLeadingAndTrailingSpaces(input);
    const std::string text(input);
    char* parsedEnd = nullptr;
    const double value = std::strtod(text.c_str(), &parsedEnd);
    if(parsedEnd == text.c_str() || !std::isfinite(value))
        return fallback;
    while(*parsedEnd != '\0'
            && std::isspace(static_cast<unsigned char>(*parsedEnd)))
        ++parsedEnd;
    return *parsedEnd == '\0' ? value : fallback;
}

double exactRecordingLength(std::string_view input,
                            double parsedLengthValue,
                            double resolvedFallback)
{
    stripLeadingAndTrailingSpaces(input);
    const std::string text(input);
    char* parsedEnd = nullptr;
    const double value = std::strtod(text.c_str(), &parsedEnd);
    if(parsedEnd == text.c_str() || !std::isfinite(value))
        return resolvedFallback;
    std::string_view units(
        parsedEnd, text.c_str() + text.size() - parsedEnd);
    stripLeadingAndTrailingSpaces(units);
    if(units.empty() || units == "px")
        return value;
    if(units == "pc")
        return value * 16.0;
    if(units == "pt")
        return value * (96.0 / 72.0);
    if(units == "in")
        return value * 96.0;
    if(units == "cm")
        return value * (96.0 / 2.54);
    if(units == "mm")
        return value * (96.0 / 25.4);
    if(units == "%" || units == "em" || units == "ex") {
        const double parsedValue = parsedLengthValue;
        if(parsedValue == 0.0)
            return value == 0.0 ? 0.0 : resolvedFallback;
        return resolvedFallback * value / parsedValue;
    }
    return resolvedFallback;
}

std::vector<std::string_view> recordingLengthTokens(std::string_view input)
{
    stripLeadingAndTrailingSpaces(input);
    if(input == "none")
        return {};
    std::vector<std::string_view> output;
    while(!input.empty()) {
        while(!input.empty()
                && (std::isspace(static_cast<unsigned char>(input.front()))
                    || input.front() == ','))
            input.remove_prefix(1);
        if(input.empty())
            break;
        const std::string tail(input);
        char* parsedEnd = nullptr;
        std::strtod(tail.c_str(), &parsedEnd);
        if(parsedEnd == tail.c_str())
            return {};
        size_t tokenLength = static_cast<size_t>(
            parsedEnd - tail.c_str());
        while(tokenLength < input.size()
                && (std::isalpha(static_cast<unsigned char>(
                        input[tokenLength]))
                    || input[tokenLength] == '%'))
            ++tokenLength;
        output.push_back(input.substr(0, tokenLength));
        input.remove_prefix(tokenLength);
    }
    return output;
}

bool parseExactLength(std::string_view input, double defaultValue,
                      bool required, double& value, ExactLengthUnits& units)
{
    units = ExactLengthUnits::Absolute;
    stripLeadingAndTrailingSpaces(input);
    if(input.empty()) {
        if(required)
            return false;
        value = defaultValue;
        return true;
    }
    if(!parseNumber(input, value) || !std::isfinite(value))
        return false;
    if(input.empty())
        return true;

    if(input == "px")
        return true;
    if(input == "pc") {
        value *= 16.0;
        return std::isfinite(value);
    }
    if(input == "pt") {
        value *= 96.0 / 72.0;
        return std::isfinite(value);
    }
    if(input == "in") {
        value *= 96.0;
        return std::isfinite(value);
    }
    if(input == "cm") {
        value *= 96.0 / 2.54;
        return std::isfinite(value);
    }
    if(input == "mm") {
        value *= 96.0 / 25.4;
        return std::isfinite(value);
    }
    if(input == "em")
        units = ExactLengthUnits::Em;
    else if(input == "ex")
        units = ExactLengthUnits::Ex;
    else if(input == "%")
        units = ExactLengthUnits::Percent;
    else
        return false;
    return true;
}

} // namespace lumina_svg_core::recorder
