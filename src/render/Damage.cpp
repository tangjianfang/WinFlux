#include <flux/Damage.hpp>

#include <algorithm>
#include <optional>

namespace flux {

namespace {

Rect rect_of(const Element& e) noexcept { return Rect{e.x, e.y, e.w, e.h}; }

bool rect_valid(const Rect& r) noexcept { return r.w > 0.0f && r.h > 0.0f; }

bool rects_overlap(const Rect& a, const Rect& b) noexcept {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

void rect_union(Rect& a, const Rect& b) noexcept {
    const float x1 = std::min(a.x, b.x);
    const float y1 = std::min(a.y, b.y);
    const float x2 = std::max(a.x + a.w, b.x + b.w);
    const float y2 = std::max(a.y + a.h, b.y + b.h);
    a = Rect{x1, y1, x2 - x1, y2 - y1};
}

void inflate(Rect& r, float dx) noexcept {
    r.x -= dx;
    r.y -= dx;
    r.w += 2.0f * dx;
    r.h += 2.0f * dx;
}

// Walk both subtrees in parallel by index. Collects raw dirty rects (both
// old + new per changed node). Does not mutate the inputs.
void diff_subtree(const Element* prev, const Element* next,
                  std::vector<Rect>& out) noexcept {
    if (prev == nullptr && next == nullptr) return;

    // One side missing entirely -> the existing side is a complete change.
    if (prev == nullptr) {
        if (rect_valid(rect_of(*next))) out.push_back(rect_of(*next));
        for (const auto& c : next->children) diff_subtree(nullptr, c.get(), out);
        return;
    }
    if (next == nullptr) {
        if (rect_valid(rect_of(*prev))) out.push_back(rect_of(*prev));
        for (const auto& c : prev->children) diff_subtree(c.get(), nullptr, out);
        return;
    }

    // Both present: node-level diff.
    const bool changed_type = (prev->type != next->type);
    const bool changed_rect = (prev->x != next->x || prev->y != next->y ||
                               prev->w != next->w || prev->h != next->h);
    const bool changed_props = !DamageTracker::paint_props_equal(prev->props, next->props);

    if (changed_type || changed_rect || changed_props) {
        if (rect_valid(rect_of(*prev))) out.push_back(rect_of(*prev));
        if (rect_valid(rect_of(*next))) out.push_back(rect_of(*next));
    }

    // Walk children in parallel up to the shorter side; the tail beyond the
    // shorter side is treated as additions/removals.
    const size_t common = std::min(prev->children.size(), next->children.size());
    for (size_t i = 0; i < common; ++i) {
        diff_subtree(prev->children[i].get(), next->children[i].get(), out);
    }
    for (size_t i = common; i < prev->children.size(); ++i) {
        diff_subtree(prev->children[i].get(), nullptr, out);
    }
    for (size_t i = common; i < next->children.size(); ++i) {
        diff_subtree(nullptr, next->children[i].get(), out);
    }
}

// Repeatedly merge any pair of overlapping rects until none remain. O(n^2)
// which is fine for the small dirty sets this tracker produces.
void merge_overlapping(std::vector<Rect>& rects) noexcept {
    bool changed = true;
    while (changed && rects.size() > 1) {
        changed = false;
        for (size_t i = 0; i < rects.size(); ++i) {
            for (size_t j = i + 1; j < rects.size(); ++j) {
                if (rects_overlap(rects[i], rects[j])) {
                    rect_union(rects[i], rects[j]);
                    rects.erase(rects.begin() + j);
                    changed = true;
                    break;
                }
            }
            if (changed) break;
        }
    }
}

} // namespace

bool DamageTracker::paint_props_equal(const Props& a, const Props& b) noexcept {
    if (a.background != b.background) return false;
    if (a.hover_background != b.hover_background) return false;
    if (a.pressed_background != b.pressed_background) return false;
    if (a.disabled_background != b.disabled_background) return false;
    if (a.border_color != b.border_color) return false;
    if (a.border_width != b.border_width) return false;
    if (a.radius != b.radius) return false;
    if (a.text != b.text) return false;
    if (a.font_size_pt != b.font_size_pt) return false;
    if (a.text_color != b.text_color) return false;
    if (a.disabled_text_color != b.disabled_text_color) return false;
    if (a.bold != b.bold) return false;
    if (a.text_align != b.text_align) return false;
    if (a.checked != b.checked) return false;
    if (a.progress_value != b.progress_value) return false;
    if (a.slider_value != b.slider_value) return false;
    if (a.slider_min != b.slider_min) return false;
    if (a.slider_max != b.slider_max) return false;
    if (a.enabled != b.enabled) return false;
    if (a.focusable != b.focusable) return false;
    return true;
}

const Element* DamageTracker::resolve_path(const Element* root,
                                           const InteractionState::Path& path) noexcept {
    if (root == nullptr) return nullptr;
    const Element* node = root;
    for (const int index : path) {
        if (index < 0 || static_cast<size_t>(index) >= node->children.size()) return nullptr;
        node = node->children[static_cast<size_t>(index)].get();
    }
    return node;
}

namespace {
void add_interaction_rects(const Element* prev, const Element* next,
                           const DamageTracker::InteractionState::Path& old_path,
                           const DamageTracker::InteractionState::Path& new_path,
                           std::vector<Rect>& out) noexcept {
    if (old_path == new_path) return;
    if (const Element* e = DamageTracker::resolve_path(prev, old_path);
        e != nullptr && rect_valid(rect_of(*e))) {
        out.push_back(rect_of(*e));
    }
    if (const Element* e = DamageTracker::resolve_path(next, new_path);
        e != nullptr && rect_valid(rect_of(*e))) {
        out.push_back(rect_of(*e));
    }
}
} // namespace

std::vector<Rect> DamageTracker::compute(const Element* prev,
                                         const Element* next,
                                         float canvas_w,
                                         float canvas_h,
                                         const InteractionState* prev_interaction,
                                         const InteractionState* next_interaction) const noexcept {
    const Rect full{0.0f, 0.0f,
                    canvas_w > 0.0f ? canvas_w : 0.0f,
                    canvas_h > 0.0f ? canvas_h : 0.0f};

    // First frame (no previous tree) or empty new tree -> full repaint.
    if (prev == nullptr || next == nullptr) {
        if (canvas_w <= 0.0f || canvas_h <= 0.0f) return {};
        return std::vector<Rect>{full};
    }

    // Root structural change -> full repaint.
    if (prev->type != next->type) return std::vector<Rect>{full};

    // Same-tree fast-path. Pointer equality means the two walks would visit
    // the same nodes at the same addresses — guaranteed to find no structural
    // difference. Only interaction state (hover/press/focus) can still produce
    // dirty rects, so skip diff_subtree entirely and check just the paths.
    if (prev == next) {
        const bool interaction_changed =
            prev_interaction != nullptr && next_interaction != nullptr &&
            (prev_interaction->hover   != next_interaction->hover   ||
             prev_interaction->pressed != next_interaction->pressed ||
             prev_interaction->focus   != next_interaction->focus);
        if (!interaction_changed) return {};

        std::vector<Rect> raw;
        add_interaction_rects(prev, next, prev_interaction->hover,  next_interaction->hover,  raw);
        add_interaction_rects(prev, next, prev_interaction->pressed, next_interaction->pressed, raw);
        add_interaction_rects(prev, next, prev_interaction->focus,  next_interaction->focus,  raw);
        if (raw.empty()) return raw;
        merge_overlapping(raw);
        if (raw.size() > max_rects) return std::vector<Rect>{full};
        for (auto& r : raw) inflate(r, kAAInflate);
        return raw;
    }

    std::vector<Rect> raw;
    diff_subtree(prev, next, raw);

    // Interaction diff: hover/press/focus are state, not part of the element
    // tree, so the structural diff above won't catch them. Resolve the OLD
    // path against the PREVIOUS tree and the NEW path against the NEXT tree,
    // push both rects (if valid) into the dirty set so the renderer can see
    // the highlight changes.
    if (prev_interaction != nullptr && next_interaction != nullptr) {
        add_interaction_rects(prev, next, prev_interaction->hover,  next_interaction->hover,  raw);
        add_interaction_rects(prev, next, prev_interaction->pressed, next_interaction->pressed, raw);
        add_interaction_rects(prev, next, prev_interaction->focus,  next_interaction->focus,  raw);
    }

    // No changes at all -> empty dirty set; caller can skip the draw.
    if (raw.empty()) return raw;

    merge_overlapping(raw);

    if (raw.size() > max_rects) return std::vector<Rect>{full};

    for (auto& r : raw) inflate(r, kAAInflate);
    return raw;
}

} // namespace flux