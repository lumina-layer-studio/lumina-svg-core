// Recorder algorithms relocated from Lumina's pinned LunaSVG patch.
// Parsing compatibility portions derive from LunaSVG (MIT); see
// THIRD_PARTY_NOTICES.md and THIRD_PARTY_LICENSES/LunaSVG.txt.
#include "lumina_svg_core/recorder/transform.h"
#include "text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

namespace lumina_svg_core::recorder {

using namespace detail;
using lunasvg::PaintTransform;

static PaintTransform rawMultiply(const PaintTransform& left,
                           const PaintTransform& right)
{
    return {
        left.a * right.a + left.b * right.c,
        left.a * right.b + left.b * right.d,
        left.c * right.a + left.d * right.c,
        left.c * right.b + left.d * right.d,
        left.e * right.a + left.f * right.c + right.e,
        left.e * right.b + left.f * right.d + right.f,
    };
}

bool inverseTransform(const PaintTransform& value, PaintTransform& inverse)
{
    const double determinant = value.a * value.d - value.b * value.c;
    if(!std::isfinite(determinant) || determinant == 0.0)
        return false;
    const double scale = 1.0 / determinant;
    inverse = {
        value.d * scale,
        -value.b * scale,
        -value.c * scale,
        value.a * scale,
        (value.c * value.f - value.d * value.e) * scale,
        (value.b * value.e - value.a * value.f) * scale,
    };
    return true;
}

static void skipTransformSpaces(std::string_view& input)
{
    while(!input.empty() && IS_WS(input.front()))
        input.remove_prefix(1);
}

static bool parseTransformArguments(std::string_view& input,
                             std::array<double, 6>& values,
                             size_t& count)
{
    skipTransformSpaces(input);
    if(input.empty() || input.front() != '(')
        return false;
    input.remove_prefix(1);
    count = 0;
    while(true) {
        skipTransformSpaces(input);
        if(input.empty())
            return false;
        if(input.front() == ')') {
            input.remove_prefix(1);
            return true;
        }
        if(count == values.size()
                || !parseNumber(input, values[count])
                || !std::isfinite(values[count]))
            return false;
        ++count;
        skipTransformSpaces(input);
        if(!input.empty() && input.front() == ',')
            input.remove_prefix(1);
    }
}

static PaintTransform translated(double x, double y)
{
    return {1, 0, 0, 1, x, y};
}

static PaintTransform scaled(double x, double y)
{
    return {x, 0, 0, y, 0, 0};
}

static PaintTransform rotated(double degrees)
{
    constexpr double RADIANS_PER_DEGREE =
        3.141592653589793238462643383279502884 / 180.0;
    const double radians = degrees * RADIANS_PER_DEGREE;
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    return {cosine, sine, -sine, cosine, 0, 0};
}

static PaintTransform sheared(double xDegrees, double yDegrees)
{
    constexpr double RADIANS_PER_DEGREE =
        3.141592653589793238462643383279502884 / 180.0;
    return {
        1,
        std::tan(yDegrees * RADIANS_PER_DEGREE),
        std::tan(xDegrees * RADIANS_PER_DEGREE),
        1,
        0,
        0,
    };
}

bool parseExactTransform(std::string_view input, PaintTransform& result)
{
    result = {};
    skipTransformSpaces(input);
    while(!input.empty()) {
        const size_t nameLength = [&]() {
            size_t length = 0;
            while(length < input.size() && IS_ALPHA(input[length]))
                ++length;
            return length;
        }();
        if(nameLength == 0)
            return false;
        const auto name = input.substr(0, nameLength);
        input.remove_prefix(nameLength);
        std::array<double, 6> values{};
        size_t count = 0;
        if(!parseTransformArguments(input, values, count))
            return false;
        PaintTransform operation;
        if(name == "matrix" && count == 6) {
            operation = {
                values[0], values[1], values[2],
                values[3], values[4], values[5],
            };
        } else if(name == "translate" && (count == 1 || count == 2)) {
            operation = translated(values[0], count == 2 ? values[1] : 0);
        } else if(name == "scale" && (count == 1 || count == 2)) {
            operation = scaled(values[0], count == 2 ? values[1] : values[0]);
        } else if(name == "rotate" && (count == 1 || count == 3)) {
            if(count == 3)
                result = composeRecordingTransforms(
                    result, translated(values[1], values[2]));
            operation = rotated(values[0]);
            if(count == 3) {
                result = composeRecordingTransforms(result, operation);
                operation = translated(-values[1], -values[2]);
            }
        } else if(name == "skewX" && count == 1) {
            operation = sheared(values[0], 0);
        } else if(name == "skewY" && count == 1) {
            operation = sheared(0, values[0]);
        } else {
            return false;
        }
        result = composeRecordingTransforms(result, operation);
        skipTransformSpaces(input);
        if(!input.empty() && input.front() == ',') {
            input.remove_prefix(1);
            skipTransformSpaces(input);
        }
    }
    return true;
}

PaintTransform composeRecordingTransforms(
    const PaintTransform& parent, const PaintTransform& local)
{
    return rawMultiply(local, parent);
}

static uint32_t saturatingIncrement(uint32_t value)
{
    return value == std::numeric_limits<uint32_t>::max()
        ? value : value + 1;
}

uint32_t computeTransformOperationCount(std::string_view value)
{
    uint32_t count = 0;
    for(size_t index = 0; index < value.size(); ++index) {
        if(!std::isalpha(static_cast<unsigned char>(value[index])))
            continue;
        while(index + 1 < value.size()
                && std::isalpha(static_cast<unsigned char>(value[index + 1])))
            ++index;
        size_t next = index + 1;
        while(next < value.size()
                && std::isspace(static_cast<unsigned char>(value[next])))
            ++next;
        if(next < value.size() && value[next] == '(')
            count = saturatingIncrement(count);
    }
    return count;
}

static std::vector<double> numericValues(std::string_view value)
{
    std::vector<double> values;
    const char* cursor = value.data();
    const char* end = cursor + value.size();
    while(cursor < end) {
        char* parsedEnd = nullptr;
        const double parsed = std::strtod(cursor, &parsedEnd);
        if(parsedEnd != cursor) {
            values.push_back(parsed);
            cursor = parsedEnd;
        } else {
            ++cursor;
        }
    }
    return values;
}

TransformPrecisionEstimate computeTransformPrecisionEstimate(
    std::string_view value)
{
    constexpr double OPERATION_ULPS = 4.0;
    constexpr double DEGREES_TO_RADIANS =
        3.141592653589793238462643383279502884 / 180.0;
    const double rounding = std::numeric_limits<float>::epsilon()
        * OPERATION_ULPS;
    TransformPrecisionEstimate estimate;
    size_t offset = 0;
    while(offset < value.size()) {
        while(offset < value.size()
                && !std::isalpha(static_cast<unsigned char>(value[offset])))
            ++offset;
        const size_t nameStart = offset;
        while(offset < value.size()
                && std::isalpha(static_cast<unsigned char>(value[offset])))
            ++offset;
        if(nameStart == offset)
            break;
        const auto name = value.substr(nameStart, offset - nameStart);
        const auto open = value.find('(', offset);
        if(open == std::string_view::npos)
            break;
        const auto close = value.find(')', open + 1);
        if(close == std::string_view::npos)
            break;
        const auto arguments = value.substr(open + 1, close - open - 1);
        const auto numbers = numericValues(arguments);
        double magnitude = 0;
        for(const double number : numbers)
            magnitude = std::max(magnitude, std::abs(number));
        if(name == "translate") {
            estimate.translation += magnitude * rounding;
        } else if(name == "scale") {
            estimate.linear += magnitude * rounding;
        } else if(name == "rotate") {
            const double angle = numbers.empty()
                ? 0 : std::abs(numbers.front());
            const double operationLinearError =
                angle * DEGREES_TO_RADIANS * rounding;
            estimate.linear += operationLinearError;
            if(numbers.size() == 3) {
                const double centerMagnitude = std::max(
                    std::abs(numbers[1]), std::abs(numbers[2]));
                estimate.translation += centerMagnitude
                    * (rounding + operationLinearError);
            }
        } else if(name == "skewX" || name == "skewY") {
            if(numbers.empty()) {
                estimate.linear = std::numeric_limits<double>::infinity();
            } else {
                constexpr float FLOAT_DEGREES_TO_RADIANS =
                    3.14159265358979323846f / 180.0f;
                const float actualDegrees =
                    static_cast<float>(numbers.front());
                const float actualRadians =
                    actualDegrees * FLOAT_DEGREES_TO_RADIANS;
                const float actualCoefficient = std::tan(actualRadians);
                const double exactRadians =
                    numbers.front() * DEGREES_TO_RADIANS;
                const double exactCoefficient = std::tan(exactRadians);
                if(!std::isfinite(actualCoefficient)
                        || !std::isfinite(exactCoefficient)
                        || std::signbit(actualCoefficient)
                            != std::signbit(exactCoefficient)) {
                    estimate.linear =
                        std::numeric_limits<double>::infinity();
                } else {
                    estimate.linear += std::abs(
                        static_cast<double>(actualCoefficient)
                            - exactCoefficient);
                }
            }
        } else if(name == "matrix") {
            for(size_t numberIndex = 0;
                    numberIndex < numbers.size(); ++numberIndex) {
                const double parsed = numbers[numberIndex];
                if(numberIndex < 4)
                    estimate.linear += std::abs(parsed) * rounding;
                else if(numberIndex < 6)
                    estimate.translation += std::abs(parsed) * rounding;
            }
        }
        offset = close + 1;
    }
    return estimate;
}

} // namespace lumina_svg_core::recorder
