#ifndef LUMINA_SVG_CORE_RECORDER_CAPABILITY_H
#define LUMINA_SVG_CORE_RECORDER_CAPABILITY_H

#include <string>
#include <string_view>

namespace lumina_svg_core::recorder {

struct AttributeCapability {
    std::string reason;
    bool needsColorValidation{false};
};

bool hasPrefix(std::string_view value, std::string_view prefix);
bool isUnimplementedGeometryCssProperty(std::string_view name);
bool isDeferredCssCapabilityProperty(std::string_view name, bool knownCssProperty);
AttributeCapability classifyAttributeCapability(
    std::string_view name, std::string_view value, bool known,
    bool isStyleElement, bool isImageElement, bool cssDeclaration);

} // namespace lumina_svg_core::recorder

#endif
