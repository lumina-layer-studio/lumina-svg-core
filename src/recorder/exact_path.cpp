// Recorder algorithms relocated from Lumina's pinned LunaSVG patch.
// Parsing compatibility portions derive from LunaSVG (MIT); see
// THIRD_PARTY_NOTICES.md and THIRD_PARTY_LICENSES/LunaSVG.txt.
#include "lumina_svg_core/recorder/exact_path.h"
#include "text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace lumina_svg_core::recorder {

using namespace detail;
using lunasvg::PaintCommand;
using lunasvg::PaintPoint;
using lunasvg::PaintPathVerb;

static void skipExactPathSeparators(std::string_view& input)
{
    while(!input.empty()
            && (IS_WS(input.front()) || input.front() == ','))
        input.remove_prefix(1);
}

static bool appendExactArc(std::vector<PaintPathVerb>& verbs,
                    std::vector<PaintPoint>& points,
                    double startX, double startY,
                    const std::array<double, 7>& values,
                    bool relative, double& endX, double& endY,
                    double& maximumLoweringError)
{
    constexpr double PI =
        3.141592653589793238462643383279502884;
    double radiusX = std::abs(values[0]);
    double radiusY = std::abs(values[1]);
    const double reducedAngleDegrees = std::fmod(values[2], 360.0);
    const double angle = reducedAngleDegrees * PI / 180.0;
    const bool largeArc = values[3] == 1.0;
    const bool sweep = values[4] == 1.0;
    if((values[3] != 0.0 && values[3] != 1.0)
            || (values[4] != 0.0 && values[4] != 1.0))
        return false;
    endX = relative ? startX + values[5] : values[5];
    endY = relative ? startY + values[6] : values[6];
    if(radiusX == 0.0 || radiusY == 0.0
            || (startX == endX && startY == endY)) {
        verbs.push_back(PaintPathVerb::LineTo);
        points.push_back({endX, endY});
        return true;
    }

    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    double deltaX = (startX - endX) * 0.5;
    double deltaY = (startY - endY) * 0.5;
    const double rotatedX = cosine * deltaX + sine * deltaY;
    const double rotatedY = -sine * deltaX + cosine * deltaY;
    deltaX = rotatedX;
    deltaY = rotatedY;
    const double radiusScale = deltaX * deltaX / (radiusX * radiusX)
        + deltaY * deltaY / (radiusY * radiusY);
    if(radiusScale > 1.0) {
        const double scale = std::sqrt(radiusScale);
        radiusX *= scale;
        radiusY *= scale;
    }
    constexpr double ARC_CUBIC_RELATIVE_ERROR = 1.0e-6;
    constexpr double ARC_OPERATION_ULPS = 64.0;
    const double arithmeticMagnitude = std::max({
        1.0, std::abs(startX), std::abs(startY),
        std::abs(endX), std::abs(endY), radiusX, radiusY,
    });
    maximumLoweringError = std::max(
        maximumLoweringError,
        std::max(radiusX, radiusY) * ARC_CUBIC_RELATIVE_ERROR
            + arithmeticMagnitude
                * std::numeric_limits<double>::epsilon()
                * ARC_OPERATION_ULPS);

    const auto normalize = [&](double x, double y) {
        return PaintPoint{
            (cosine * x + sine * y) / radiusX,
            (-sine * x + cosine * y) / radiusY,
        };
    };
    const auto sourceStart = normalize(startX, startY);
    const auto sourceEnd = normalize(endX, endY);
    double chordX = sourceEnd.x - sourceStart.x;
    double chordY = sourceEnd.y - sourceStart.y;
    const double chordLengthSquared = chordX * chordX + chordY * chordY;
    if(!(std::isfinite(chordLengthSquared) && chordLengthSquared > 0.0))
        return false;
    double centerScaleSquared = 1.0 / chordLengthSquared - 0.25;
    centerScaleSquared = std::max(0.0, centerScaleSquared);
    double centerScale = std::sqrt(centerScaleSquared);
    if(sweep == largeArc)
        centerScale = -centerScale;
    chordX *= centerScale;
    chordY *= centerScale;
    const double centerX = 0.5 * (sourceStart.x + sourceEnd.x) - chordY;
    const double centerY = 0.5 * (sourceStart.y + sourceEnd.y) + chordX;
    const double startAngle = std::atan2(
        sourceStart.y - centerY, sourceStart.x - centerX);
    const double endAngle = std::atan2(
        sourceEnd.y - centerY, sourceEnd.x - centerX);
    double sweepAngle = endAngle - startAngle;
    if(sweepAngle < 0.0 && sweep)
        sweepAngle += 2.0 * PI;
    else if(sweepAngle > 0.0 && !sweep)
        sweepAngle -= 2.0 * PI;
    const size_t segmentCount = std::max<size_t>(
        1, static_cast<size_t>(std::ceil(
            std::abs(sweepAngle) / (PI / 8.0))));
    const auto denormalize = [&](double x, double y) {
        return PaintPoint{
            radiusX * x * cosine - radiusY * y * sine,
            radiusX * x * sine + radiusY * y * cosine,
        };
    };
    for(size_t index = 0; index < segmentCount; ++index) {
        const double firstAngle = startAngle
            + static_cast<double>(index) * sweepAngle / segmentCount;
        const double lastAngle = startAngle
            + static_cast<double>(index + 1) * sweepAngle / segmentCount;
        const double tangent = 4.0 / 3.0
            * std::tan(0.25 * (lastAngle - firstAngle));
        auto control1 = denormalize(
            std::cos(firstAngle) - tangent * std::sin(firstAngle)
                + centerX,
            std::sin(firstAngle) + tangent * std::cos(firstAngle)
                + centerY);
        auto control2 = denormalize(
            std::cos(lastAngle) + tangent * std::sin(lastAngle) + centerX,
            std::sin(lastAngle) - tangent * std::cos(lastAngle) + centerY);
        auto endpoint = denormalize(
            std::cos(lastAngle) + centerX,
            std::sin(lastAngle) + centerY);
        if(index + 1 == segmentCount)
            endpoint = {endX, endY};
        verbs.push_back(PaintPathVerb::CubicTo);
        points.push_back(control1);
        points.push_back(control2);
        points.push_back(endpoint);
    }
    return true;
}

