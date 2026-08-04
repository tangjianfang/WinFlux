// WinFlux smoke test — exercises the headless layers (theme, element
// builders, flex layout) without creating a window, so it runs in CI.

#include <cmath>
#include <cstdio>

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

} // namespace

int main() {
    test_theme();
    test_builders();
    test_layout_row_grow();
    test_layout_column_center();

    if (failures == 0) {
        std::printf("WinFlux smoke test: all checks passed.\n");
        return 0;
    }
    std::printf("WinFlux smoke test: %d check(s) failed.\n", failures);
    return 1;
}

