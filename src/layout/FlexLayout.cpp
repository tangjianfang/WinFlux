#include <flux/FlexLayout.hpp>

#include <algorithm>

namespace flux {

namespace {

constexpr float kMinSize = 0.0f;

bool is_leaf(const Element& e) noexcept { return e.children.empty(); }

// Recursive content measurement (logical px). Leaves consult the text
// measurer; containers sum their children along their own main axis.
std::pair<float, float> measure(const Element& e, const LayoutContext& ctx) noexcept {
    const EdgeInsets& pad = e.props.padding;
    if (is_leaf(e)) {
        auto [tw, th] = ctx.measure_leaf ? ctx.measure_leaf(e) : std::pair<float, float>{0.0f, 0.0f};
        return {tw + pad.left + pad.right, th + pad.top + pad.bottom};
    }

    float main = 0.0f;
    float cross = 0.0f;
    const bool row = e.props.direction == Direction::row;
    const float gap = e.props.gap;
    for (size_t i = 0; i < e.children.size(); ++i) {
        const Element& c = *e.children[i];
        const EdgeInsets& m = c.props.margin;
        auto [cw, ch] = measure(c, ctx);
        const float cmain = row ? cw + m.left + m.right : ch + m.top + m.bottom;
        const float ccross = row ? ch + m.top + m.bottom : cw + m.left + m.right;
        main += cmain + (i > 0 ? gap : 0.0f);
        cross = std::max(cross, ccross);
    }
    return row ? std::pair{main + pad.left + pad.right, cross + pad.top + pad.bottom}
               : std::pair{cross + pad.left + pad.right, main + pad.top + pad.bottom};
}

float fixed_or_content_main(const Element& e, bool row, const LayoutContext& ctx) noexcept {
    const auto& fixed = row ? e.props.width : e.props.height;
    if (fixed.has_value()) return *fixed;
    auto [mw, mh] = measure(e, ctx);
    return row ? mw : mh;
}

float fixed_or_content_cross(const Element& e, bool row, const LayoutContext& ctx) noexcept {
    const auto& fixed = row ? e.props.height : e.props.width;
    if (fixed.has_value()) return *fixed;
    auto [mw, mh] = measure(e, ctx);
    return row ? mh : mw;
}

void layout_node(Element& e, float ox, float oy, float w, float h, const LayoutContext& ctx) noexcept {
    e.x = ox;
    e.y = oy;
    e.w = w;
    e.h = h;
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
        mains[i] = std::max(kMinSize, fixed_or_content_main(c, row, ctx) + margin_main);
        total += mains[i];
        grow_sum += c.props.flex_grow;
    }
    total += p.gap * static_cast<float>(n > 0 ? n - 1 : 0);

    // Pass 2: distribute free space (grow) or shrink proportionally.
    const float free_space = avail_main - total;
    if (free_space > 0.0f && grow_sum > 0.0f) {
        for (size_t i = 0; i < n; ++i) {
            mains[i] += free_space * (e.children[i]->props.flex_grow / grow_sum);
        }
    } else if (free_space < 0.0f && total > 0.0f) {
        const float scale = std::max(0.0f, avail_main / total);
        for (size_t i = 0; i < n; ++i) mains[i] *= scale;
    }

    // Pass 3: position children along main axis; resolve cross axis.
    float cursor = 0.0f;
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
            : std::max(kMinSize, fixed_or_content_cross(c, row, ctx));

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
        layout_node(c, child_x, child_y, child_w, child_h, ctx);

        cursor += mains[i] + p.gap;
    }
}

} // namespace

void flex_layout(Element& root, float width, float height, const LayoutContext& ctx) noexcept {
    layout_node(root, 0.0f, 0.0f, width, height, ctx);
}

} // namespace flux
