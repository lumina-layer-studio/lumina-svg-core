#ifndef LUMINA_SVG_CORE_RECORDER_TRANSFORM_H
#define LUMINA_SVG_CORE_RECORDER_TRANSFORM_H

#include <lunasvg.h>

#include <cstdint>
#include <string_view>

namespace lumina_svg_core::recorder {

struct TransformPrecisionEstimate {
    double linear{0};
    double translation{0};
};

uint32_t computeTransformOperationCount(std::string_view value);
TransformPrecisionEstimate computeTransformPrecisionEstimate(std::string_view value);
lunasvg::PaintTransform composeRecordingTransforms(
    const lunasvg::PaintTransform& parent, const lunasvg::PaintTransform& local);
bool inverseTransform(const lunasvg::PaintTransform& value,
                      lunasvg::PaintTransform& inverse);
bool parseExactTransform(std::string_view input, lunasvg::PaintTransform& result);

} // namespace lumina_svg_core::recorder

#endif
