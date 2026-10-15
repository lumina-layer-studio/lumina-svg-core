#ifndef LUMINA_SVG_CORE_RECORDER_PATH_PRECISION_H
#define LUMINA_SVG_CORE_RECORDER_PATH_PRECISION_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace lumina_svg_core::recorder {

struct PathParseWork {
    size_t verbs{0};
    size_t points{0};
    bool valid{true};
    bool exceeded{false};
};

struct PointListParseWork {
    size_t points{0};
    bool valid{true};
    bool exceeded{false};
};

uint32_t computeRelativePathOperationCount(std::string_view value);
std::vector<uint8_t> computeSourceSubpathAreaPotential(std::string_view value);
PathParseWork computePathParseWork(std::string_view value);
PointListParseWork computePointListParseWork(std::string_view input);
double computePathInputPrecisionError(
    std::string_view value, bool& strokeDirectionAmbiguous,
    bool& isolatedStrokeDirectionAmbiguous,
    double& maximumErrorX, double& maximumErrorY,
    bool& sourceHasLoweredCurve);

} // namespace lumina_svg_core::recorder

#endif
