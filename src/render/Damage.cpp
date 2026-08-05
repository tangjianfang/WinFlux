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
    if (a.enabled != b.enabled) return false;
    if (a.focusable != b.focusable) return false;
    return true;
}

std::vector<Rect> DamageTracker::compute(const Element* prev,
                                         const Element* next,
                                         float canvas_w,
                                         float canvas_h) const noexcept {
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

    std::vector<Rect> raw;
    diff_subtree(prev, next, raw);

    // No changes at all -> empty dirty set; caller can skip the draw.
    if (raw.empty()) return raw;

    merge_overlapping(raw);

    if (raw.size() > max_rects) return std::vector<Rect>{full};

    for (auto& r : raw) inflate(r, kAAInflate);
    return raw;
}

} // namespace flux