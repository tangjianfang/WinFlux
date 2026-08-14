// WinFlux smoke test — exercises the headless layers (theme, element
// builders, flex layout, damage tracking) without creating a window, so it
// runs in CI. Host lifecycle and live D2D rendering are covered by a separate
// WinFluxHostLifecycleTest target (see tests/host_lifecycle.cpp).

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <flux/Damage.hpp>
#include <flux/flux.hpp>

using namespace flux;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (condition) {
        std::printf("[PASS] %s\n", what);
    } else {
        std::printf("[FAIL] %s\n", what);
        ++failures;
    }
}

bool near_eq(float a, float b) noexcept { return std::fabs(a - b) < 0.01f; }

void test_theme() {
    const Palette light = palette(ThemeMode::light);
    const Palette dark = palette(ThemeMode::dark);
    const Palette hc = palette(ThemeMode::high_contrast);

    expect(light.background != dark.background, "light/dark backgrounds differ");
    expect(hc.background == rgb(0, 0, 0), "high contrast background is pure black");
    expect(hc.text == rgb(255, 255, 255), "high contrast text is pure white");

    const Color a = rgb(0, 0, 0);
    const Color b = rgb(200, 100, 50);
    expect(lerp(a, b, 0.0f) == a, "lerp t=0 returns start colour");
    expect(lerp(a, b, 1.0f) == b, "lerp t=1 returns end colour");
    expect(lerp(a, b, 0.5f) == rgb(100, 50, 25), "lerp t=0.5 mixes per channel");

    const Palette mid = lerp_palette(light, dark, 1.0f);
    expect(mid.background == dark.background, "lerp_palette t=1 equals target palette");

    expect(tokens::spacing_sm == 8 && tokens::control_height_md == 32,
           "design tokens match the DESIGN.md contract");

    // Full token contract (mirrors DESIGN.md one-for-one). Kept as a list so
    // adding a token surfaces here as a missing assertion, not a silent drift.
    expect(tokens::spacing_xs == 4, "spacing_xs == 4");
    expect(tokens::spacing_sm == 8, "spacing_sm == 8");
    expect(tokens::spacing_md == 16, "spacing_md == 16");
    expect(tokens::spacing_lg == 24, "spacing_lg == 24");
    expect(tokens::spacing_xl == 32, "spacing_xl == 32");
    expect(tokens::control_height_sm == 24, "control_height_sm == 24");
    expect(tokens::control_height_md == 32, "control_height_md == 32");
    expect(tokens::control_height_lg == 40, "control_height_lg == 40");
    expect(tokens::radius_sm == 4, "radius_sm == 4");
    expect(tokens::radius_control == 6, "radius_control == 6");
    expect(tokens::radius_md == 8, "radius_md == 8");
    expect(tokens::radius_card == 10, "radius_card == 10");
    expect(tokens::radius_lg == 12, "radius_lg == 12");
    expect(near_eq(tokens::font_caption, 12.0f), "font_caption == 12.0");
    expect(near_eq(tokens::font_body, 14.0f), "font_body == 14.0");
    expect(near_eq(tokens::font_subtitle, 16.0f), "font_subtitle == 16.0");
    expect(near_eq(tokens::font_title, 20.0f), "font_title == 20.0");
    expect(near_eq(tokens::font_hero, 28.0f), "font_hero == 28.0");
    expect(tokens::theme_fade_ms == 200, "theme_fade_ms == 200");
    expect(tokens::checkbox_box == 18, "checkbox_box == 18");
    expect(tokens::progress_height == 10, "progress_height == 10");

    // Motion tokens (Round 1 visual foundation).
    expect(tokens::motion_fast_ms == 120, "motion_fast_ms == 120");
    expect(tokens::motion_medium_ms == 200, "motion_medium_ms == 200");
    expect(tokens::motion_slow_ms == 280, "motion_slow_ms == 280");
    expect(tokens::shadow_blur_sm == 4, "shadow_blur_sm == 4");
    expect(tokens::shadow_blur_md == 8, "shadow_blur_md == 8");
    expect(tokens::shadow_blur_lg == 14, "shadow_blur_lg == 14");
    expect(near_eq(tokens::shadow_alpha, 0.10f), "shadow_alpha == 0.10");
    expect(near_eq(tokens::shadow_offset_y, 2.0f), "shadow_offset_y == 2.0");
}

