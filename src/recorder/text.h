// SVG lexical parsing adapted from LunaSVG (MIT).
// Copyright Samuel Ugochukwu and LunaSVG contributors.
// See THIRD_PARTY_LICENSES/LunaSVG.txt.
#ifndef LUMINA_SVG_CORE_RECORDER_TEXT_H
#define LUMINA_SVG_CORE_RECORDER_TEXT_H

#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

namespace lumina_svg_core::recorder::detail {

constexpr bool IS_NUM(int cc) { return cc >= '0' && cc <= '9'; }
constexpr bool IS_ALPHA(int cc) { return (cc >= 'a' && cc <= 'z') || (cc >= 'A' && cc <= 'Z'); }
constexpr bool IS_WS(int cc) { return cc == ' ' || cc == '\t' || cc == '\n' || cc == '\r'; }

constexpr void stripLeadingSpaces(std::string_view& input)
{
    while(!input.empty() && IS_WS(input.front())) {
        input.remove_prefix(1);
    }
}

constexpr void stripTrailingSpaces(std::string_view& input)
{
    while(!input.empty() && IS_WS(input.back())) {
        input.remove_suffix(1);
    }
}

constexpr void stripLeadingAndTrailingSpaces(std::string_view& input)
{
    stripLeadingSpaces(input);
    stripTrailingSpaces(input);
}

constexpr bool skipOptionalSpaces(std::string_view& input)
{
    while(!input.empty() && IS_WS(input.front()))
        input.remove_prefix(1);
    return !input.empty();
}

constexpr bool skipOptionalSpacesOrDelimiter(std::string_view& input, char delimiter)
{
    if(!input.empty() && !IS_WS(input.front()) && delimiter != input.front())
        return false;
    if(skipOptionalSpaces(input)) {
        if(delimiter == input.front()) {
            input.remove_prefix(1);
            skipOptionalSpaces(input);
        }
    }

    return !input.empty();
}

constexpr bool skipOptionalSpacesOrComma(std::string_view& input)
{
    return skipOptionalSpacesOrDelimiter(input, ',');
}

constexpr bool skipDelimiter(std::string_view& input, char delimiter)
{
    if(!input.empty() && input.front() == delimiter) {
        input.remove_prefix(1);
        return true;
    }

    return false;
}

constexpr bool skipString(std::string_view& input, std::string_view value)
{
    if(input.size() >= value.size() && value == input.substr(0, value.size())) {
        input.remove_prefix(value.size());
        return true;
    }

    return false;
}

inline size_t pathParameterCount(char command)
{
    switch(static_cast<char>(std::tolower(
        static_cast<unsigned char>(command)))) {
    case 'm': case 'l': case 't': return 2;
    case 'h': case 'v': return 1;
    case 'c': return 6;
    case 's': case 'q': return 4;
    case 'a': return 7;
    default: return 0;
    }
}

template<typename T>
inline bool parseNumber(std::string_view& input, T& number)
{
    constexpr T maxValue = std::numeric_limits<T>::max();
    T integer = 0;
    T fraction = 0;
    int exponent = 0;
    int sign = 1;
    int expsign = 1;

    if(!input.empty() && input.front() == '+') {
        input.remove_prefix(1);
    } else if(!input.empty() && input.front() == '-') {
        input.remove_prefix(1);
        sign = -1;
    }

    if(input.empty() || (!IS_NUM(input.front()) && input.front() != '.'))
        return false;
    if(IS_NUM(input.front())) {
        do {
            integer = static_cast<T>(10) * integer + (input.front() - '0');
            input.remove_prefix(1);
        } while(!input.empty() && IS_NUM(input.front()));
    }

    if(!input.empty() && input.front() == '.') {
        input.remove_prefix(1);
        if(input.empty() || !IS_NUM(input.front()))
            return false;
        T divisor = static_cast<T>(1);
        do {
            fraction = static_cast<T>(10) * fraction + (input.front() - '0');
            divisor *= static_cast<T>(10);
            input.remove_prefix(1);
        } while(!input.empty() && IS_NUM(input.front()));
        fraction /= divisor;
    }

    if(input.size() > 1 && (input[0] == 'e' || input[0] == 'E')
        && (input[1] != 'x' && input[1] != 'm'))
    {
        input.remove_prefix(1);
        if(!input.empty() && input.front() == '+')
            input.remove_prefix(1);
        else if(!input.empty() && input.front() == '-') {
            input.remove_prefix(1);
            expsign = -1;
        }

        if(input.empty() || !IS_NUM(input.front()))
            return false;
        do {
            exponent = 10 * exponent + (input.front() - '0');
            input.remove_prefix(1);
        } while(!input.empty() && IS_NUM(input.front()));
    }

    number = sign * (integer + fraction);
    if(exponent)
        number *= static_cast<T>(std::pow(10.0, expsign * exponent));
    return number >= -maxValue && number <= maxValue;
}

} // namespace lumina_svg_core::recorder::detail

#endif