void appendExactMove(PaintCommand& command, double x, double y)
{
    command.path_verbs.push_back(PaintPathVerb::MoveTo);
    command.path_points.push_back({x, y});
}

void appendExactLine(PaintCommand& command, double x, double y)
{
    command.path_verbs.push_back(PaintPathVerb::LineTo);
    command.path_points.push_back({x, y});
}

bool appendExactEllipse(PaintCommand& command,
                        double centerX, double centerY,
                        double radiusX, double radiusY,
                        double& loweringPrecisionError)
{
    if(!(radiusX > 0.0 && radiusY > 0.0))
        return false;
    appendExactMove(command, centerX + radiusX, centerY);
    std::array<double, 7> arc{{
        radiusX, radiusY, 0.0, 0.0, 1.0,
        centerX - radiusX, centerY,
    }};
    double endX = 0;
    double endY = 0;
    if(!appendExactArc(
            command.path_verbs, command.path_points,
            centerX + radiusX, centerY, arc, false,
            endX, endY, loweringPrecisionError))
        return false;
    arc[5] = centerX + radiusX;
    if(!appendExactArc(
            command.path_verbs, command.path_points,
            endX, endY, arc, false,
            endX, endY, loweringPrecisionError))
        return false;
    command.path_verbs.push_back(PaintPathVerb::Close);
    return true;
}

