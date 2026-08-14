// flux::ui charts — line / area / pie / sparkline / gauge built on the
// Props::paint escape hatch. Geometry helpers are pure functions so the
// smoke test can verify normalization and slice math headlessly.

#include <flux/Components.hpp>

#include <flux/D2DRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flux::ui {

// ---- Geometry (pure, headless-testable) ----------------------------------

std::pair<float, float> value_range(const std::vector<float>& values) noexcept {
    if (values.empty()) return {0.0f, 1.0f};
    float lo = values.front();
    float hi = values.front();
    for (float v : values) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    if (hi - lo < 1.0e-6f) {
        // Flat series: pad so the line sits in the vertical middle.
        lo -= 1.0f;
        hi += 1.0f;
    }
    return {lo, hi};
}

std::vector<std::pair<float, float>> line_points(const std::vector<float>& values,
                                                 float x, float y, float w,
                                                 float h) noexcept {
    std::vector<std::pair<float, float>> pts;
    if (values.empty() || w <= 0.0f || h <= 0.0f) return pts;
    const auto [lo, hi] = value_range(values);
    const float step = values.size() > 1 ? w / static_cast<float>(values.size() - 1) : 0.0f;
    const float pad = 4.0f;   // keep the stroke inside the box
    const float usable = h - 2.0f * pad;
    for (size_t i = 0; i < values.size(); ++i) {
        const float t = (values[i] - lo) / (hi - lo);
        pts.push_back({x + step * static_cast<float>(i),
                       y + pad + (1.0f - t) * usable});
    }
    return pts;
}

std::vector<PieSlice> pie_slices(const std::vector<float>& values) noexcept {
    std::vector<PieSlice> out;
    float total = 0.0f;
    for (float v : values) total += std::max(0.0f, v);
    if (total <= 0.0f) return out;
    float cursor = 0.0f;
    for (float v : values) {
        const float frac = std::max(0.0f, v) / total;
        if (frac <= 0.0f) continue;
        out.push_back(PieSlice{cursor * 360.0f, frac * 360.0f});
        cursor += frac;
    }
    return out;
}

std::vector<std::pair<float, float>> arc_points(float cx, float cy, float r,
                                                float start_deg, float sweep_deg,
                                                float step_deg) noexcept {
    std::vector<std::pair<float, float>> pts;
    constexpr float kPi = 3.14159265358979f;
    const int steps = std::max(2, static_cast<int>(std::abs(sweep_deg) / step_deg) + 1);
    for (int i = 0; i <= steps; ++i) {
        const float deg = start_deg + sweep_deg * static_cast<float>(i) / static_cast<float>(steps);
        const float rad = deg * kPi / 180.0f;
        pts.push_back({cx + r * std::sin(rad), cy - r * std::cos(rad)});
    }
    return pts;
}

namespace {

Color series_color(const Palette& pal, size_t i) noexcept {
    switch (i % 5) {
    case 0: return pal.accent;
    case 1: return pal.success;
    case 2: return pal.warning;
    case 3: return pal.danger;
    default: return pal.info;
    }
}

// Stable-ish id for the paint diff: mixes size + first/last + sum.
unsigned series_id(const std::vector<float>& values) noexcept {
    unsigned h = 2166136261u;
    for (float v : values) {
        const auto bits = static_cast<unsigned>(std::fabs(v) * 100.0f);
        h = (h ^ bits) * 16777619u;
    }
    return h ^ static_cast<unsigned>(values.size()) * 2654435761u;
}

} // namespace

// ---- Components ------------------------------------------------------------

ElementPtr line_chart(const Palette& pal, std::vector<float> values, Props p) {
    Props vp = std::move(p);
    if (!vp.height.has_value()) vp.height = 160.0f;
    const unsigned id = series_id(values);
    auto vals = std::make_shared<std::vector<float>>(std::move(values));
    vp.paint_id = id;
    vp.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h) {
        const auto [lo, hi] = value_range(*vals);
        r.draw_line(x, y + h - 4.0f, x + w, y + h - 4.0f, pal.divider, 1.0f);
        r.draw_polyline(line_points(*vals, x, y, w, h), pal.accent, 2.0f);
        (void)lo;
        (void)hi;
    };
    return view(std::move(vp));
}

ElementPtr area_chart(const Palette& pal, std::vector<float> values, Props p) {
    Props vp = std::move(p);
    if (!vp.height.has_value()) vp.height = 160.0f;
    const unsigned id = series_id(values);
    auto vals = std::make_shared<std::vector<float>>(std::move(values));
    vp.paint_id = id;
    vp.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h) {
        auto pts = line_points(*vals, x, y, w, h);
        if (pts.size() < 2) return;
        std::vector<std::pair<float, float>> area(pts);
        area.push_back({x + w, y + h});
        area.push_back({x, y + h});
        r.draw_polygon(area, pal.accent, 0.22f);
        r.draw_polyline(pts, pal.accent, 2.0f);
    };
    return view(std::move(vp));
}

