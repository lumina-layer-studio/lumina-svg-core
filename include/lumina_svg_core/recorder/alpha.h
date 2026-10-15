#ifndef LUMINA_SVG_CORE_RECORDER_ALPHA_H
#define LUMINA_SVG_CORE_RECORDER_ALPHA_H

#include <optional>
#include <string_view>

namespace lumina_svg_core::recorder {

struct AlphaSemantics {
    bool zero{false};
    bool partial{false};
};

double parseRecordingNumberOrPercentage(
    std::string_view input, bool allowPercentage, double defaultValue);
bool recordingAsciiEqualsIgnoreCase(std::string_view left, std::string_view right);
double recordingColorAlpha(std::string_view input, bool validColor,
                           float parsedAlpha);
AlphaSemantics numberAlphaSemantics(std::string_view input);
// Decimal source classification must survive display-float underflow/rounding.
bool isSourcePartialAlpha(std::string_view input);
// A missing result requests the bridge's public PlutoVG color validation.
std::optional<AlphaSemantics> sourceColorAlphaSemantics(
    std::string_view input, AlphaSemantics currentColor);

} // namespace lumina_svg_core::recorder

#endif