bool appendExactRect(PaintCommand& command,
                     double x, double y, double width, double height,
                     double radiusX, double radiusY,
                     bool hasRadiusX, bool hasRadiusY,
                     bool& containsArc, double& loweringPrecisionError)
{
    if(!hasRadiusX)
        radiusX = radiusY;
    if(!hasRadiusY)
        radiusY = radiusX;
    radiusX = std::min(radiusX, width * 0.5);
    radiusY = std::min(radiusY, height * 0.5);
    if(!(radiusX > 0.0 && radiusY > 0.0)) {
        appendExactMove(command, x, y);
        appendExactLine(command, x + width, y);
        appendExactLine(command, x + width, y + height);
        appendExactLine(command, x, y + height);
        command.path_verbs.push_back(PaintPathVerb::Close);
        return true;
    }
    containsArc = true;
    appendExactMove(command, x + radiusX, y);
    appendExactLine(command, x + width - radiusX, y);
    const std::array<std::array<double, 2>, 4> endpoints{{
        {{x + width, y + radiusY}},
        {{x + width - radiusX, y + height}},
        {{x, y + height - radiusY}},
        {{x + radiusX, y}},
    }};
    const std::array<std::array<double, 2>, 3> lineEnds{{
        {{x + width, y + height - radiusY}},
        {{x + radiusX, y + height}},
        {{x, y + radiusY}},
    }};
    double startX = x + width - radiusX;
    double startY = y;
    for(size_t index = 0; index < endpoints.size(); ++index) {
        std::array<double, 7> arc{{
            radiusX, radiusY, 0.0, 0.0, 1.0,
            endpoints[index][0], endpoints[index][1],
        }};
        double endX = 0;
        double endY = 0;
        if(!appendExactArc(
                command.path_verbs, command.path_points,
                startX, startY, arc, false,
                endX, endY, loweringPrecisionError))
            return false;
        startX = endX;
        startY = endY;
        if(index < lineEnds.size()) {
            startX = lineEnds[index][0];
            startY = lineEnds[index][1];
            appendExactLine(command, startX, startY);
        }
    }
    command.path_verbs.push_back(PaintPathVerb::Close);
    return true;
}

bool recordExactPointList(std::string_view input, bool closed,
                          PaintCommand& command)
{
    skipExactPathSeparators(input);
    bool first = true;
    while(!input.empty()) {
        double x = 0;
        double y = 0;
        if(!parseNumber(input, x) || !std::isfinite(x))
            return false;
        skipExactPathSeparators(input);
        if(!parseNumber(input, y) || !std::isfinite(y))
            return false;
        skipExactPathSeparators(input);
        if(first) {
            appendExactMove(command, x, y);
            first = false;
        } else {
            appendExactLine(command, x, y);
        }
    }
    if(first)
        return false;
    if(closed)
        command.path_verbs.push_back(PaintPathVerb::Close);
    return true;
}

