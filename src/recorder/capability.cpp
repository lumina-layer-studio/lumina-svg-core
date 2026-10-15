// Recorder algorithms relocated from Lumina's pinned LunaSVG patch.
// Parsing compatibility portions derive from LunaSVG (MIT); see
// THIRD_PARTY_NOTICES.md and THIRD_PARTY_LICENSES/LunaSVG.txt.
#include "lumina_svg_core/recorder/capability.h"
#include "text.h"
#include "lumina_svg_core/recorder/alpha.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace lumina_svg_core::recorder {

using namespace detail;

bool hasPrefix(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size()
        && value.substr(0, prefix.size()) == prefix;
}

static std::string_view::size_type findUrlFunction(std::string_view value)
{
    for(size_t index = 0; index + 4 <= value.size(); ++index) {
        if((value[index] == 'u' || value[index] == 'U')
                && (value[index + 1] == 'r' || value[index + 1] == 'R')
                && (value[index + 2] == 'l' || value[index + 2] == 'L')
                && value[index + 3] == '(')
            return index;
    }
    return std::string_view::npos;
}

static std::string_view trimmedValue(std::string_view value)
{
    auto first = value.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos)
        return {};
    auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

static bool parseNumberValue(std::string_view value, bool allowPercentage,
                             float& parsed)
{
    value = trimmedValue(value);
    if(value.empty() || !parseNumber(value, parsed) || !std::isfinite(parsed))
        return false;
    if(allowPercentage && skipDelimiter(value, '%'))
        parsed /= 100.f;
    return value.empty();
}

static bool isValidLengthValue(std::string_view value, bool allowNegative)
{
    stripLeadingAndTrailingSpaces(value);
    float number = 0.f;
    if(!parseNumber(value, number) || (!allowNegative && number < 0.f))
        return false;
    return value.empty() || value == "%" || value == "px" || value == "pc"
        || value == "pt" || value == "in" || value == "cm" || value == "mm"
        || value == "em" || value == "ex";
}

static bool isValidDashArray(std::string_view value)
{
    value = trimmedValue(value);
    if(value == "none")
        return true;
    bool foundLength = false;
    while(!value.empty()) {
        size_t count = 0;
        while(count < value.size() && value[count] != ',' && !IS_WS(value[count]))
            ++count;
        if(count == 0 || !isValidLengthValue(
                value.substr(0, count), false))
            return false;
        foundLength = true;
        value.remove_prefix(count);
        if(value.empty())
            break;
        if(!skipOptionalSpacesOrComma(value))
            return false;
    }
    return foundLength;
}

static bool containsUnstableFloatNumber(std::string_view value)
{
    while(!value.empty()) {
        const char first = value.front();
        if(!IS_NUM(first) && first != '+' && first != '-' && first != '.') {
            value.remove_prefix(1);
            continue;
        }
        auto remaining = value;
        double number = 0.0;
        if(!parseNumber(remaining, number)) {
            value.remove_prefix(1);
            continue;
        }
        const size_t consumed = value.size() - remaining.size();
        if(consumed == 0) {
            value.remove_prefix(1);
            continue;
        }
        if(std::isfinite(number) && number != 0.0) {
            const double magnitude = std::abs(number);
            if(magnitude < std::numeric_limits<float>::min()
                    || magnitude > std::numeric_limits<float>::max())
                return true;
        }
        value.remove_prefix(consumed);
    }
    return false;
}

static bool isFloatGeometryAttribute(std::string_view name)
{
    return name == "d" || name == "points" || name == "transform"
        || name == "viewBox" || name == "x" || name == "y"
        || name == "x1" || name == "y1" || name == "x2" || name == "y2"
        || name == "cx" || name == "cy" || name == "r"
        || name == "rx" || name == "ry" || name == "width"
        || name == "height" || name == "dx" || name == "dy"
        || name == "stroke-width" || name == "stroke-dasharray"
        || name == "stroke-dashoffset" || name == "stroke-miterlimit";
}

