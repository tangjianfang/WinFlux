#pragma once

// WinFlux damage tracking — a pure CPU, headless-testable diff between two
// scene trees. Given the previously laid-out tree and the newly laid-out
// tree, it produces a list of dirty rectangles (logical units) that the
// Host hands to the Presenter for partial repaint.
//
// The diff compares nodes path-by-path (type + layout rect + paint-relevant
// props). For each changed node the previous and the new bounding box are
// both added to the dirty set, so a child that moved will repaint both its
// old and its new position. Overlapping dirty rects are unioned and the
// result is inflated by kAAInflate to leave room for antialiasing seams.
// If the root node changed or the final rect count exceeds max_rects the
// result collapses to a single full-screen rect.

#include <vector>

#include "Element.hpp"
#include "Presenter.hpp"   // Rect

namespace flux {

struct DamageTracker {
    unsigned max_rects = 8;
    static constexpr float kAAInflate = 1.0f;

    // Three paths whose old + new rects should be added to the dirty set when
    // they differ between frames. The Diff alone can't see hover/press/focus
    // because those are interaction state, not element-tree state; without
    // this, handlers would have to force a fullscreen repaint on every
    // interaction (the altitude trap we're trying to escape).
    struct InteractionState {
        using Path = std::vector<int>;
        Path hover;
        Path pressed;
        Path focus;
    };

    // canvas_w/h are the client area in logical units (used for the
    // fullscreen fallback and to clamp negatives). prev_interaction /
    // next_interaction are optional; when supplied and any of the three
    // paths differ, both the old (resolved in prev) and the new (resolved in
    // next) element rects are added to the dirty set.
    [[nodiscard]] std::vector<Rect> compute(const Element* prev,
                                            const Element* next,
                                            float canvas_w,
                                            float canvas_h,
                                            const InteractionState* prev_interaction = nullptr,
                                            const InteractionState* next_interaction = nullptr) const noexcept;

    // Resolve a child-index path against a tree root. Returns nullptr on
    // out-of-range indices or an empty root.
    [[nodiscard]] static const Element* resolve_path(const Element* root,
                                                     const InteractionState::Path& path) noexcept;

    // Compare paint-relevant Props fields. Layout-only fields (flex_grow,
    // direction, padding, margin, gap, align) are not compared here because
    // a layout-only change is already reflected by different x/y/w/h on the
    // elements themselves (computed by flex_layout).
    [[nodiscard]] static bool paint_props_equal(const Props& a,
                                                const Props& b) noexcept;
};

} // namespace flux