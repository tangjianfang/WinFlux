#pragma once

// WinFlux flex layout engine. Single main-axis pass per node (no flex-wrap)
// supporting fixed sizes, content measurement, flex_grow distribution, gap,
// padding, margin, and cross-axis alignment (start/center/end/stretch).
// All coordinates are logical px at the 96 DPI baseline.

#include <functional>

#include "Element.hpp"

namespace flux {

struct LayoutContext {
    // Measures leaf content (text glyphs) in logical px -> {width, height}.
    // Containers without a fixed main-axis size are measured recursively.
    std::function<std::pair<float, float>(const Element&)> measure_leaf;
};

void flex_layout(Element& root, float width, float height, const LayoutContext& ctx) noexcept;

} // namespace flux