ElementPtr sparkline(const Palette& pal, std::vector<float> values, Props p) {
    Props vp = std::move(p);
    if (!vp.width.has_value()) vp.width = 90.0f;
    if (!vp.height.has_value()) vp.height = 24.0f;
    const unsigned id = series_id(values);
    auto vals = std::make_shared<std::vector<float>>(std::move(values));
    vp.paint_id = id;
    vp.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h) {
        r.draw_polyline(line_points(*vals, x, y, w, h), pal.accent, 1.5f);
    };
    return view(std::move(vp));
}

ElementPtr pie_chart(const Palette& pal, std::vector<float> values,
                     std::vector<std::wstring> labels, Props p) {
    Props root_p = std::move(p);
    root_p.direction = Direction::row;
    root_p.gap = static_cast<float>(tokens::spacing_md);
    root_p.align = Align::center;
    auto root = view(std::move(root_p));

    const unsigned id = series_id(values);
    auto vals = std::make_shared<std::vector<float>>(std::move(values));
    Props pie_p;
    pie_p.width = 140.0f;
    pie_p.height = 140.0f;
    pie_p.paint_id = id;
    pie_p.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h) {
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.5f;
        const float rad = std::min(w, h) * 0.5f - 2.0f;
        const auto slices = pie_slices(*vals);
        for (size_t i = 0; i < slices.size(); ++i) {
            r.draw_pie_slice(cx, cy, rad, slices[i].start_deg, slices[i].sweep_deg,
                             series_color(pal, i));
        }
    };
    root->children.push_back(view(std::move(pie_p)));

    // Legend column.
    Props col_p;
    col_p.direction = Direction::column;
    col_p.gap = static_cast<float>(tokens::spacing_xs);
    auto col = view(std::move(col_p));
    for (size_t i = 0; i < labels.size(); ++i) {
        Props row_p;
        row_p.direction = Direction::row;
        row_p.gap = static_cast<float>(tokens::spacing_sm);
        row_p.align = Align::center;
        auto row = view(std::move(row_p));
        Props dot_p;
        dot_p.width = 10.0f;
        dot_p.height = 10.0f;
        dot_p.radius = 5.0f;
        dot_p.background = series_color(pal, i);
        row->children.push_back(view(std::move(dot_p)));
        Props lbl_p;
        lbl_p.text_color = pal.text_secondary;
        lbl_p.font_size_pt = tokens::font_caption;
        lbl_p.text_align = Align::start;
        row->children.push_back(label(labels[i], std::move(lbl_p)));
        col->children.push_back(std::move(row));
    }
    root->children.push_back(std::move(col));
    return root;
}

ElementPtr gauge(const Palette& pal, float value, std::wstring caption, Props p) {
    Props vp = std::move(p);
    if (!vp.width.has_value()) vp.width = 140.0f;
    if (!vp.height.has_value()) vp.height = 96.0f;
    const float t = std::clamp(value, 0.0f, 1.0f);
    const unsigned id = 0x9E4A7u ^ static_cast<unsigned>(t * 1000.0f);
    vp.paint_id = id;
    vp.paint = [pal, t](D2DRenderer& r, float x, float y, float w, float h) {
        const float cx = x + w * 0.5f;
        const float cy = y + h - 8.0f;
        const float rad = std::min(w * 0.5f, h) - 6.0f;
        r.draw_polyline(arc_points(cx, cy, rad, 0.0f, 180.0f, 6.0f),
                        pal.surface_variant, 8.0f);
        if (t > 0.001f) {
            r.draw_polyline(arc_points(cx, cy, rad, 0.0f, 180.0f * t, 6.0f),
                            pal.accent, 8.0f);
        }
    };
    if (!caption.empty()) {
        // Percent readout layered over the gauge's open mouth.
        Props txt_p;
        txt_p.text = std::to_wstring(static_cast<int>(t * 100.0f + 0.5f)) + L"%";
        txt_p.text_color = pal.text;
        txt_p.bold = true;
        txt_p.font_size_pt = tokens::font_subtitle;
        txt_p.text_align = Align::center;
        Props cap_p;
        cap_p.text = caption;
        cap_p.text_color = pal.text_secondary;
        cap_p.font_size_pt = tokens::font_caption;
        cap_p.text_align = Align::center;

        Props col_p;
        col_p.direction = Direction::column;
        col_p.gap = static_cast<float>(tokens::spacing_xs);
        col_p.justify = Justify::end;
        col_p.align = Align::center;
        col_p.flex_grow = 1.0f;
        auto overlay_col = view(std::move(col_p));
        overlay_col->children.push_back(label(txt_p.text, std::move(txt_p)));
        overlay_col->children.push_back(label(caption, std::move(cap_p)));
        auto wrap = view(std::move(vp));
        wrap->children.push_back(std::move(overlay_col));
        return wrap;
    }
    return view(std::move(vp));
}

} // namespace flux::ui