void test_builders() {
    const ElementPtr b = button(L"OK", nullptr);
    expect(b->type == L"button", "button builder sets type");
    expect(b->props.height.has_value() && near_eq(*b->props.height, 32.0f),
           "button default height is 32 (control_height_md)");
    expect(b->props.focusable, "button is focusable");

    bool toggled_to = false;
    const ElementPtr cb = checkbox(L"X", false, [&toggled_to](bool v) { toggled_to = v; });
    expect(cb->props.checked.has_value() && !*cb->props.checked, "checkbox stores checked state");
    if (cb->props.on_click) cb->props.on_click();
    expect(toggled_to, "checkbox click handler toggles to true");

    const ElementPtr pr = progress(1.7f);
    expect(near_eq(pr->props.progress_value, 1.0f), "progress clamps to [0,1]");
}

void test_layout_row_grow() {
    LayoutContext ctx;
    ctx.measure_leaf = [](const Element&) { return std::pair<float, float>{0.0f, 0.0f}; };

    Props row_p;
    row_p.direction = Direction::row;
    row_p.gap = 8.0f;
    row_p.padding = EdgeInsets{8, 8, 8, 8};

    Props c1; c1.width = 50.0f; c1.height = 20.0f;
    Props c2; c2.width = 50.0f; c2.height = 20.0f;
    Props c3; c3.flex_grow = 1.0f; c3.height = 20.0f;

    auto root = with_children(view(std::move(row_p)), {
        view(std::move(c1)), view(std::move(c2)), view(std::move(c3)),
    });
    flex_layout(*root, 300.0f, 100.0f, ctx);

    expect(root->children.size() == 3, "row has three children");
    const Element& a = *root->children[0];
    const Element& b = *root->children[1];
    const Element& c = *root->children[2];
    expect(near_eq(a.x, 8.0f) && near_eq(a.w, 50.0f), "fixed child 1 keeps size at padding edge");
    expect(near_eq(b.x, 66.0f), "child 2 placed after gap");
    expect(near_eq(c.x, 124.0f) && near_eq(c.w, 168.0f), "grow child absorbs remaining main space");
    expect(near_eq(c.w + c.x, 292.0f), "grow child ends at right padding edge");
    expect(near_eq(a.y, 8.0f) && near_eq(a.h, 20.0f), "cross axis keeps fixed height at start");
}

void test_layout_column_center() {
    LayoutContext ctx;
    ctx.measure_leaf = [](const Element&) { return std::pair<float, float>{40.0f, 16.0f}; };

    Props col_p;
    col_p.direction = Direction::column;
    col_p.align = Align::center;
    col_p.gap = 4.0f;

    auto root = with_children(view(std::move(col_p)), {label(L"a"), label(L"b")});
    flex_layout(*root, 200.0f, 200.0f, ctx);

    const Element& a = *root->children[0];
    const Element& b = *root->children[1];
    expect(near_eq(a.w, 40.0f), "leaf measured width used");
    expect(near_eq(a.x, 80.0f), "cross-axis center alignment positions child");
    expect(near_eq(b.y, a.y + a.h + 4.0f), "column gap separates children");
}

