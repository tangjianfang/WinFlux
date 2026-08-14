#include <flux/FlexLayout.hpp>

#include <algorithm>
#include <unordered_map>

namespace flux {

namespace {

constexpr float kMinSize = 0.0f;

bool is_leaf(const Element& e) noexcept { return e.children.empty(); }

// Per-node measure cache. Keys by Element address — valid for the duration of
// a single flex_layout call because the tree isn't mutated mid-pass. Caches
// the (main, cross) intrinsic pair so cross-axis lookups in the same pass
// don't trigger another recursive measure of the same subtree.
using MeasureKey = const Element*;
struct MeasureValue { float main; float cross; };
using MeasureCache = std::unordered_map<MeasureKey, MeasureValue>;

std::pair<float, float> measure_impl(const Element& e, const LayoutContext& ctx,
                                     MeasureCache& cache) noexcept {
    const auto it = cache.find(&e);
    if (it != cache.end()) return {it->second.main, it->second.cross};

    const EdgeInsets& pad = e.props.padding;
    std::pair<float, float> result;
    if (e.type == L"scroll_view") {
        // The viewport is sized by Props / parent flex allocation only —
        // the content never leaks into the parent's measurement (the Host
        // lays the content out separately with an unbounded canvas).
        result = {pad.left + pad.right + e.props.width.value_or(0.0f),
                  pad.top + pad.bottom + e.props.height.value_or(0.0f)};
    } else if (is_leaf(e)) {
        auto [tw, th] = ctx.measure_leaf ? ctx.measure_leaf(e) : std::pair<float, float>{0.0f, 0.0f};
        result = {tw + pad.left + pad.right, th + pad.top + pad.bottom};
    } else {
        float main = 0.0f;
        float cross = 0.0f;
        const bool row = e.props.direction == Direction::row;
        const float gap = e.props.gap;
        for (size_t i = 0; i < e.children.size(); ++i) {
            const Element& c = *e.children[i];
            const EdgeInsets& m = c.props.margin;
            auto [cw, ch] = measure_impl(c, ctx, cache);
            const float cmain = row ? cw + m.left + m.right : ch + m.top + m.bottom;
            const float ccross = row ? ch + m.top + m.bottom : cw + m.left + m.right;
            main += cmain + (i > 0 ? gap : 0.0f);
            cross = std::max(cross, ccross);
        }
        result = row ? std::pair{main + pad.left + pad.right, cross + pad.top + pad.bottom}
                     : std::pair{cross + pad.left + pad.right, main + pad.top + pad.bottom};
        // Non-leaf containers with explicit width / height (e.g. the
        // 32x32 avatar circles) still measure from their children if we
        // don't override here — the children's intrinsic sizes then leak
        // up and collapse the parent. Prefer the explicit dimension when
        // set; keep the other axis from the children.
        if (e.props.width.has_value())  result.first  = *e.props.width  + pad.left + pad.right;
        if (e.props.height.has_value()) result.second = *e.props.height + pad.top  + pad.bottom;
    }
    cache.emplace(&e, MeasureValue{result.first, result.second});
    return result;
}

float fixed_or_content_main(const Element& e, bool row, const LayoutContext& ctx,
                            MeasureCache& cache) noexcept {
    const auto& fixed = row ? e.props.width : e.props.height;
    if (fixed.has_value()) return *fixed;
    auto [mw, mh] = measure_impl(e, ctx, cache);
    return row ? mw : mh;
}

float fixed_or_content_cross(const Element& e, bool row, const LayoutContext& ctx,
                             MeasureCache& cache) noexcept {
    const auto& fixed = row ? e.props.height : e.props.width;
    if (fixed.has_value()) return *fixed;
    auto [mw, mh] = measure_impl(e, ctx, cache);
    return row ? mh : mw;
}

void layout_node(Element& e, float ox, float oy, float w, float h,
                 const LayoutContext& ctx, MeasureCache& cache) noexcept {
    e.x = ox;
    e.y = oy;
    e.w = w;
    e.h = h;
    // scroll_view children are NOT laid out here — the content box gets an
    // unbounded layout pass in layout_scroll_view (Host-driven, after the
    // viewport position is known and the scroll offset can be applied).
    if (e.type == L"scroll_view") return;
    if (e.children.empty()) return;

    const Props& p = e.props;
    const bool row = p.direction == Direction::row;
    const float cx = ox + p.padding.left;
    const float cy = oy + p.padding.top;
    const float cw = std::max(kMinSize, w - p.padding.left - p.padding.right);
    const float ch = std::max(kMinSize, h - p.padding.top - p.padding.bottom);
    const float avail_main = row ? cw : ch;
    const float avail_cross = row ? ch : cw;

    // Pass 1: desired main sizes.
    const size_t n = e.children.size();
    std::vector<float> mains(n);
    float total = 0.0f;
    float grow_sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        Element& c = *e.children[i];
        const EdgeInsets& m = c.props.margin;
        const float margin_main = row ? m.left + m.right : m.top + m.bottom;
        mains[i] = std::max(kMinSize, fixed_or_content_main(c, row, ctx, cache) + margin_main);
        total += mains[i];
        grow_sum += c.props.flex_grow;
    }
    total += p.gap * static_cast<float>(n > 0 ? n - 1 : 0);

