// Recorder algorithms relocated from Lumina's pinned LunaSVG patch.
// Parsing compatibility portions derive from LunaSVG (MIT); see
// THIRD_PARTY_NOTICES.md and THIRD_PARTY_LICENSES/LunaSVG.txt.
#include "lumina_svg_core/recorder/alpha.h"
#include "text.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace lumina_svg_core::recorder {

using namespace detail;

static float parseNumberOrPercentage(std::string_view input, bool allowPercentage, float defaultValue)
{
    float value;
    if(!parseNumber(input, value))
        return defaultValue;
    if(allowPercentage) {
        if(skipDelimiter(input, '%'))
            value /= 100.f;
        value = std::clamp(value, 0.f, 1.f);
    }

    if(!input.empty())
        return defaultValue;
    return value;
}

double parseRecordingNumberOrPercentage(
    std::string_view input, bool allowPercentage, double defaultValue)
{
    stripLeadingAndTrailingSpaces(input);
    bool percentage = false;
    if(allowPercentage && !input.empty() && input.back() == '%') {
        percentage = true;
        input.remove_suffix(1);
        stripLeadingAndTrailingSpaces(input);
    }
    if(input.empty())
        return defaultValue;
    std::string source(input);
    char* parsedEnd = nullptr;
    const double parsed = std::strtod(source.c_str(), &parsedEnd);
    if(parsedEnd != source.c_str() + source.size()
            || !std::isfinite(parsed))
        return defaultValue;
    double value = percentage ? parsed / 100.0 : parsed;
    return std::clamp(value, 0.0, 1.0);
}

static uint8_t recordingHexDigit(char value)
{
    if(value >= '0' && value <= '9')
        return static_cast<uint8_t>(value - '0');
    if(value >= 'a' && value <= 'f')
        return static_cast<uint8_t>(10 + value - 'a');
    return static_cast<uint8_t>(10 + value - 'A');
}

bool recordingAsciiEqualsIgnoreCase(
    std::string_view left, std::string_view right)
{
    if(left.size() != right.size())
        return false;
    for(size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](char value) {
            return value >= 'A' && value <= 'Z'
                ? static_cast<char>(value + ('a' - 'A')) : value;
        };
        if(lower(left[index]) != lower(right[index]))
            return false;
    }
    return true;
}

double recordingColorAlpha(
    std::string_view input, bool validColor, float parsedAlpha)
{
    stripLeadingAndTrailingSpaces(input);
    if(!validColor)
        return 1.0;

    if(!input.empty() && input.front() == '#') {
        const auto digits = input.substr(1);
        if(digits.size() == 4)
            return recordingHexDigit(digits[3]) / 15.0;
        if(digits.size() == 8) {
            const uint8_t alpha = static_cast<uint8_t>(
                recordingHexDigit(digits[6]) * 16
                + recordingHexDigit(digits[7]));
            return alpha / 255.0;
        }
        return 1.0;
    }

    const auto open = input.find('(');
    const auto close = input.rfind(')');
    if(open != std::string_view::npos && close == input.size() - 1) {
        const auto functionName = input.substr(0, open);
        if(recordingAsciiEqualsIgnoreCase(functionName, "rgb")
                || recordingAsciiEqualsIgnoreCase(functionName, "rgba")
                || recordingAsciiEqualsIgnoreCase(functionName, "hsl")
                || recordingAsciiEqualsIgnoreCase(functionName, "hsla")) {
            const auto arguments = input.substr(
                open + 1, close - open - 1);
            const auto commaCount = static_cast<size_t>(std::count(
                arguments.begin(), arguments.end(), ','));
            if(commaCount == 3) {
                const auto alphaStart = arguments.rfind(',');
                return parseRecordingNumberOrPercentage(
                    arguments.substr(alphaStart + 1), true,
                    static_cast<double>(parsedAlpha));
            }
        }
    }
    return std::clamp(static_cast<double>(parsedAlpha), 0.0, 1.0);
}

