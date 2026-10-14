#ifndef LUMINA_SVG_CORE_RECORDER_EXACT_PATH_H
#define LUMINA_SVG_CORE_RECORDER_EXACT_PATH_H

#include <lunasvg.h>

#include <string_view>

namespace lumina_svg_core::recorder {

void appendExactMove(lunasvg::PaintCommand& command, double x, double y);
void appendExactLine(lunasvg::PaintCommand& command, double x, double y);
bool appendExactEllipse(lunasvg::PaintCommand& command,
                        double centerX, double centerY,
                        double radiusX, double radiusY,
                        double& loweringPrecisionError);
bool appendExactRect(lunasvg::PaintCommand& command,
                     double x, double y, double width, double height,
                     double radiusX, double radiusY,
                     bool hasRadiusX, bool hasRadiusY,
                     bool& containsArc, double& loweringPrecisionError);
bool recordExactPointList(std::string_view input, bool closed,
                          lunasvg::PaintCommand& command);
bool recordExactPath(std::string_view input, lunasvg::PaintCommand& command,
                     bool& containsArc, double& loweringPrecisionError);

} // namespace lumina_svg_core::recorder

#endif
