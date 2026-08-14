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

// Lays out a laid-out (x/y/w/h already set) scroll_view's single child at
// its natural height inside the viewport, clamps `offset_y` to the
// scrollable range, and bakes the content-box origin minus the offset into
// the content subtree's absolute coordinates. Returns the content height.
// The caller (Host) owns the per-path scroll offset across frames.
float layout_scroll_view(Element& sv, float& offset_y, const LayoutContext& ctx) noexcept;

} // namespace flux
