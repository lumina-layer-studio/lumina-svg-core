// Recorder algorithms relocated from Lumina's pinned LunaSVG patch.
// Parsing compatibility portions derive from LunaSVG (MIT); see
// THIRD_PARTY_NOTICES.md and THIRD_PARTY_LICENSES/LunaSVG.txt.
#include "lumina_svg_core/recorder/path_precision.h"
#include "text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <utility>

namespace lumina_svg_core::recorder {

using namespace detail;

static uint32_t saturatingIncrement(uint32_t value)
{
    return value == std::numeric_limits<uint32_t>::max()
        ? value : value + 1;
}

uint32_t computeRelativePathOperationCount(std::string_view value)
{
    const char* cursor = value.data();
    const char* end = cursor + value.size();
    char command = 0;
    size_t parameterIndex = 0;
    size_t parametersPerGroup = 0;
    uint32_t chain = 0;
    uint32_t subpathChain = 0;
    uint32_t maximumChain = 0;
    bool firstMoveGroup = false;
    while(cursor < end) {
        if(std::isspace(static_cast<unsigned char>(*cursor))
                || *cursor == ',') {
            ++cursor;
            continue;
        }
        if(std::isalpha(static_cast<unsigned char>(*cursor))) {
            command = *cursor++;
            parameterIndex = 0;
            parametersPerGroup = pathParameterCount(command);
            firstMoveGroup = command == 'm' || command == 'M';
            if(command == 'z' || command == 'Z')
                chain = subpathChain;
            continue;
        }
        char* parsedEnd = nullptr;
        if((command == 'a' || command == 'A')
                && (parameterIndex == 3 || parameterIndex == 4)
                && (*cursor == '0' || *cursor == '1')) {
            parsedEnd = const_cast<char*>(cursor + 1);
        } else {
            std::strtod(cursor, &parsedEnd);
        }
        if(parsedEnd == cursor) {
            ++cursor;
            continue;
        }
        cursor = parsedEnd;
        if(parametersPerGroup == 0)
            continue;
        parameterIndex += 1;
        if(parameterIndex < parametersPerGroup)
            continue;
        parameterIndex = 0;
        if(command >= 'a' && command <= 'z')
            chain = saturatingIncrement(chain);
        else
            chain = 0;
        maximumChain = std::max(maximumChain, chain);
        if(firstMoveGroup) {
            subpathChain = chain;
            firstMoveGroup = false;
            command = command == 'm' ? 'l' : 'L';
        }
    }
    return maximumChain;
}

static double floatUlp(float value)
{
    if(!std::isfinite(value))
        return std::numeric_limits<double>::infinity();
    const float upward = std::nextafter(
        value, std::numeric_limits<float>::infinity());
    const float downward = std::nextafter(
        value, -std::numeric_limits<float>::infinity());
    return std::max(
        std::abs(static_cast<double>(upward) - value),
        std::abs(static_cast<double>(value) - downward));
}

static double computeArcLoweringPrecisionError(std::string_view value)
{
    constexpr double PI = 3.141592653589793238462643383279502884;
    constexpr float FLOAT_DEGREES_TO_RADIANS =
        3.14159265358979323846f / 180.0f;
    // PlutoVG lowers arcs in spans no larger than pi/8. One part per million
    // of the major radius conservatively covers the cubic approximation;
    // input-angle and stored-float errors are charged separately below.
    constexpr double ARC_CUBIC_RELATIVE_ERROR = 1.0e-6;
    constexpr double ARC_OPERATION_ULPS = 16.0;

    const char* cursor = value.data();
    const char* end = cursor + value.size();
    char command = 0;
    size_t parameterIndex = 0;
    double radiusX = 0;
    double radiusY = 0;
    double maximumError = 0;
    while(cursor < end) {
        if(std::isspace(static_cast<unsigned char>(*cursor))
                || *cursor == ',') {
            ++cursor;
            continue;
        }
        if(std::isalpha(static_cast<unsigned char>(*cursor))) {
            command = *cursor++;
            parameterIndex = 0;
            continue;
        }
        char* parsedEnd = nullptr;
        const double parsed = std::strtod(cursor, &parsedEnd);
        if(parsedEnd == cursor) {
            ++cursor;
            continue;
        }
        cursor = parsedEnd;
        if(command != 'A' && command != 'a')
            continue;
        if(parameterIndex == 0) {
            radiusX = std::abs(parsed);
        } else if(parameterIndex == 1) {
            radiusY = std::abs(parsed);
        } else if(parameterIndex == 2 && radiusX > 0 && radiusY > 0) {
            const float floatRadiusX = static_cast<float>(radiusX);
            const float floatRadiusY = static_cast<float>(radiusY);
            const float floatAngle = static_cast<float>(parsed);
            const float floatRadians =
                floatAngle * FLOAT_DEGREES_TO_RADIANS;
            if(!std::isfinite(floatRadiusX)
                    || !std::isfinite(floatRadiusY)
                    || !std::isfinite(floatRadians)) {
                return std::numeric_limits<double>::infinity();
            }
            const double majorRadius = std::max(radiusX, radiusY);
            const double angleErrorRadians = std::abs(
                static_cast<double>(floatRadians) - parsed * PI / 180.0)
                + floatUlp(floatRadians) * ARC_OPERATION_ULPS;
            const double radiusStorageError = std::max(
                floatUlp(floatRadiusX), floatUlp(floatRadiusY))
                * ARC_OPERATION_ULPS;
            const double operationError = majorRadius
                * std::numeric_limits<double>::epsilon()
                * ARC_OPERATION_ULPS;
            maximumError = std::max(
                maximumError,
                majorRadius * (ARC_CUBIC_RELATIVE_ERROR
                    + angleErrorRadians)
                    + radiusStorageError + operationError);
        }
        parameterIndex = (parameterIndex + 1) % 7;
    }
    return maximumError;
}

struct ParsedPathNumber {
    float actual{0};
    long double exact{0};
};

static bool parsePathNumber(const char*& cursor, const char* end,
                     ParsedPathNumber& number)
{
    const char* position = cursor;
    float actualInteger = 0;
    float actualFraction = 0;
    float actualExponent = 0;
    long double exactInteger = 0;
    long double exactFraction = 0;
    long double exactDivisor = 1;
    int sign = 1;
    int exponentSign = 1;
    if(position < end && *position == '+') {
        ++position;
    } else if(position < end && *position == '-') {
        ++position;
        sign = -1;
    }
    if(position >= end
            || (*position != '.'
                && !std::isdigit(static_cast<unsigned char>(*position))))
        return false;
    if(std::isdigit(static_cast<unsigned char>(*position))) {
        do {
            const int digit = *position++ - '0';
            actualInteger = 10.f * actualInteger + digit;
            exactInteger = 10.0L * exactInteger + digit;
        } while(position < end
                && std::isdigit(static_cast<unsigned char>(*position)));
    }
    if(position < end && *position == '.') {
        ++position;
        if(position >= end
                || !std::isdigit(static_cast<unsigned char>(*position)))
            return false;
        float actualDivisor = 1.f;
        do {
            const int digit = *position++ - '0';
            actualFraction = 10.f * actualFraction + digit;
            actualDivisor *= 10.f;
            exactFraction = 10.0L * exactFraction + digit;
            exactDivisor *= 10.0L;
        } while(position < end
                && std::isdigit(static_cast<unsigned char>(*position)));
        actualFraction /= actualDivisor;
        exactFraction /= exactDivisor;
    }
    if(position < end && (*position == 'e' || *position == 'E')) {
        ++position;
        if(position < end && *position == '+') {
            ++position;
        } else if(position < end && *position == '-') {
            ++position;
            exponentSign = -1;
        }
        if(position >= end
                || !std::isdigit(static_cast<unsigned char>(*position)))
            return false;
        do {
            actualExponent = 10.f * actualExponent + (*position++ - '0');
        } while(position < end
                && std::isdigit(static_cast<unsigned char>(*position)));
    }
    number.actual = static_cast<float>(sign)
        * (actualInteger + actualFraction);
    number.exact = static_cast<long double>(sign)
        * (exactInteger + exactFraction);
    if(actualExponent != 0) {
        number.actual *= std::pow(
            10.f, static_cast<float>(exponentSign) * actualExponent);
        number.exact *= std::pow(
            10.0L,
            static_cast<long double>(exponentSign) * actualExponent);
    }
    if(!std::isfinite(number.actual) || !std::isfinite(number.exact))
        return false;
    cursor = position;
    return true;
}

static void skipPathSeparators(const char*& cursor, const char* end)
{
    while(cursor < end
            && (std::isspace(static_cast<unsigned char>(*cursor))
                || *cursor == ','))
        ++cursor;
}

static size_t expandedArcSegmentCount(float currentX, float currentY,
                               const std::array<ParsedPathNumber, 7>& values,
                               bool relative)
{
    constexpr double PI = 3.141592653589793238462643383279502884;
    double radiusX = std::abs(static_cast<double>(values[0].actual));
    double radiusY = std::abs(static_cast<double>(values[1].actual));
    const double angle = static_cast<double>(values[2].actual) * PI / 180.0;
    const double endX = static_cast<double>(values[5].actual)
        + (relative ? currentX : 0.f);
    const double endY = static_cast<double>(values[6].actual)
        + (relative ? currentY : 0.f);
    if(radiusX == 0.0 || radiusY == 0.0
            || (endX == currentX && endY == currentY))
        return 1;

    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double deltaX = (static_cast<double>(currentX) - endX) * 0.5;
    const double deltaY = (static_cast<double>(currentY) - endY) * 0.5;
    const double transformedX = cosine * deltaX + sine * deltaY;
    const double transformedY = -sine * deltaX + cosine * deltaY;
    const double radiusScale = transformedX * transformedX
            / (radiusX * radiusX)
        + transformedY * transformedY / (radiusY * radiusY);
    if(radiusScale > 1.0) {
        const double scale = std::sqrt(radiusScale);
        radiusX *= scale;
        radiusY *= scale;
    }

    const double numerator = std::max(
        0.0,
        radiusX * radiusX * radiusY * radiusY
            - radiusX * radiusX * transformedY * transformedY
            - radiusY * radiusY * transformedX * transformedX);
    const double denominator = radiusX * radiusX
            * transformedY * transformedY
        + radiusY * radiusY * transformedX * transformedX;
    if(denominator == 0.0)
        return 1;
    double centerScale = std::sqrt(numerator / denominator);
    const bool largeArc = values[3].exact == 1.0L;
    const bool sweep = values[4].exact == 1.0L;
    if(largeArc == sweep)
        centerScale = -centerScale;
    const double centerX = centerScale * radiusX * transformedY / radiusY;
    const double centerY = -centerScale * radiusY * transformedX / radiusX;
    const double startVectorX = (transformedX - centerX) / radiusX;
    const double startVectorY = (transformedY - centerY) / radiusY;
    const double endVectorX = (-transformedX - centerX) / radiusX;
    const double endVectorY = (-transformedY - centerY) / radiusY;
    double sweepAngle = std::atan2(
        startVectorX * endVectorY - startVectorY * endVectorX,
        startVectorX * endVectorX + startVectorY * endVectorY);
    if(sweepAngle < 0.0 && sweep)
        sweepAngle += 2.0 * PI;
    else if(sweepAngle > 0.0 && !sweep)
        sweepAngle -= 2.0 * PI;
    if(!std::isfinite(sweepAngle))
        return 16;
    return std::max<size_t>(
        1, static_cast<size_t>(std::ceil(std::abs(sweepAngle) / (PI / 8.0))));
}

PathParseWork computePathParseWork(std::string_view value)
{
    PathParseWork work;
    const char* cursor = value.data();
    const char* end = cursor + value.size();
    char command = 0;
    char previousCommand = 0;
    float currentX = 0.f;
    float currentY = 0.f;
    float startX = 0.f;
    float startY = 0.f;
    bool firstCommand = true;
    const auto add = [&](size_t verbs, size_t points) {
        if(verbs > std::numeric_limits<size_t>::max() - work.verbs
                || points > std::numeric_limits<size_t>::max() - work.points) {
            work.exceeded = true;
            return false;
        }
        work.verbs += verbs;
        work.points += points;
        return true;
    };

    skipPathSeparators(cursor, end);
    while(cursor < end) {
        if(std::isalpha(static_cast<unsigned char>(*cursor))) {
            command = *cursor++;
            skipPathSeparators(cursor, end);
        }
        if(command == 0
                || (firstCommand && command != 'M' && command != 'm')) {
            work.valid = false;
            return work;
        }
        const char normalized = static_cast<char>(std::tolower(
            static_cast<unsigned char>(command)));
        if(normalized == 'z') {
            if(previousCommand == 'Z' || previousCommand == 'z') {
                work.valid = false;
                return work;
            }
            if(!add(1, 1))
                return work;
            currentX = startX;
            currentY = startY;
            previousCommand = command;
            command = 0;
            firstCommand = false;
            skipPathSeparators(cursor, end);
            continue;
        }

        const size_t parameterCount = pathParameterCount(command);
        if(parameterCount == 0) {
            work.valid = false;
            return work;
        }
        std::array<ParsedPathNumber, 7> values{};
        for(size_t index = 0; index < parameterCount; ++index) {
            if(!parsePathNumber(cursor, end, values[index])) {
                work.valid = false;
                return work;
            }
            skipPathSeparators(cursor, end);
        }
        if(normalized == 'a'
                && ((values[3].exact != 0.0L && values[3].exact != 1.0L)
                    || (values[4].exact != 0.0L
                        && values[4].exact != 1.0L))) {
            work.valid = false;
            return work;
        }

        const bool relative = command >= 'a' && command <= 'z';
        size_t expandedVerbs = 1;
        size_t expandedPoints = 1;
        if(normalized == 'c' || normalized == 's'
                || normalized == 'q' || normalized == 't') {
            expandedPoints = 3;
        } else if(normalized == 'a') {
            expandedVerbs = expandedArcSegmentCount(
                currentX, currentY, values, relative);
            expandedPoints = expandedVerbs * 3;
        }
        if(!add(expandedVerbs, expandedPoints))
            return work;

        const auto coordinate = [&](size_t index, float current) {
            return values[index].actual + (relative ? current : 0.f);
        };
        if(normalized == 'm' || normalized == 'l'
                || normalized == 't') {
            currentX = coordinate(0, currentX);
            currentY = coordinate(1, currentY);
            if(normalized == 'm') {
                startX = currentX;
                startY = currentY;
                command = relative ? 'l' : 'L';
            }
        } else if(normalized == 'h') {
            currentX = coordinate(0, currentX);
        } else if(normalized == 'v') {
            currentY = coordinate(0, currentY);
        } else if(normalized == 'c') {
            currentX = coordinate(4, currentX);
            currentY = coordinate(5, currentY);
        } else if(normalized == 's' || normalized == 'q') {
            currentX = coordinate(2, currentX);
            currentY = coordinate(3, currentY);
        } else if(normalized == 'a') {
            currentX = coordinate(5, currentX);
            currentY = coordinate(6, currentY);
        }
        previousCommand = command;
        firstCommand = false;
    }
    return work;
}

PointListParseWork computePointListParseWork(std::string_view input)
{
    PointListParseWork work;
    stripLeadingSpaces(input);
    while(!input.empty()) {
        float coordinate = 0.f;
        if(!parseNumber(input, coordinate)
                || !skipOptionalSpacesOrComma(input)
                || !parseNumber(input, coordinate)) {
            work.valid = false;
            return work;
        }
        if(work.points == std::numeric_limits<size_t>::max()) {
            work.exceeded = true;
            return work;
        }
        ++work.points;
        skipOptionalSpacesOrComma(input);
    }
    return work;
}

double computePathInputPrecisionError(
    std::string_view value, bool& strokeDirectionAmbiguous,
    bool& isolatedStrokeDirectionAmbiguous,
    double& maximumErrorX, double& maximumErrorY,
    bool& sourceHasLoweredCurve)
{
    strokeDirectionAmbiguous = false;
    isolatedStrokeDirectionAmbiguous = false;
    maximumErrorX = 0;
    maximumErrorY = 0;
    sourceHasLoweredCurve = false;
    const char* cursor = value.data();
    const char* end = cursor + value.size();
    char command = 0;
    char previousCommand = 0;
    float actualX = 0;
    float actualY = 0;
    float actualStartX = 0;
    float actualStartY = 0;
    float actualControlX = 0;
    float actualControlY = 0;
    long double exactX = 0;
    long double exactY = 0;
    long double exactStartX = 0;
    long double exactStartY = 0;
    long double exactControlX = 0;
    long double exactControlY = 0;
    long double maximumError = 0;
    long double maximumIsotropicError = 0;
    long double maximumCoordinateErrorX = 0;
    long double maximumCoordinateErrorY = 0;
    size_t subpathNonzeroSegmentCount = 0;
    bool subpathHasAmbiguousLine = false;
    const auto finishSubpath = [&]() {
        if(subpathHasAmbiguousLine) {
            if(subpathNonzeroSegmentCount > 1)
                strokeDirectionAmbiguous = true;
            else
                isolatedStrokeDirectionAmbiguous = true;
        }
        subpathNonzeroSegmentCount = 0;
        subpathHasAmbiguousLine = false;
    };
    const auto includePoint = [&](float actualPointX, float actualPointY,
                                  long double exactPointX,
                                  long double exactPointY) {
        maximumCoordinateErrorX = std::max(
            maximumCoordinateErrorX,
            std::abs(static_cast<long double>(actualPointX) - exactPointX));
        maximumCoordinateErrorY = std::max(
            maximumCoordinateErrorY,
            std::abs(static_cast<long double>(actualPointY) - exactPointY));
        maximumError = std::max({
            maximumError,
            maximumCoordinateErrorX,
            maximumCoordinateErrorY,
        });
    };
    const auto includeStrokeDirection = [
        &strokeDirectionAmbiguous](
        float actualFromX, float actualFromY,
        long double exactFromX, long double exactFromY,
        float actualToX, float actualToY,
        long double exactToX, long double exactToY,
        float fallbackX, float fallbackY) {
        const long double exactVectorX = exactToX - exactFromX;
        const long double exactVectorY = exactToY - exactFromY;
        const long double exactLength = std::hypot(
            exactVectorX, exactVectorY);
        if(exactLength == 0.0L
                || (fallbackX == actualFromX
                    && fallbackY == actualFromY))
            return;
        const long double actualVectorX =
            static_cast<long double>(actualToX) - actualFromX;
        const long double actualVectorY =
            static_cast<long double>(actualToY) - actualFromY;
        const long double actualLength = std::hypot(
            actualVectorX, actualVectorY);
        const long double vectorError = std::hypot(
            actualVectorX - exactVectorX,
            actualVectorY - exactVectorY);
        if(actualLength <= vectorError)
            strokeDirectionAmbiguous = true;
    };
    const auto includeLineDirection = [
        &subpathNonzeroSegmentCount, &subpathHasAmbiguousLine](
        float actualFromX, float actualFromY,
        long double exactFromX, long double exactFromY,
        float actualToX, float actualToY,
        long double exactToX, long double exactToY) {
        const long double exactVectorX = exactToX - exactFromX;
        const long double exactVectorY = exactToY - exactFromY;
        if(exactVectorX == 0.0L && exactVectorY == 0.0L)
            return;
        ++subpathNonzeroSegmentCount;
        const long double actualVectorX =
            static_cast<long double>(actualToX) - actualFromX;
        const long double actualVectorY =
            static_cast<long double>(actualToY) - actualFromY;
        const long double actualLength = std::hypot(
            actualVectorX, actualVectorY);
        const long double vectorError = std::hypot(
            actualVectorX - exactVectorX,
            actualVectorY - exactVectorY);
        if(actualLength <= vectorError)
            subpathHasAmbiguousLine = true;
    };
    const auto includeNonlinearSegment = [
        &subpathNonzeroSegmentCount](
        long double startX, long double startY,
        std::initializer_list<std::pair<long double, long double>> points) {
        for(const auto& point : points) {
            if(point.first != startX || point.second != startY) {
                ++subpathNonzeroSegmentCount;
                return;
            }
        }
    };
    skipPathSeparators(cursor, end);
    while(cursor < end) {
        if(std::isalpha(static_cast<unsigned char>(*cursor))) {
            command = *cursor++;
            skipPathSeparators(cursor, end);
        }
        if(command == 0)
            return std::numeric_limits<double>::infinity();
        const char normalized = static_cast<char>(std::tolower(
            static_cast<unsigned char>(command)));
        if(normalized == 'q' || normalized == 't' || normalized == 'a')
            sourceHasLoweredCurve = true;
        if(normalized == 'z') {
            includeLineDirection(
                actualX, actualY, exactX, exactY,
                actualStartX, actualStartY, exactStartX, exactStartY);
            actualX = actualStartX;
            actualY = actualStartY;
            exactX = exactStartX;
            exactY = exactStartY;
            finishSubpath();
            previousCommand = command;
            command = 0;
            skipPathSeparators(cursor, end);
            continue;
        }
        const size_t parameterCount = pathParameterCount(command);
        if(parameterCount == 0)
            return std::numeric_limits<double>::infinity();
        std::array<ParsedPathNumber, 7> values{};
        for(size_t index = 0; index < parameterCount; ++index) {
            if(!parsePathNumber(cursor, end, values[index]))
                return std::numeric_limits<double>::infinity();
            skipPathSeparators(cursor, end);
        }
        const bool relative = command >= 'a' && command <= 'z';
        const auto actualCoordinate = [&](size_t index, float current) {
            return relative ? values[index].actual + current
                : values[index].actual;
        };
        const auto exactCoordinate = [&](size_t index,
                                         long double current) {
            return relative ? values[index].exact + current
                : values[index].exact;
        };
        if(normalized == 'm') {
            finishSubpath();
            actualX = actualCoordinate(0, actualX);
            actualY = actualCoordinate(1, actualY);
            exactX = exactCoordinate(0, exactX);
            exactY = exactCoordinate(1, exactY);
            includePoint(actualX, actualY, exactX, exactY);
            actualStartX = actualX;
            actualStartY = actualY;
            exactStartX = exactX;
            exactStartY = exactY;
            command = relative ? 'l' : 'L';
        } else if(normalized == 'l') {
            const float previousActualX = actualX;
            const float previousActualY = actualY;
            const long double previousExactX = exactX;
            const long double previousExactY = exactY;
            actualX = actualCoordinate(0, actualX);
            actualY = actualCoordinate(1, actualY);
            exactX = exactCoordinate(0, exactX);
            exactY = exactCoordinate(1, exactY);
            includeLineDirection(
                previousActualX, previousActualY,
                previousExactX, previousExactY,
                actualX, actualY, exactX, exactY);
            includePoint(actualX, actualY, exactX, exactY);
        } else if(normalized == 'h') {
            const float previousActualX = actualX;
            const long double previousExactX = exactX;
            actualX = actualCoordinate(0, actualX);
            exactX = exactCoordinate(0, exactX);
            includeLineDirection(
                previousActualX, actualY, previousExactX, exactY,
                actualX, actualY, exactX, exactY);
            includePoint(actualX, actualY, exactX, exactY);
        } else if(normalized == 'v') {
            const float previousActualY = actualY;
            const long double previousExactY = exactY;
            actualY = actualCoordinate(0, actualY);
            exactY = exactCoordinate(0, exactY);
            includeLineDirection(
                actualX, previousActualY, exactX, previousExactY,
                actualX, actualY, exactX, exactY);
            includePoint(actualX, actualY, exactX, exactY);
        } else if(normalized == 'c') {
            const float actualStartX = actualX;
            const float actualStartY = actualY;
            const long double exactStartX = exactX;
            const long double exactStartY = exactY;
            const float actualControl1X = actualCoordinate(0, actualX);
            const float actualControl1Y = actualCoordinate(1, actualY);
            const float actualControl2X = actualCoordinate(2, actualX);
            const float actualControl2Y = actualCoordinate(3, actualY);
            const long double exactControl1X = exactCoordinate(0, exactX);
            const long double exactControl1Y = exactCoordinate(1, exactY);
            const long double exactControl2X = exactCoordinate(2, exactX);
            const long double exactControl2Y = exactCoordinate(3, exactY);
            const float actualEndX = actualCoordinate(4, actualX);
            const float actualEndY = actualCoordinate(5, actualY);
            const long double exactEndX = exactCoordinate(4, exactX);
            const long double exactEndY = exactCoordinate(5, exactY);
            includeNonlinearSegment(
                exactStartX, exactStartY,
                {{exactControl1X, exactControl1Y},
                 {exactControl2X, exactControl2Y},
                 {exactEndX, exactEndY}});
            includeStrokeDirection(
                actualStartX, actualStartY, exactStartX, exactStartY,
                actualControl1X, actualControl1Y,
                exactControl1X, exactControl1Y,
                actualControl2X != actualStartX
                        || actualControl2Y != actualStartY
                    ? actualControl2X : actualEndX,
                actualControl2X != actualStartX
                        || actualControl2Y != actualStartY
                    ? actualControl2Y : actualEndY);
            includeStrokeDirection(
                actualEndX, actualEndY, exactEndX, exactEndY,
                actualControl2X, actualControl2Y,
                exactControl2X, exactControl2Y,
                actualControl1X != actualEndX
                        || actualControl1Y != actualEndY
                    ? actualControl1X : actualStartX,
                actualControl1X != actualEndX
                        || actualControl1Y != actualEndY
                    ? actualControl1Y : actualStartY);
            actualX = actualEndX;
            actualY = actualEndY;
            exactX = exactEndX;
            exactY = exactEndY;
            includePoint(actualControl1X, actualControl1Y,
                         exactControl1X, exactControl1Y);
            includePoint(actualControl2X, actualControl2Y,
                         exactControl2X, exactControl2Y);
            includePoint(actualX, actualY, exactX, exactY);
            actualControlX = actualControl2X;
            actualControlY = actualControl2Y;
            exactControlX = exactControl2X;
            exactControlY = exactControl2Y;
        } else if(normalized == 's') {
            const float actualStartX = actualX;
            const float actualStartY = actualY;
            const long double exactStartX = exactX;
            const long double exactStartY = exactY;
            const bool reflect = previousCommand == 'C'
                || previousCommand == 'c' || previousCommand == 'S'
                || previousCommand == 's';
            const float actualControl1X = reflect
                ? 2.f * actualX - actualControlX : actualX;
            const float actualControl1Y = reflect
                ? 2.f * actualY - actualControlY : actualY;
            const long double exactControl1X = reflect
                ? 2.0L * exactX - exactControlX : exactX;
            const long double exactControl1Y = reflect
                ? 2.0L * exactY - exactControlY : exactY;
            const float actualControl2X = actualCoordinate(0, actualX);
            const float actualControl2Y = actualCoordinate(1, actualY);
            const long double exactControl2X = exactCoordinate(0, exactX);
            const long double exactControl2Y = exactCoordinate(1, exactY);
            const float actualEndX = actualCoordinate(2, actualX);
            const float actualEndY = actualCoordinate(3, actualY);
            const long double exactEndX = exactCoordinate(2, exactX);
            const long double exactEndY = exactCoordinate(3, exactY);
            includeNonlinearSegment(
                exactStartX, exactStartY,
                {{exactControl1X, exactControl1Y},
                 {exactControl2X, exactControl2Y},
                 {exactEndX, exactEndY}});
            includeStrokeDirection(
                actualStartX, actualStartY, exactStartX, exactStartY,
                actualControl1X, actualControl1Y,
                exactControl1X, exactControl1Y,
                actualControl2X != actualStartX
                        || actualControl2Y != actualStartY
                    ? actualControl2X : actualEndX,
                actualControl2X != actualStartX
                        || actualControl2Y != actualStartY
                    ? actualControl2Y : actualEndY);
            includeStrokeDirection(
                actualEndX, actualEndY, exactEndX, exactEndY,
                actualControl2X, actualControl2Y,
                exactControl2X, exactControl2Y,
                actualControl1X != actualEndX
                        || actualControl1Y != actualEndY
                    ? actualControl1X : actualStartX,
                actualControl1X != actualEndX
                        || actualControl1Y != actualEndY
                    ? actualControl1Y : actualStartY);
            actualX = actualEndX;
            actualY = actualEndY;
            exactX = exactEndX;
            exactY = exactEndY;
            includePoint(actualControl1X, actualControl1Y,
                         exactControl1X, exactControl1Y);
            includePoint(actualControl2X, actualControl2Y,
                         exactControl2X, exactControl2Y);
            includePoint(actualX, actualY, exactX, exactY);
            actualControlX = actualControl2X;
            actualControlY = actualControl2Y;
            exactControlX = exactControl2X;
            exactControlY = exactControl2Y;
        } else if(normalized == 'q' || normalized == 't') {
            const float actualStartX = actualX;
            const float actualStartY = actualY;
            const long double exactStartX = exactX;
            const long double exactStartY = exactY;
            const bool smooth = normalized == 't';
            const bool reflect = previousCommand == 'Q'
                || previousCommand == 'q' || previousCommand == 'T'
                || previousCommand == 't';
            const float actualQuadraticX = smooth
                ? (reflect ? 2.f * actualX - actualControlX : actualX)
                : actualCoordinate(0, actualX);
            const float actualQuadraticY = smooth
                ? (reflect ? 2.f * actualY - actualControlY : actualY)
                : actualCoordinate(1, actualY);
            const long double exactQuadraticX = smooth
                ? (reflect ? 2.0L * exactX - exactControlX : exactX)
                : exactCoordinate(0, exactX);
            const long double exactQuadraticY = smooth
                ? (reflect ? 2.0L * exactY - exactControlY : exactY)
                : exactCoordinate(1, exactY);
            const size_t endOffset = smooth ? 0 : 2;
            const float actualEndX = actualCoordinate(endOffset, actualX);
            const float actualEndY = actualCoordinate(endOffset + 1, actualY);
            const long double exactEndX = exactCoordinate(endOffset, exactX);
            const long double exactEndY = exactCoordinate(
                endOffset + 1, exactY);
            includeNonlinearSegment(
                exactStartX, exactStartY,
                {{exactQuadraticX, exactQuadraticY},
                 {exactEndX, exactEndY}});
            includeStrokeDirection(
                actualStartX, actualStartY, exactStartX, exactStartY,
                actualQuadraticX, actualQuadraticY,
                exactQuadraticX, exactQuadraticY,
                actualEndX, actualEndY);
            includeStrokeDirection(
                actualEndX, actualEndY, exactEndX, exactEndY,
                actualQuadraticX, actualQuadraticY,
                exactQuadraticX, exactQuadraticY,
                actualStartX, actualStartY);
            const float actualCubic1X = 2.f / 3.f * actualQuadraticX
                + 1.f / 3.f * actualX;
            const float actualCubic1Y = 2.f / 3.f * actualQuadraticY
                + 1.f / 3.f * actualY;
            const float actualCubic2X = 2.f / 3.f * actualQuadraticX
                + 1.f / 3.f * actualEndX;
            const float actualCubic2Y = 2.f / 3.f * actualQuadraticY
                + 1.f / 3.f * actualEndY;
            const long double exactCubic1X = 2.0L / 3.0L * exactQuadraticX
                + 1.0L / 3.0L * exactX;
            const long double exactCubic1Y = 2.0L / 3.0L * exactQuadraticY
                + 1.0L / 3.0L * exactY;
            const long double exactCubic2X = 2.0L / 3.0L * exactQuadraticX
                + 1.0L / 3.0L * exactEndX;
            const long double exactCubic2Y = 2.0L / 3.0L * exactQuadraticY
                + 1.0L / 3.0L * exactEndY;
            includePoint(actualCubic1X, actualCubic1Y,
                         exactCubic1X, exactCubic1Y);
            includePoint(actualCubic2X, actualCubic2Y,
                         exactCubic2X, exactCubic2Y);
            includePoint(actualEndX, actualEndY, exactEndX, exactEndY);
            actualControlX = actualQuadraticX;
            actualControlY = actualQuadraticY;
            exactControlX = exactQuadraticX;
            exactControlY = exactQuadraticY;
            actualX = actualEndX;
            actualY = actualEndY;
            exactX = exactEndX;
            exactY = exactEndY;
        } else if(normalized == 'a') {
            if(values[3].exact != 0.0L && values[3].exact != 1.0L)
                return std::numeric_limits<double>::infinity();
            if(values[4].exact != 0.0L && values[4].exact != 1.0L)
                return std::numeric_limits<double>::infinity();
            const float actualEndX = actualCoordinate(5, actualX);
            const float actualEndY = actualCoordinate(6, actualY);
            const long double exactEndX = exactCoordinate(5, exactX);
            const long double exactEndY = exactCoordinate(6, exactY);
            if(exactEndX != exactX || exactEndY != exactY)
                ++subpathNonzeroSegmentCount;
            const long double exactChord = std::hypot(
                exactEndX - exactX, exactEndY - exactY);
            const double actualChord = std::hypot(
                static_cast<double>(actualEndX - actualX),
                static_cast<double>(actualEndY - actualY));
            const long double endpointError = std::hypot(
                    static_cast<long double>(actualX) - exactX,
                    static_cast<long double>(actualY) - exactY)
                + std::hypot(
                    static_cast<long double>(actualEndX) - exactEndX,
                    static_cast<long double>(actualEndY) - exactEndY);
            const long double majorRadius = std::max(
                std::abs(values[0].exact), std::abs(values[1].exact));
            const long double minorRadius = std::min(
                std::abs(values[0].exact), std::abs(values[1].exact));
            if(exactChord > 0.0L && majorRadius > 0.0L) {
                if(actualChord == 0.0) {
                    return std::numeric_limits<double>::infinity();
                }
                const long double chordConditioningError =
                    majorRadius * endpointError / exactChord;
                maximumIsotropicError = std::max(
                    maximumIsotropicError, chordConditioningError);
                maximumError = std::max(
                    maximumError, chordConditioningError);
                if(minorRadius > 0.0L) {
                    const long double eccentricityConditioningError =
                        endpointError * majorRadius / minorRadius;
                    maximumIsotropicError = std::max(
                        maximumIsotropicError,
                        eccentricityConditioningError);
                    maximumError = std::max(
                        maximumError,
                        eccentricityConditioningError);
                }
            }
            actualX = actualEndX;
            actualY = actualEndY;
            exactX = exactEndX;
            exactY = exactEndY;
            includePoint(actualX, actualY, exactX, exactY);
        } else {
            return std::numeric_limits<double>::infinity();
        }
        previousCommand = command;
    }
    finishSubpath();
    const long double arcError = computeArcLoweringPrecisionError(value);
    maximumIsotropicError = std::max(maximumIsotropicError, arcError);
    maximumCoordinateErrorX = std::max(
        maximumCoordinateErrorX, maximumIsotropicError);
    maximumCoordinateErrorY = std::max(
        maximumCoordinateErrorY, maximumIsotropicError);
    maximumErrorX = static_cast<double>(maximumCoordinateErrorX);
    maximumErrorY = static_cast<double>(maximumCoordinateErrorY);
    return static_cast<double>(std::max({
        maximumError,
        maximumCoordinateErrorX,
        maximumCoordinateErrorY,
    }));
}

} // namespace lumina_svg_core::recorder