bool recordExactPath(std::string_view input,
                     PaintCommand& command,
                     bool& containsArc,
                     double& loweringPrecisionError)
{
    containsArc = false;
    loweringPrecisionError = 0.0;
    PaintCommand exactCommand;
    char activeCommand = 0;
    char previousCommand = 0;
    double currentX = 0;
    double currentY = 0;
    double startX = 0;
    double startY = 0;
    double previousControlX = 0;
    double previousControlY = 0;
    double previousQuadraticX = 0;
    double previousQuadraticY = 0;
    skipExactPathSeparators(input);
    while(!input.empty()) {
        if(IS_ALPHA(input.front())) {
            activeCommand = input.front();
            input.remove_prefix(1);
            skipExactPathSeparators(input);
        }
        if(activeCommand == 0)
            return false;
        const char executedCommand = activeCommand;
        const char normalized = static_cast<char>(std::tolower(
            static_cast<unsigned char>(executedCommand)));
        if(normalized == 'z') {
            exactCommand.path_verbs.push_back(PaintPathVerb::Close);
            currentX = startX;
            currentY = startY;
            previousCommand = executedCommand;
            activeCommand = 0;
            skipExactPathSeparators(input);
            continue;
        }
        const size_t parameterCount = pathParameterCount(
            executedCommand);
        if(parameterCount == 0)
            return false;
        std::array<double, 7> values{};
        for(size_t index = 0; index < parameterCount; ++index) {
            if(!parseNumber(input, values[index])
                    || !std::isfinite(values[index]))
                return false;
            skipExactPathSeparators(input);
        }
        const bool relative = executedCommand >= 'a'
            && executedCommand <= 'z';
        const auto coordinate = [relative](double value, double current) {
            return relative ? current + value : value;
        };
        if(normalized == 'm') {
            currentX = coordinate(values[0], currentX);
            currentY = coordinate(values[1], currentY);
            startX = currentX;
            startY = currentY;
            exactCommand.path_verbs.push_back(PaintPathVerb::MoveTo);
            exactCommand.path_points.push_back({currentX, currentY});
            activeCommand = relative ? 'l' : 'L';
        } else if(normalized == 'l') {
            currentX = coordinate(values[0], currentX);
            currentY = coordinate(values[1], currentY);
            exactCommand.path_verbs.push_back(PaintPathVerb::LineTo);
            exactCommand.path_points.push_back({currentX, currentY});
        } else if(normalized == 'h') {
            currentX = coordinate(values[0], currentX);
            exactCommand.path_verbs.push_back(PaintPathVerb::LineTo);
            exactCommand.path_points.push_back({currentX, currentY});
        } else if(normalized == 'v') {
            currentY = coordinate(values[0], currentY);
            exactCommand.path_verbs.push_back(PaintPathVerb::LineTo);
            exactCommand.path_points.push_back({currentX, currentY});
        } else if(normalized == 'c') {
            const double control1X = coordinate(values[0], currentX);
            const double control1Y = coordinate(values[1], currentY);
            const double control2X = coordinate(values[2], currentX);
            const double control2Y = coordinate(values[3], currentY);
            currentX = coordinate(values[4], currentX);
            currentY = coordinate(values[5], currentY);
            exactCommand.path_verbs.push_back(PaintPathVerb::CubicTo);
            exactCommand.path_points.push_back({control1X, control1Y});
            exactCommand.path_points.push_back({control2X, control2Y});
            exactCommand.path_points.push_back({currentX, currentY});
            previousControlX = control2X;
            previousControlY = control2Y;
        } else if(normalized == 's') {
            const bool reflect = previousCommand == 'C'
                || previousCommand == 'c' || previousCommand == 'S'
                || previousCommand == 's';
            const double control1X = reflect
                ? 2.0 * currentX - previousControlX : currentX;
            const double control1Y = reflect
                ? 2.0 * currentY - previousControlY : currentY;
            const double control2X = coordinate(values[0], currentX);
            const double control2Y = coordinate(values[1], currentY);
            currentX = coordinate(values[2], currentX);
            currentY = coordinate(values[3], currentY);
            exactCommand.path_verbs.push_back(PaintPathVerb::CubicTo);
            exactCommand.path_points.push_back({control1X, control1Y});
            exactCommand.path_points.push_back({control2X, control2Y});
            exactCommand.path_points.push_back({currentX, currentY});
            previousControlX = control2X;
            previousControlY = control2Y;
        } else if(normalized == 'q' || normalized == 't') {
            const bool smooth = normalized == 't';
            const bool reflect = previousCommand == 'Q'
                || previousCommand == 'q' || previousCommand == 'T'
                || previousCommand == 't';
            const double quadraticX = smooth
                ? (reflect ? 2.0 * currentX - previousQuadraticX : currentX)
                : coordinate(values[0], currentX);
            const double quadraticY = smooth
                ? (reflect ? 2.0 * currentY - previousQuadraticY : currentY)
                : coordinate(values[1], currentY);
            const size_t endOffset = smooth ? 0 : 2;
            const double endX = coordinate(values[endOffset], currentX);
            const double endY = coordinate(values[endOffset + 1], currentY);
            const double control1X = (currentX + 2.0 * quadraticX) / 3.0;
            const double control1Y = (currentY + 2.0 * quadraticY) / 3.0;
            const double control2X = (endX + 2.0 * quadraticX) / 3.0;
            const double control2Y = (endY + 2.0 * quadraticY) / 3.0;
            exactCommand.path_verbs.push_back(PaintPathVerb::CubicTo);
            exactCommand.path_points.push_back({control1X, control1Y});
            exactCommand.path_points.push_back({control2X, control2Y});
            exactCommand.path_points.push_back({endX, endY});
            currentX = endX;
            currentY = endY;
            previousQuadraticX = quadraticX;
            previousQuadraticY = quadraticY;
        } else if(normalized == 'a') {
            containsArc = true;
            double endX = 0;
            double endY = 0;
            if(!appendExactArc(
                    exactCommand.path_verbs, exactCommand.path_points,
                    currentX, currentY, values, relative, endX, endY,
                    loweringPrecisionError))
                return false;
            currentX = endX;
            currentY = endY;
        } else {
            return false;
        }
        previousCommand = executedCommand;
    }
    if(exactCommand.path_verbs.empty())
        return false;
    command.path_verbs = std::move(exactCommand.path_verbs);
    command.path_points = std::move(exactCommand.path_points);
    return true;
}

} // namespace lumina_svg_core::recorder