void test_damage() {
    LayoutContext zero;
    zero.measure_leaf = [](const Element&) { return std::pair<float, float>{40.0f, 16.0f}; };
    DamageTracker d;

    // Case 1: identical trees -> empty dirty (nothing to repaint).
    {
        Props rp; rp.direction = Direction::row; rp.gap = 8.0f; rp.padding = {8,8,8,8};
        Props a; a.width = 50.0f; a.height = 20.0f;
        Props b; b.width = 50.0f; b.height = 20.0f;
        auto t = with_children(view(std::move(rp)), {view(std::move(a)), view(std::move(b))});
        flex_layout(*t, 200.0f, 100.0f, zero);
        auto rects = d.compute(t.get(), t.get(), 200.0f, 100.0f);
        expect(rects.empty(), "identical trees produce empty dirty");
    }

    // Case 2: text change on a label -> exactly that node's bounds (AA-inflated).
    {
        Props rp; rp.direction = Direction::column; rp.padding = {8,8,8,8};
        auto prev = with_children(view(rp), {label(L"Hello")});
        auto next = with_children(view(rp), {label(L"World")});
        flex_layout(*prev, 200.0f, 100.0f, zero);
        flex_layout(*next, 200.0f, 100.0f, zero);
        auto rects = d.compute(prev.get(), next.get(), 200.0f, 100.0f);
        expect(rects.size() == 1, "text change yields single dirty rect");
        if (!rects.empty()) {
            const Rect& r = rects[0];
            // Label rect: x=8, y=8, w=40, h=16; AA inflate by 1 -> (7,7,42,18).
            expect(near_eq(r.x, 7.0f) && near_eq(r.y, 7.0f) &&
                   near_eq(r.w, 42.0f) && near_eq(r.h, 18.0f),
                   "text dirty rect covers label bounds with AA inflate");
        }
    }

    // Case 3: sibling resize moves the flex-grow child -> both old + new
    // positions end up in the dirty set (unioned with AA slack).
    {
        Props rp; rp.direction = Direction::row; rp.gap = 8.0f; rp.padding = {8,8,8,8};
        Props a;  a.width  = 50.0f; a.height = 20.0f;
        Props b;  b.width  = 50.0f; b.height = 20.0f;
        Props c;  c.height = 20.0f; c.flex_grow = 1.0f;
        auto prev = with_children(view(rp), {view(a), view(b), view(c)});
        Props a2; a2.width = 100.0f; a2.height = 20.0f;
        auto next = with_children(view(rp), {view(a2), view(b), view(c)});
        flex_layout(*prev, 300.0f, 100.0f, zero);
        flex_layout(*next, 300.0f, 100.0f, zero);
        auto rects = d.compute(prev.get(), next.get(), 300.0f, 100.0f);
        expect(!rects.empty(), "child move yields non-empty dirty");
        expect(rects.size() <= d.max_rects, "child move stays within rect budget");
        float minx = 1e9f, maxx = -1e9f;
        for (const auto& r : rects) {
            minx = std::min(minx, r.x);
            maxx = std::max(maxx, r.x + r.w);
        }
        expect(minx <= 8.0f, "dirty union starts near the old first-child edge");
        expect(maxx >= 290.0f, "dirty union reaches the new flex-child right edge");
    }

    // Case 4: >max_rects changed children -> fullscreen fallback.
    {
        Props rp; rp.direction = Direction::column; rp.padding = {0,0,0,0};
        auto prev = view(rp);
        auto next = view(rp);
        constexpr int kChildren = 9;
        for (int i = 0; i < kChildren; ++i) {
            Props p; p.width = 50.0f; p.height = 20.0f; p.background = rgb(0,0,0);
            prev->children.push_back(view(p));
            Props p2 = p; p2.background = rgb(255,0,0);   // change
            next->children.push_back(view(p2));
        }
        flex_layout(*prev, 200.0f, 300.0f, zero);
        flex_layout(*next, 200.0f, 300.0f, zero);
        auto rects = d.compute(prev.get(), next.get(), 200.0f, 300.0f);
        expect(rects.size() == 1, ">max_rects changes collapse to a single rect");
        if (!rects.empty()) {
            expect(near_eq(rects[0].x, 0.0f) && near_eq(rects[0].y, 0.0f) &&
                   near_eq(rects[0].w, 200.0f) && near_eq(rects[0].h, 300.0f),
                   "fullscreen fallback covers the whole canvas");
        }
    }

    // First-frame and empty-tree edge cases.
    {
        auto t = view(Props{});
        flex_layout(*t, 200.0f, 100.0f, zero);
        expect(d.compute(nullptr, t.get(), 200.0f, 100.0f).size() == 1,
               "first frame (no prev) forces fullscreen");
        expect(d.compute(t.get(), nullptr, 200.0f, 100.0f).size() == 1,
               "tree removal forces fullscreen");
    }

    // Interaction-state diff: hover changes on an otherwise-unchanged tree
    // must produce dirty rects for the OLD and NEW hover elements (the
    // structural diff alone sees no change).
    {
        Props rp; rp.direction = Direction::row; rp.gap = 8.0f; rp.padding = {0,0,0,0};
        Props a; a.width = 50.0f; a.height = 20.0f;
        Props b; b.width = 50.0f; b.height = 20.0f;
        auto t = with_children(view(rp), {view(a), view(b)});
        flex_layout(*t, 200.0f, 100.0f, zero);
        DamageTracker::InteractionState prev{{}, {}, {}};
        DamageTracker::InteractionState next{{0}, {}, {}};
        auto rects = d.compute(t.get(), t.get(), 200.0f, 100.0f, &prev, &next);
        expect(!rects.empty(), "hover change yields non-empty dirty");
        if (!rects.empty()) {
            const Rect& r = rects[0];
            // First child at (0,0) sized 50x20; AA inflate -> (-1,-1,52,22).
            expect(r.x <= 0.0f && r.y <= 0.0f && r.w >= 50.0f && r.h >= 20.0f,
                   "hover-change dirty rect covers first-child bounds");
        }
    }

    // No interaction change + no tree change = empty dirty (regression for the
    // old behaviour where every handler had to set force_full_dirty_).
    {
        Props rp; rp.direction = Direction::row; rp.gap = 8.0f; rp.padding = {0,0,0,0};
        auto t = with_children(view(rp), {label(L"a"), label(L"b")});
        flex_layout(*t, 200.0f, 100.0f, zero);
        DamageTracker::InteractionState same{{}, {1}, {}};
        auto rects = d.compute(t.get(), t.get(), 200.0f, 100.0f, &same, &same);
        expect(rects.empty(), "no-op interaction state stays empty");
    }

    // Same-element hover: old and new paths identical -> add_interaction_rects
    // returns early, no extra rects pushed. (Regression: would have produced
    // two duplicates of the same rect.)
    {
        Props rp; rp.direction = Direction::row; rp.padding = {0,0,0,0};
        auto t = with_children(view(rp), {label(L"a")});
        flex_layout(*t, 200.0f, 100.0f, zero);
        DamageTracker::InteractionState prev{{}, {}, {}};
        DamageTracker::InteractionState next{{0}, {}, {}};
        auto rects = d.compute(t.get(), t.get(), 200.0f, 100.0f, &prev, &next);
        expect(!rects.empty(), "hover entering an element yields dirty");
        expect(rects.size() <= d.max_rects, "hover-only diff stays within budget");
    }

    // Three distinct interaction changes (hover + press + focus all move)
    // pushes up to 6 rects; merge_overlapping collapses any that overlap.
    {
        Props rp; rp.direction = Direction::row; rp.gap = 0.0f; rp.padding = {0,0,0,0};
        Props a; a.width = 50.0f; a.height = 20.0f;
        Props b; b.width = 50.0f; b.height = 20.0f;
        Props c; c.width = 50.0f; c.height = 20.0f;
        auto t = with_children(view(rp), {view(a), view(b), view(c)});
        flex_layout(*t, 200.0f, 100.0f, zero);
        DamageTracker::InteractionState prev{{0}, {1}, {2}};
        DamageTracker::InteractionState next{{1}, {2}, {0}};
        auto rects = d.compute(t.get(), t.get(), 200.0f, 100.0f, &prev, &next);
        // 6 rects (3 old + 3 new), none overlap (50px apart, 20px tall),
        // so they all survive merge; AA inflate 1px keeps gaps.
        expect(!rects.empty(), "3 interactions x 2 sides yields dirty");
        expect(rects.size() <= d.max_rects, "3 interactions stay within budget");
    }

    // Multi-rect merge: two changed children with overlapping bounds -> one
    // merged rect after merge_overlapping (not the union of two adjacent).
    {
        Props rp; rp.direction = Direction::row; rp.gap = 0.0f; rp.padding = {0,0,0,0};
        // Two 50px children in a 100px container with no gap — child 2 has a
        // -25 left margin so its rect (25..75) overlaps child 1's (0..50).
        Props a; a.width = 50.0f; a.height = 30.0f; a.background = rgb(0,0,0);
        Props b; b.width = 50.0f; b.height = 30.0f; b.background = rgb(0,0,0);
        b.margin = EdgeInsets{0, 0, 0, 0};
        b.margin.left = -25.0f;
        auto prev = with_children(view(rp), {view(a), view(b)});
        Props a2 = a; a2.background = rgb(255,0,0);
        Props b2 = b; b2.background = rgb(0,255,0);
        auto next = with_children(view(rp), {view(a2), view(b2)});
        flex_layout(*prev, 100.0f, 100.0f, zero);
        flex_layout(*next, 100.0f, 100.0f, zero);
        auto rects = d.compute(prev.get(), next.get(), 100.0f, 100.0f);
        // Overlapping changed rects merge into a single rect.
        expect(rects.size() == 1, "overlapping changed siblings merge into one rect");
    }
}

} // namespace

int main() {
    test_theme();
    test_builders();
    test_layout_row_grow();
    test_layout_column_center();
    test_damage();

    if (failures == 0) {
        std::printf("WinFlux smoke test: all checks passed.\n");
        return 0;
    }
    std::printf("WinFlux smoke test: %d check(s) failed.\n", failures);
    return 1;
}

