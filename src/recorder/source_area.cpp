// Source-area diagnostics relocated from Lumina's LunaSVG recorder patch.
#include "lumina_svg_core/recorder/path_precision.h"
#include "text.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace lumina_svg_core::recorder {

using detail::pathParameterCount;

class SourceSubpathAreaTracker {
public:
    void include(long double x, long double y)
    {
        if(!m_active) {
            m_active = true;
            m_first_x = x;
            m_first_y = y;
            return;
        }
        const long double dx = x - m_first_x;
        const long double dy = y - m_first_y;
        if(!m_has_direction) {
            if(dx == 0 && dy == 0)
                return;
            m_direction_x = dx;
            m_direction_y = dy;
            m_has_direction = true;
            return;
        }
        const long double cross = m_direction_x * dy - m_direction_y * dx;
        const long double scale = std::max({
            std::abs(dx), std::abs(dy),
            std::abs(m_direction_x), std::abs(m_direction_y),
        });
        const long double error = std::numeric_limits<long double>::epsilon()
            * 128 * scale * scale;
        if(std::abs(cross) > error)
            m_area_potential = true;
    }

    void markAreaPotential()
    {
        m_area_potential = true;
    }

    bool active() const
    {
        return m_active;
    }

    uint8_t areaPotential() const
    {
        return static_cast<uint8_t>(m_area_potential);
    }

private:
    long double m_first_x{0};
    long double m_first_y{0};
    long double m_direction_x{0};
    long double m_direction_y{0};
    bool m_active{false};
    bool m_has_direction{false};
    bool m_area_potential{false};
};

