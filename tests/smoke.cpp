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

