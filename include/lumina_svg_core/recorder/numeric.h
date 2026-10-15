#ifndef LUMINA_SVG_CORE_RECORDER_NUMERIC_H
#define LUMINA_SVG_CORE_RECORDER_NUMERIC_H

#include <string_view>
#include <vector>

namespace lumina_svg_core::recorder {

// Preserve source doubles alongside LunaSVG's resolved display-float lengths.
double exactRecordingNumber(std::string_view input, double fallback);
double exactRecordingLength(std::string_view input, double parsedLengthValue,
                            double resolvedFallback);
std::vector<std::string_view> recordingLengthTokens(std::string_view input);

enum class ExactLengthUnits { Absolute, Em, Ex, Percent };

// Relative units are resolved by the DOM bridge using the current layout.
bool parseExactLength(std::string_view input, double defaultValue, bool required,
                      double& value, ExactLengthUnits& units);

} // namespace lumina_svg_core::recorder

#endif