std::vector<uint8_t> computeSourceSubpathAreaPotential(
    std::string_view value)
{
    std::vector<uint8_t> result;
    SourceSubpathAreaTracker tracker;
    long double currentX = 0;
    long double currentY = 0;
    long double subpathX = 0;
    long double subpathY = 0;
    long double previousCubicX = 0;
    long double previousCubicY = 0;
    long double previousQuadraticX = 0;
    long double previousQuadraticY = 0;
    bool previousWasCubic = false;
    bool previousWasQuadratic = false;
    char command = 0;
    const char* cursor = value.data();
    const char* end = cursor + value.size();
    bool firstMoveGroup = false;
    const auto finishSubpath = [&]() {
        if(tracker.active())
            result.push_back(tracker.areaPotential());
        tracker = SourceSubpathAreaTracker{};
    };
    while(cursor < end) {
        while(cursor < end
                && (std::isspace(static_cast<unsigned char>(*cursor))
                    || *cursor == ','))
            ++cursor;
        if(cursor == end)
            break;
        if(std::isalpha(static_cast<unsigned char>(*cursor))) {
            command = *cursor++;
            if(pathParameterCount(command) == 0
                    && command != 'z' && command != 'Z')
                return {};
            firstMoveGroup = command == 'm' || command == 'M';
            if(command == 'z' || command == 'Z') {
                if(!tracker.active())
                    return {};
                tracker.include(subpathX, subpathY);
                currentX = subpathX;
                currentY = subpathY;
                previousWasCubic = false;
                previousWasQuadratic = false;
                command = 0;
            }
            continue;
        }
        const size_t parameterCount = pathParameterCount(command);
        if(parameterCount == 0)
            return {};
        std::array<long double, 7> values{};
        for(size_t parameter = 0; parameter < parameterCount; ++parameter) {
            while(cursor < end
                    && (std::isspace(static_cast<unsigned char>(*cursor))
                        || *cursor == ','))
                ++cursor;
            if(cursor == end
                    || std::isalpha(static_cast<unsigned char>(*cursor)))
                return {};
            char* parsedEnd = nullptr;
            values[parameter] = std::strtold(cursor, &parsedEnd);
            if(parsedEnd == cursor || parsedEnd > end
                    || !std::isfinite(values[parameter]))
                return {};
            cursor = parsedEnd;
        }
        const bool relative = command >= 'a' && command <= 'z';
        const char normalized = static_cast<char>(std::tolower(
            static_cast<unsigned char>(command)));
        const auto xValue = [&](size_t parameter) {
            return values[parameter] + (relative ? currentX : 0);
        };
        const auto yValue = [&](size_t parameter) {
            return values[parameter] + (relative ? currentY : 0);
        };
        if(normalized == 'm') {
            if(firstMoveGroup) {
                finishSubpath();
                currentX = xValue(0);
                currentY = yValue(1);
                subpathX = currentX;
                subpathY = currentY;
                tracker.include(currentX, currentY);
                firstMoveGroup = false;
                command = relative ? 'l' : 'L';
            } else {
                currentX = xValue(0);
                currentY = yValue(1);
                tracker.include(currentX, currentY);
            }
            previousWasCubic = false;
            previousWasQuadratic = false;
        } else if(normalized == 'l') {
            currentX = xValue(0);
            currentY = yValue(1);
            tracker.include(currentX, currentY);
            previousWasCubic = false;
            previousWasQuadratic = false;
        } else if(normalized == 'h') {
            currentX = values[0] + (relative ? currentX : 0);
            tracker.include(currentX, currentY);
            previousWasCubic = false;
            previousWasQuadratic = false;
        } else if(normalized == 'v') {
            currentY = values[0] + (relative ? currentY : 0);
            tracker.include(currentX, currentY);
            previousWasCubic = false;
            previousWasQuadratic = false;
        } else if(normalized == 'c') {
            const long double control1X = xValue(0);
            const long double control1Y = yValue(1);
            const long double control2X = xValue(2);
            const long double control2Y = yValue(3);
            const long double endX = xValue(4);
            const long double endY = yValue(5);
            tracker.include(control1X, control1Y);
            tracker.include(control2X, control2Y);
            tracker.include(endX, endY);
            currentX = endX;
            currentY = endY;
            previousCubicX = control2X;
            previousCubicY = control2Y;
            previousWasCubic = true;
            previousWasQuadratic = false;
        } else if(normalized == 's') {
            const long double control1X = previousWasCubic
                ? currentX * 2 - previousCubicX : currentX;
            const long double control1Y = previousWasCubic
                ? currentY * 2 - previousCubicY : currentY;
            const long double control2X = xValue(0);
            const long double control2Y = yValue(1);
            const long double endX = xValue(2);
            const long double endY = yValue(3);
            tracker.include(control1X, control1Y);
            tracker.include(control2X, control2Y);
            tracker.include(endX, endY);
            currentX = endX;
            currentY = endY;
            previousCubicX = control2X;
            previousCubicY = control2Y;
            previousWasCubic = true;
            previousWasQuadratic = false;
        } else if(normalized == 'q') {
            previousQuadraticX = xValue(0);
            previousQuadraticY = yValue(1);
            tracker.include(previousQuadraticX, previousQuadraticY);
            currentX = xValue(2);
            currentY = yValue(3);
            tracker.include(currentX, currentY);
            previousWasCubic = false;
            previousWasQuadratic = true;
        } else if(normalized == 't') {
            previousQuadraticX = previousWasQuadratic
                ? currentX * 2 - previousQuadraticX : currentX;
            previousQuadraticY = previousWasQuadratic
                ? currentY * 2 - previousQuadraticY : currentY;
            tracker.include(previousQuadraticX, previousQuadraticY);
            currentX = xValue(0);
            currentY = yValue(1);
            tracker.include(currentX, currentY);
            previousWasCubic = false;
            previousWasQuadratic = true;
        } else if(normalized == 'a') {
            const long double endX = xValue(5);
            const long double endY = yValue(6);
            if(std::abs(values[0]) > 0 && std::abs(values[1]) > 0
                    && (endX != currentX || endY != currentY))
                tracker.markAreaPotential();
            currentX = endX;
            currentY = endY;
            tracker.include(currentX, currentY);
            previousWasCubic = false;
            previousWasQuadratic = false;
        } else {
            return {};
        }
    }
    finishSubpath();
    return result;
}

} // namespace lumina_svg_core::recorder