bool isSourcePartialAlpha(std::string_view value)
{
    stripLeadingAndTrailingSpaces(value);
    bool percentage = false;
    if(!value.empty() && value.back() == '%') {
        percentage = true;
        value.remove_suffix(1);
    }
    bool negative = false;
    if(!value.empty() && (value.front() == '+' || value.front() == '-')) {
        negative = value.front() == '-';
        value.remove_prefix(1);
    }
    std::string digits;
    size_t fractionDigits = 0;
    bool foundDigit = false;
    while(!value.empty() && value.front() >= '0' && value.front() <= '9') {
        digits.push_back(value.front());
        value.remove_prefix(1);
        foundDigit = true;
    }
    if(!value.empty() && value.front() == '.') {
        value.remove_prefix(1);
        while(!value.empty() && value.front() >= '0' && value.front() <= '9') {
            digits.push_back(value.front());
            value.remove_prefix(1);
            ++fractionDigits;
            foundDigit = true;
        }
    }
    int exponentSign = 1;
    int64_t exponent = 0;
    if(!value.empty() && (value.front() == 'e' || value.front() == 'E')) {
        value.remove_prefix(1);
        if(!value.empty() && (value.front() == '+' || value.front() == '-')) {
            exponentSign = value.front() == '-' ? -1 : 1;
            value.remove_prefix(1);
        }
        bool foundExponentDigit = false;
        while(!value.empty() && value.front() >= '0' && value.front() <= '9') {
            exponent = std::min<int64_t>(
                1000000000,
                exponent * 10 + (value.front() - '0'));
            value.remove_prefix(1);
            foundExponentDigit = true;
        }
        if(!foundExponentDigit)
            return false;
    }
    if(!foundDigit || !value.empty() || negative)
        return false;
    const auto firstNonZero = digits.find_first_not_of('0');
    if(firstNonZero == std::string::npos)
        return false;
    digits.erase(0, firstNonZero);
    int64_t scale = exponentSign * exponent
        - static_cast<int64_t>(fractionDigits);
    while(digits.size() > 1 && digits.back() == '0') {
        digits.pop_back();
        ++scale;
    }
    const std::string_view threshold = percentage ? "100" : "1";
    const int64_t decimalDigits = static_cast<int64_t>(digits.size()) + scale;
    if(decimalDigits != static_cast<int64_t>(threshold.size()))
        return decimalDigits < static_cast<int64_t>(threshold.size());
    const size_t comparisonDigits = std::max(digits.size(), threshold.size());
    for(size_t index = 0; index < comparisonDigits; ++index) {
        const char digit = index < digits.size() ? digits[index] : '0';
        const char thresholdDigit = index < threshold.size()
            ? threshold[index] : '0';
        if(digit != thresholdDigit)
            return digit < thresholdDigit;
    }
    return false;
}

AlphaSemantics numberAlphaSemantics(std::string_view input)
{
    if(isSourcePartialAlpha(input))
        return {false, true};
    const float parsed = parseNumberOrPercentage(input, true, 1.f);
    return {parsed <= 0.f, false};
}

std::optional<AlphaSemantics> sourceColorAlphaSemantics(
    std::string_view input, AlphaSemantics currentColor)
{
    stripLeadingAndTrailingSpaces(input);
    if(recordingAsciiEqualsIgnoreCase(input, "currentColor"))
        return currentColor;
    const auto open = input.find('(');
    const auto close = input.rfind(')');
    const auto comma = input.rfind(',');
    if(open != std::string_view::npos && close == input.size() - 1
            && comma != std::string_view::npos && comma > open) {
        const auto functionName = input.substr(0, open);
        if(recordingAsciiEqualsIgnoreCase(functionName, "rgba")
                || recordingAsciiEqualsIgnoreCase(functionName, "hsla"))
            return numberAlphaSemantics(
                input.substr(comma + 1, close - comma - 1));
    }
    return std::nullopt;
}

} // namespace lumina_svg_core::recorder