    // Pass 2: distribute free space (grow) or shrink proportionally.
    const float free_space = avail_main - total;
    bool grew = false;
    if (free_space > 0.0f && grow_sum > 0.0f) {
        for (size_t i = 0; i < n; ++i) {
            mains[i] += free_space * (e.children[i]->props.flex_grow / grow_sum);
        }
        grew = true;
    } else if (free_space < 0.0f && total > 0.0f) {
        const float scale = std::max(0.0f, avail_main / total);
        for (size_t i = 0; i < n; ++i) mains[i] *= scale;
    }

    // Pass 3: position children along main axis; resolve cross axis. When
    // nothing grew, justify packs the pack along the main axis
    // (start/center/end); flex_grow absorbs all free space and wins.
    float cursor = 0.0f;
    if (!grew && free_space > 0.0f) {
        switch (p.justify) {
        case Justify::center: cursor = free_space * 0.5f; break;
        case Justify::end:    cursor = free_space;        break;
        case Justify::start:
        default:              break;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        Element& c = *e.children[i];
        const EdgeInsets& m = c.props.margin;
        const float margin_main_start = row ? m.left : m.top;
        const float margin_main_end = row ? m.right : m.bottom;
        const float margin_cross_start = row ? m.top : m.left;
        const float margin_cross_end = row ? m.bottom : m.right;

        const float main_size = std::max(kMinSize, mains[i] - margin_main_start - margin_main_end);
        const bool stretch = p.align == Align::stretch && !(row ? c.props.height : c.props.width).has_value();
        const float cross_avail = avail_cross - margin_cross_start - margin_cross_end;
        const float cross_size = stretch
            ? std::max(kMinSize, cross_avail)
            : std::max(kMinSize, fixed_or_content_cross(c, row, ctx, cache));

        const float main_pos = cursor + margin_main_start;
        float cross_pos = margin_cross_start;
        switch (p.align) {
        case Align::center:
            cross_pos = margin_cross_start + (avail_cross - margin_cross_start - margin_cross_end - cross_size) * 0.5f;
            break;
        case Align::end:
            cross_pos = avail_cross - margin_cross_end - cross_size;
            break;
        case Align::start:
        case Align::stretch:
        default:
            break;
        }

        const float child_x = row ? cx + main_pos : cx + cross_pos;
        const float child_y = row ? cy + cross_pos : cy + main_pos;
        const float child_w = row ? main_size : cross_size;
        const float child_h = row ? cross_size : main_size;
        layout_node(c, child_x, child_y, child_w, child_h, ctx, cache);

        cursor += mains[i] + p.gap;
    }
}

} // namespace

void flex_layout(Element& root, float width, float height, const LayoutContext& ctx) noexcept {
    MeasureCache cache;
    layout_node(root, 0.0f, 0.0f, width, height, ctx, cache);
}

float layout_scroll_view(Element& sv, float& offset_y, const LayoutContext& ctx) noexcept {
    if (sv.children.empty()) {
        offset_y = 0.0f;
        return 0.0f;
    }
    constexpr float kUnbounded = 1.0e6f;
    const Props& p = sv.props;
    const float viewport_w = std::max(0.0f, sv.w - p.padding.left - p.padding.right);
    const float viewport_h = std::max(0.0f, sv.h - p.padding.top - p.padding.bottom);

    // Unbounded vertical canvas so the content keeps its natural height
    // (nothing shrinks, nothing grows).
    Element& content = *sv.children[0];
    MeasureCache cache;
    layout_node(content, 0.0f, 0.0f, viewport_w, kUnbounded, ctx, cache);

    float content_h = content.props.height.value_or(0.0f) + content.props.padding.top +
                      content.props.padding.bottom;
    if (!content.children.empty()) {
        const Element& last = *content.children.back();
        content_h = std::max(content_h, last.y + last.h + content.props.padding.bottom);
    }

    offset_y = std::clamp(offset_y, 0.0f, std::max(0.0f, content_h - viewport_h));

    // Bake the content-box origin (viewport top minus the scroll offset)
    // into the content's absolute coordinates so the generic diff / clip /
    // hit-test pipeline works with zero special cases.
    const float dx = sv.x + p.padding.left;
    const float dy = sv.y + p.padding.top - offset_y;
    std::vector<Element*> stack{&content};
    while (!stack.empty()) {
        Element* n = stack.back();
        stack.pop_back();
        n->x += dx;
        n->y += dy;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return content_h;
}

} // namespace flux