static bool isValidLocalUrl(std::string_view value)
{
    value = trimmedValue(value);
    if(value == "none")
        return true;
    if(!skipString(value, "url") || !skipOptionalSpaces(value)
            || !skipDelimiter(value, '(') || !skipOptionalSpaces(value))
        return false;
    char quote = 0;
    if(!value.empty() && (value.front() == '\'' || value.front() == '"')) {
        quote = value.front();
        value.remove_prefix(1);
        skipOptionalSpaces(value);
    }
    if(!skipDelimiter(value, '#'))
        return false;
    size_t identifierLength = 0;
    while(identifierLength < value.size()
            && value[identifierLength] != ')'
            && (quote == 0 || value[identifierLength] != quote))
        ++identifierLength;
    if(identifierLength == 0)
        return false;
    value.remove_prefix(identifierLength);
    skipOptionalSpaces(value);
    if(quote != 0 && !skipDelimiter(value, quote))
        return false;
    skipOptionalSpaces(value);
    if(!skipDelimiter(value, ')'))
        return false;
    skipOptionalSpaces(value);
    return value.empty();
}

static bool matchesAny(std::string_view value, std::string_view first,
                       std::string_view second = {}, std::string_view third = {})
{
    return value == first || (!second.empty() && value == second)
        || (!third.empty() && value == third);
}

bool isUnimplementedGeometryCssProperty(std::string_view name)
{
    return name == "transform" || name == "transform-origin"
        || name == "transform-box" || name == "translate"
        || name == "rotate" || name == "scale" || name == "x"
        || name == "y" || name == "cx" || name == "cy" || name == "r"
        || name == "rx" || name == "ry" || name == "width"
        || name == "height" || name == "d" || name == "points"
        || name == "clip" || name == "marker"
        || name == "stroke-alignment" || hasPrefix(name, "offset-")
        || hasPrefix(name, "motion-");
}

bool isDeferredCssCapabilityProperty(std::string_view name, bool knownCssProperty)
{
    return knownCssProperty
        || isUnimplementedGeometryCssProperty(name)
        || name == "all"
        || name == "filter" || name == "backdrop-filter"
        || name == "paint-order" || name == "mask"
        || name == "mix-blend-mode" || name == "isolation"
        || name == "color-interpolation";
}

