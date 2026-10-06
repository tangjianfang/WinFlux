// flux::ui charts — line / area / pie / sparkline / gauge built on the
// Props::paint escape hatch. Geometry helpers are pure functions so the
// smoke test can verify normalization and slice math headlessly.

#include <flux/Components.hpp>

#include <flux/D2DRenderer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

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

size_t nearest_index(const std::vector<float>& values, float t) noexcept {
    // t in [0,1] across the series; clamped; empty series -> 0.
    if (values.empty()) return 0;
    if (values.size() == 1 || t <= 0.0f) return 0;
    if (t >= 1.0f) return values.size() - 1;
    const float pos = t * static_cast<float>(values.size() - 1);
    const size_t i = static_cast<size_t>(pos + 0.5f);
    return std::min(i, values.size() - 1);
}

namespace {

// Crosshair + value bubble over the hovered chart: vertical hairline at the
// cursor, dot on the nearest series point, value caption near the dot.
void draw_hover_crosshair(D2DRenderer& r, const Palette& pal,
                          const std::vector<float>& values, float x, float y,
                          float w, float h, float mx, float my) {
    if (mx < 0.0f || my < 0.0f || values.empty() || w <= 0.0f) return;
    const size_t idx = nearest_index(values, mx / w);
    const auto pts = line_points(values, x, y, w, h);
    if (idx >= pts.size()) return;
    const auto [px, py] = pts[idx];

    // Hairline at the cursor's column.
    r.draw_line(px, y, px, y + h, pal.divider, 1.0f);
    // Dot on the point.
    r.fill_round_rect(Rect{px - 4.0f, py - 4.0f, 8.0f, 8.0f}, 4.0f, pal.accent);
    r.fill_round_rect(Rect{px - 2.0f, py - 2.0f, 4.0f, 4.0f}, 2.0f, pal.surface);

    // Value bubble above the dot (falls below when near the top).
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f", values[idx]);
    const std::wstring text(buf);
    const auto [tw, th] = r.measure_text(text, tokens::font_caption, false);
    const float bw = tw + 12.0f;
    const float bh = th + 6.0f;
    const float bx = std::clamp(px - bw * 0.5f, x, x + w - bw);
    const float by = py - bh - 10.0f >= y ? py - bh - 10.0f : py + 10.0f;
    r.fill_round_rect(Rect{bx, by, bw, bh}, 4.0f, pal.surface);
    r.draw_round_rect(Rect{bx, by, bw, bh}, 4.0f, pal.border, 1.0f);
    r.draw_text(Rect{bx, by, bw, bh}, text, tokens::font_caption, pal.text, false,
                Align::center);
}

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
    vp.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h,
                          bool /*hovered*/, float mx, float my) {
        const auto [lo, hi] = value_range(*vals);
        r.draw_line(x, y + h - 4.0f, x + w, y + h - 4.0f, pal.divider, 1.0f);
        r.draw_polyline(line_points(*vals, x, y, w, h), pal.accent, 2.0f);
        draw_hover_crosshair(r, pal, *vals, x, y, w, h, mx, my);
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
    vp.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h,
                          bool /*hovered*/, float mx, float my) {
        auto pts = line_points(*vals, x, y, w, h);
        if (pts.size() < 2) return;
        std::vector<std::pair<float, float>> area(pts);
        area.push_back({x + w, y + h});
        area.push_back({x, y + h});
        r.draw_polygon(area, pal.accent, 0.22f);
        r.draw_polyline(pts, pal.accent, 2.0f);
        draw_hover_crosshair(r, pal, *vals, x, y, w, h, mx, my);
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
    vp.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h,
                          bool /*hovered*/, float /*mx*/, float /*my*/) {
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
    pie_p.paint = [pal, vals](D2DRenderer& r, float x, float y, float w, float h,
                              bool /*hovered*/, float /*mx*/, float /*my*/) {
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
    vp.paint = [pal, t](D2DRenderer& r, float x, float y, float w, float h,
                        bool /*hovered*/, float /*mx*/, float /*my*/) {
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


// ---- Radar & scatter (Round 7) -------------------------------------------

std::vector<std::pair<float, float>> radar_points(const std::vector<float>& values,
                                                  float cx, float cy, float r) noexcept {
    // Axis i points up + rotated clockwise; values normalized to 0..1 by
    // the caller (component normalizes against the series max).
    std::vector<std::pair<float, float>> pts;
    constexpr float kPi = 3.14159265358979f;
    const size_t n = values.size();
    if (n == 0) return pts;
    for (size_t i = 0; i < n; ++i) {
        const float t = std::clamp(values[i], 0.0f, 1.0f);
        const float deg = 360.0f * static_cast<float>(i) / static_cast<float>(n);
        const float rad = deg * kPi / 180.0f;
        pts.push_back({cx + r * t * std::sin(rad), cy - r * t * std::cos(rad)});
    }
    return pts;
}

std::pair<float, float> scatter_map(float vx, float vy, float x, float y, float w,
                                    float h, float xmin, float xmax, float ymin,
                                    float ymax) noexcept {
    // Maps a value pair into the plot rect; ranges degenerate to mid.
    const auto norm = [](float v, float lo, float hi) {
        return hi - lo > 1.0e-6f ? std::clamp((v - lo) / (hi - lo), 0.0f, 1.0f) : 0.5f;
    };
    return {x + w * norm(vx, xmin, xmax), y + h * (1.0f - norm(vy, ymin, ymax))};
}

ElementPtr radar_chart(const Palette& pal, std::vector<std::wstring> axes,
                       std::vector<std::vector<float>> series, Props p) {
    Props vp = std::move(p);
    if (!vp.width.has_value()) vp.width = 180.0f;
    if (!vp.height.has_value()) vp.height = 180.0f;

    float max_v = 0.0f;
    for (const auto& s : series)
        for (float v : s) max_v = std::max(max_v, v);
    if (max_v <= 0.0f) max_v = 1.0f;

    auto axes_ptr = std::make_shared<std::vector<std::wstring>>(std::move(axes));
    auto series_ptr = std::make_shared<std::vector<std::vector<float>>>(std::move(series));
    // The id must mix EVERY paint input (review 2026-10-06): deriving it from
    // max_v alone meant data that changed while the maximum held froze the
    // chart — identical rects, props, and id => empty damage => no repaint.
    unsigned id = 0x5ADA9u;
    for (const auto& row : *series_ptr) {
        for (const float v : row) {
            id = (id ^ static_cast<unsigned>(std::fabs(v) * 100.0f)) * 16777619u;
        }
    }
    id ^= static_cast<unsigned>(axes_ptr->size()) * 0x9E37u;
    vp.paint_id = id;
    vp.paint = [pal, axes_ptr, series_ptr, max_v](D2DRenderer& r, float x, float y,
                                                 float w, float h, bool, float, float) {
        const float cx = x + w * 0.5f;
        const float cy = y + h * 0.5f;
        const float rad = std::min(w, h) * 0.5f - 18.0f;
        const size_t n = axes_ptr->size();
        if (n < 3) return;

        // Web rings at 1/3, 2/3, 1.
        for (int ring = 1; ring <= 3; ++ring) {
            std::vector<float> ring_v(n, static_cast<float>(ring) / 3.0f);
            r.draw_polyline(radar_points(ring_v, cx, cy, rad), pal.divider, 1.0f);
        }
        // Axis spokes + captions.
        for (size_t i = 0; i < n; ++i) {
            const float deg = 360.0f * static_cast<float>(i) / static_cast<float>(n);
            constexpr float kPi = 3.14159265358979f;
            const float rad_ang = deg * kPi / 180.0f;
            r.draw_line(cx, cy, cx + rad * std::sin(rad_ang), cy - rad * std::cos(rad_ang),
                        pal.divider, 1.0f);
            const float lx = cx + (rad + 12.0f) * std::sin(rad_ang);
            const float ly = cy - (rad + 12.0f) * std::cos(rad_ang);
            r.draw_text(Rect{lx - 30.0f, ly - 8.0f, 60.0f, 16.0f}, (*axes_ptr)[i],
                        tokens::font_caption, pal.text_secondary, false, Align::center);
        }
        // Series polygons.
        for (size_t s = 0; s < series_ptr->size(); ++s) {
            std::vector<float> norm((*series_ptr)[s].size());
            for (size_t i = 0; i < norm.size(); ++i)
                norm[i] = (*series_ptr)[s][i] / max_v;
            r.draw_polyline(radar_points(norm, cx, cy, rad), series_color(pal, s), 2.0f);
        }
    };
    return view(std::move(vp));
}

ElementPtr scatter_chart(const Palette& pal, std::vector<std::pair<float, float>> points,
                         Props p) {
    Props vp = std::move(p);
    if (!vp.height.has_value()) vp.height = 160.0f;

    float xmin = 0.0f, xmax = 1.0f, ymin = 0.0f, ymax = 1.0f;
    if (!points.empty()) {
        xmin = xmax = points.front().first;
        ymin = ymax = points.front().second;
        for (const auto& [vx, vy] : points) {
            xmin = std::min(xmin, vx);
            xmax = std::max(xmax, vx);
            ymin = std::min(ymin, vy);
            ymax = std::max(ymax, vy);
        }
        if (xmax - xmin < 1.0e-6f) xmax = xmin + 1.0f;
        if (ymax - ymin < 1.0e-6f) ymax = ymin + 1.0f;
    }

    unsigned id = 0x5CA7u;
    for (const auto& [vx, vy] : points) {
        id = (id ^ static_cast<unsigned>(std::fabs(vx) * 100.0f + vy)) * 16777619u;
    }
    auto pts = std::make_shared<std::vector<std::pair<float, float>>>(std::move(points));
    vp.paint_id = id;
    vp.paint = [pal, pts, xmin, xmax, ymin, ymax](D2DRenderer& r, float x, float y,
                                                 float w, float h, bool, float, float) {
        // Axes.
        r.draw_line(x, y + h - 4.0f, x + w, y + h - 4.0f, pal.divider, 1.0f);
        r.draw_line(x, y, x, y + h - 4.0f, pal.divider, 1.0f);
        for (const auto& [vx, vy] : *pts) {
            const auto [px, py] = scatter_map(vx, vy, x, y, w, h - 4.0f, xmin, xmax,
                                              ymin, ymax);
            r.fill_round_rect(Rect{px - 3.0f, py - 3.0f, 6.0f, 6.0f}, 3.0f,
                              pal.accent, 0.8f);
        }
    };
    return view(std::move(vp));
}

} // namespace flux::ui