AttributeCapability classifyAttributeCapability(
    std::string_view name, std::string_view value, bool known,
    bool isStyleElement, bool isImageElement, bool cssDeclaration)
{
    bool needsColorValidation = false;
    if(isStyleElement) {
        const auto styleValue = trimmedValue(value);
        if(name == "media" && !styleValue.empty() && styleValue != "all")
            return {"unsupported_css_media"};
        if(name == "type" && !styleValue.empty()
                && styleValue != "text/css")
            return {"unsupported_css_type"};
        if(name == "scoped")
            return {"unsupported_css_scope"};
    }
    if(name == "requiredExtensions" || name == "requiredFeatures"
            || name == "systemLanguage")
        return {"unsupported_conditional_processing"};
    if(name == "pathLength")
        return {"unsupported_path_length"};
    if(isImageElement
            && (name == "href" || name == "xlink:href"))
        return {"unsupported_image"};
    if(name == "xml:base")
        return {"unsupported_external_resource"};
    if(cssDeclaration && !known
            && (isUnimplementedGeometryCssProperty(name)
                || name == "all"))
        return {"unsupported_css_property"};
    const auto normalizedValue = trimmedValue(value);
    if(known && normalizedValue == "inherit")
        return {};
    auto firstValueCharacter = value.find_first_not_of(" \t\r\n");
    if((name == "href" || name == "xlink:href")
            && firstValueCharacter != std::string_view::npos
            && value[firstValueCharacter] != '#')
        return {"unsupported_external_resource"};
    auto urlPosition = findUrlFunction(value);
    if(urlPosition != std::string_view::npos) {
        auto urlValue = value.find_first_not_of(" \t\r\n'\"", urlPosition + 4);
        if(urlValue != std::string_view::npos && value[urlValue] != '#')
            return {"unsupported_external_resource"};
    }
    if(isFloatGeometryAttribute(name)
            && containsUnstableFloatNumber(value))
        return {"unsupported_input_precision"};
    const bool isPaintColor = name == "fill" || name == "stroke"
        || name == "color" || name == "stop-color"
        || name == "flood-color" || name == "lighting-color";
    if(isPaintColor) {
        if(urlPosition != std::string_view::npos)
            return {"unsupported_paint_server"};
        if(normalizedValue.empty())
            return {"unsupported_color_syntax"};
        const auto colorValue = normalizedValue;
        if((name == "fill" || name == "stroke") && colorValue == "none")
            return {};
        if(colorValue == "currentColor")
            return {};
        if(colorValue.size()
                > static_cast<size_t>(std::numeric_limits<int>::max()))
            return {"unsupported_color_syntax"};
        needsColorValidation = true;
    }
    if(name == "vector-effect")
        return {matchesAny(normalizedValue, "none", "non-scaling-stroke") ? std::string{} : "unsupported_vector_effect"};
    if(name == "paint-order")
        return {normalizedValue == "normal"
            ? std::string{} : "unsupported_paint_order"};
    if(name == "filter" || name == "backdrop-filter")
        return {normalizedValue == "none"
            ? std::string{} : "unsupported_filter"};
    if(name == "mask")
        return {normalizedValue == "none"
            ? std::string{} : "unsupported_mask"};
    if(name == "mix-blend-mode")
        return {normalizedValue == "normal"
            ? std::string{} : "unsupported_blend_mode"};
    if(name == "isolation")
        return {normalizedValue == "auto"
            ? std::string{} : "unsupported_blend_mode"};
    if(name == "color-interpolation")
        return {normalizedValue == "sRGB"
            ? std::string{} : "unsupported_color_interpolation"};
    if(hasPrefix(name, "on"))
        return {"unsupported_script"};
    if(name == "clip-path" && !isValidLocalUrl(value))
        return {"unsupported_clip_path"};
    if(name == "fill-rule" || name == "clip-rule") {
        if(!matchesAny(normalizedValue, "nonzero", "evenodd"))
            return {"unsupported_property_value"};
    }
    if(name == "display" && !matchesAny(normalizedValue, "inline", "none"))
        return {"unsupported_property_value"};
    if(name == "visibility"
            && !matchesAny(normalizedValue, "visible", "hidden", "collapse"))
        return {"unsupported_property_value"};
    if(name == "overflow"
            && !matchesAny(normalizedValue, "visible", "hidden"))
        return {"unsupported_property_value"};
    if(name == "stroke-linecap"
            && !matchesAny(normalizedValue, "butt", "round", "square"))
        return {"unsupported_property_value"};
    if(name == "stroke-linejoin"
            && !matchesAny(normalizedValue, "miter", "round", "bevel"))
        return {"unsupported_property_value"};
    if(name == "pointer-events"
            && normalizedValue != "none" && normalizedValue != "auto"
            && normalizedValue != "stroke" && normalizedValue != "fill"
            && normalizedValue != "painted" && normalizedValue != "visible"
            && normalizedValue != "visibleStroke"
            && normalizedValue != "visibleFill"
            && normalizedValue != "visiblePainted"
            && normalizedValue != "bounding-box"
            && normalizedValue != "all")
        return {"unsupported_property_value"};
    if(name == "stroke-width"
            && !isValidLengthValue(value, false))
        return {"unsupported_property_value"};
    if(name == "stroke-dashoffset"
            && !isValidLengthValue(value, true))
        return {"unsupported_property_value"};
    if(name == "stroke-dasharray" && !isValidDashArray(value))
        return {"unsupported_property_value"};
    if(name == "stroke-miterlimit") {
        float miterLimit = 0.f;
        if(!parseNumberValue(value, false, miterLimit) || miterLimit < 1.f)
            return {"unsupported_property_value"};
    }
    if(name == "opacity" || name == "fill-opacity"
            || name == "stroke-opacity" || name == "stop-opacity") {
        float alpha = 0.f;
        if(!parseNumberValue(value, true, alpha))
            return {"unsupported_property_value"};
        if(name == "stop-opacity"
                && isSourcePartialAlpha(value))
            return {"unsupported_partial_alpha"};
    }
    return {{}, needsColorValidation};
}

} // namespace lumina_svg_core::recorder
