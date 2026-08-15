// WinFlux smoke test — exercises the headless layers (theme, element
// builders, flex layout, damage tracking) without creating a window, so it
// runs in CI. Host lifecycle and live D2D rendering are covered by a separate
// WinFluxHostLifecycleTest target (see tests/host_lifecycle.cpp).

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <flux/Components.hpp>
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

    // Alpha channel (Round 2): default opaque, rgba() packs all four
    // channels, lerp interpolates alpha too.
    expect(rgb(1, 2, 3).a == 255, "rgb() defaults to opaque alpha");
    expect(rgba(10, 20, 30, 128).a == 128, "rgba() packs alpha");
    expect(lerp(rgba(0, 0, 0, 0), rgba(0, 0, 0, 255), 0.5f).a == 128,
           "lerp interpolates alpha channel");

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
    expect(tokens::scrollbar_width == 8, "scrollbar_width == 8");

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

void test_justify() {
    LayoutContext zero;
    zero.measure_leaf = [](const Element&) { return std::pair<float, float>{50.0f, 20.0f}; };

    // Row, justify=center: two 50-wide children in a 200-wide container
    // leave 100 free -> children sit at x=50 and x=100.
    {
        Props rp; rp.direction = Direction::row; rp.justify = Justify::center;
        auto t = with_children(view(std::move(rp)), {view(), view()});
        flex_layout(*t, 200.0f, 100.0f, zero);
        expect(near_eq(t->children[0]->x, 50.0f) && near_eq(t->children[1]->x, 100.0f),
               "row justify=center centers the pack");
    }
    // Row, justify=end: pack flush against the main-axis end.
    {
        Props rp; rp.direction = Direction::row; rp.justify = Justify::end;
        auto t = with_children(view(std::move(rp)), {view(), view()});
        flex_layout(*t, 200.0f, 100.0f, zero);
        expect(near_eq(t->children[0]->x, 100.0f) &&
               near_eq(t->children[1]->x + t->children[1]->w, 200.0f),
               "row justify=end flush right");
    }
    // Column, justify=center: vertical centering (the modal use-case).
    {
        Props rp; rp.direction = Direction::column; rp.justify = Justify::center;
        auto t = with_children(view(std::move(rp)), {view()});
        flex_layout(*t, 200.0f, 100.0f, zero);
        expect(near_eq(t->children[0]->y, 40.0f),
               "column justify=center centers vertically");
    }
    // flex_grow still absorbs all free space; justify becomes a no-op.
    {
        Props rp; rp.direction = Direction::row; rp.justify = Justify::end;
        Props a; a.flex_grow = 1.0f;
        auto t = with_children(view(std::move(rp)), {view(std::move(a)), view()});
        flex_layout(*t, 200.0f, 100.0f, zero);
        expect(near_eq(t->children[0]->w, 150.0f) && near_eq(t->children[0]->x, 0.0f),
               "flex_grow wins over justify");
    }
}

void test_scroll_layout() {
    LayoutContext zero;
    zero.measure_leaf = [](const Element&) { return std::pair<float, float>{0.0f, 20.0f}; };

    // 30 rows x 20 px of content inside a 300x200 scroll_view.
    auto mk = [] {
        auto content = view();
        for (int i = 0; i < 30; ++i) content->children.push_back(view());
        Props sp; sp.width = 300.0f; sp.height = 200.0f;
        return scroll_view(std::move(content), std::move(sp));
    };

    // a) Without explicit size the scroll_view measures to 0 — the content
    // never leaks into the parent's measurement.
    {
        auto content = view();
        for (int i = 0; i < 30; ++i) content->children.push_back(view());
        Props sp;
        auto sv = scroll_view(std::move(content), std::move(sp));
        Props rp; rp.direction = Direction::column;
        auto root = with_children(view(std::move(rp)), {std::move(sv)});
        flex_layout(*root, 400.0f, 500.0f, zero);
        expect(near_eq(root->children[0]->h, 0.0f),
               "scroll_view without explicit size collapses (no content leak)");
    }

    // b/c/d) Content layout + offset clamping + coordinate baking.
    {
        Props rp; rp.direction = Direction::column;
        auto root = with_children(view(std::move(rp)), {mk()});
        flex_layout(*root, 400.0f, 500.0f, zero);
        Element& sv = *root->children[0];
        expect(sv.type == L"scroll_view", "scroll_view builder sets type");
        expect(near_eq(sv.w, 300.0f) && near_eq(sv.h, 200.0f),
               "scroll_view sizes from Props");

        float offset = 0.0f;
        const float ch = layout_scroll_view(sv, offset, zero);
        expect(near_eq(ch, 600.0f), "content_h == 30 rows x 20");
        expect(near_eq(sv.children[0]->children[0]->y, sv.y),
               "offset 0: first row bakes at content-box top");

        offset = 1000.0f;
        (void)layout_scroll_view(sv, offset, zero);
        expect(near_eq(offset, 400.0f), "offset clamps to content-viewport");
        expect(near_eq(sv.children[0]->children[0]->y, sv.y - 400.0f),
               "offset bakes into content coords");
    }

    // Empty scroll_view: no content, content_h 0, offset pinned to 0.
    {
        auto sv = scroll_view(nullptr, Props{});
        sv->x = 10.0f; sv->y = 10.0f; sv->w = 100.0f; sv->h = 100.0f;
        float offset = 50.0f;
        const float ch = layout_scroll_view(*sv, offset, zero);
        expect(ch == 0.0f && offset == 0.0f, "empty scroll_view pins offset to 0");
    }
}

void test_overlay_anchoring() {
    LayoutContext zero;
    // Mirrors the Host's leaf measurement: explicit Props size wins, else 0.
    zero.measure_leaf = [](const Element& e) {
        return std::pair<float, float>{e.props.width.value_or(0.0f),
                                       e.props.height.value_or(0.0f)};
    };

    // Anchoring pattern 1 — centred modal: full-canvas column with
    // justify=center align=center; a 480x280 card lands at the middle.
    {
        Props root_p;
        root_p.direction = Direction::column;
        root_p.justify = Justify::center;
        root_p.align = Align::center;
        Props card_p; card_p.width = 480.0f; card_p.height = 280.0f;
        auto root = with_children(view(std::move(root_p)), {view(std::move(card_p))});
        flex_layout(*root, 1000.0f, 600.0f, zero);
        const Element& card = *root->children[0];
        expect(near_eq(card.x + card.w * 0.5f, 500.0f) &&
               near_eq(card.y + card.h * 0.5f, 300.0f),
               "overlay anchoring: centred modal");
    }

    // Anchoring pattern 2 — corner toast stack: justify=end align=end with
    // padding; the card's bottom-right corner sits at canvas minus padding.
    {
        Props root_p;
        root_p.direction = Direction::column;
        root_p.justify = Justify::end;
        root_p.align = Align::end;
        root_p.padding = {24, 24, 24, 24};
        Props card_p; card_p.width = 260.0f; card_p.height = 48.0f;
        auto root = with_children(view(std::move(root_p)), {view(std::move(card_p))});
        flex_layout(*root, 1000.0f, 600.0f, zero);
        const Element& card = *root->children[0];
        expect(near_eq(card.x + card.w, 976.0f) && near_eq(card.y + card.h, 576.0f),
               "overlay anchoring: corner toast");
    }

    // Anchoring pattern 3 — popup below a (100,60,200,30) anchor rect:
    // spacer(height=90) + row{spacer(width=100), menu 120x40}.
    {
        Props root_p; root_p.direction = Direction::column;
        Props gap_p; gap_p.height = 90.0f;   // anchor.bottom
        Props row_p; row_p.direction = Direction::row;
        Props indent_p; indent_p.width = 100.0f;   // anchor.left
        Props menu_p; menu_p.width = 120.0f; menu_p.height = 40.0f;
        auto row = with_children(view(std::move(row_p)),
                                 {view(std::move(indent_p)), view(std::move(menu_p))});
        auto root = with_children(view(std::move(root_p)),
                                  {view(std::move(gap_p)), std::move(row)});
        flex_layout(*root, 1000.0f, 600.0f, zero);
        const Element& menu = *root->children[1]->children[1];
        expect(near_eq(menu.x, 100.0f) && near_eq(menu.y, 90.0f),
               "overlay anchoring: below-rect popup");
    }
}

void test_components() {
    const Palette pal = palette(ThemeMode::light);

    // tabs: strip + underline divider, callbacks carry the index.
    {
        int got = -99;
        auto t = ui::tabs(pal, {L"One", L"Two"}, 1, [&](int i) { got = i; });
        // strip(2 segments) + underline(2 segments)
        expect(t->children.size() == 2 && t->children[0]->children.size() == 2,
               "ui::tabs builds strip + underline");
        t->children[0]->children[0]->props.on_click();
        expect(got == 0, "ui::tabs callback carries index");
        t->children[0]->children[1]->props.on_click();
        expect(got == 1, "ui::tabs second tab callback");
        expect(t->children[1]->children[1]->props.background.value_or(pal.surface)
                   == pal.accent,
               "ui::tabs underline marks the active tab");
    }

    // segmented: selected fills with accent; callback per segment.
    {
        int got = -1;
        auto s = ui::segmented(pal, {L"A", L"B", L"C"}, 2, [&](int i) { got = i; });
        expect(s->children.size() == 3, "ui::segmented has one child per option");
        expect(s->children[2]->props.background.has_value(), "active segment filled");
        s->children[0]->props.on_click();
        expect(got == 0, "ui::segmented callback");
    }

    // pagination: clamps current, disables at edges, prev/next + pages.
    {
        int got = -1;
        auto pg = ui::pagination(pal, 7, 5, [&](int i) { got = i; });
        // < 1 2 3 4 5 > = 7 children; current clamped to 4 (last page).
        expect(pg->children.size() == 7, "ui::pagination builds prev/pages/next");
        expect(pg->children[0]->props.enabled, "prev enabled at last page (clamped)");
        expect(!pg->children[6]->props.enabled, "next disabled at last page");
        // Direct prop-click bypasses the Host's enabled gate, so the lambda
        // still fires — the enabled flag is enforced at dispatch time.
        pg->children[3]->props.on_click();
        expect(got == 2, "page button callback");
    }

    // accordion: open section carries its body; toggle callback flips.
    {
        int got = -1;
        auto body = label(L"content");
        auto acc = ui::accordion(
            pal,
            {ui::AccordionSection{L"First", label(L"first body")},
             ui::AccordionSection{L"Second", nullptr}},
            0, [&](int i) { got = i; }, Props{});
        // Section 0: header + body; section 1: header only.
        expect(acc->children[0]->children.size() == 2, "open accordion section has body");
        expect(acc->children[1]->children.size() == 1, "closed section is header-only");
        acc->children[0]->children[0]->props.on_click();
        expect(got == -1, "clicking the open section requests close (-1)");
        acc->children[1]->children[0]->props.on_click();
        expect(got == 1, "clicking a closed section opens it");
    }

    // alert/badge: kind colours differ, badge keeps its text.
    {
        auto a1 = ui::alert(pal, ui::AlertKind::info, L"hi");
        auto a2 = ui::alert(pal, ui::AlertKind::danger, L"hi");
        expect(a1->children[0]->props.background != a2->children[0]->props.background,
               "ui::alert stripe colour follows the kind");
        auto b = ui::badge(pal, ui::BadgeKind::success, L"3 new");
        expect(b->props.text == L"3 new" && b->props.background.value() == pal.success,
               "ui::badge keeps text and kind colour");
    }

    // ToastHost: show assigns ids; prune drops expired, returns next deadline.
    {
        ui::ToastHost th;
        ui::toast_show(th, ui::ToastKind::info, L"one");
        ui::toast_show(th, ui::ToastKind::danger, L"two");
        expect(th.entries.size() == 2 && th.entries[0].id == 0 && th.entries[1].id == 1,
               "toast_show assigns sequential ids");
        th.entries[0].deadline_ms = 100;
        th.entries[1].deadline_ms = 500;
        expect(ui::prune_toasts(th, 200) == 500, "prune returns earliest live deadline");
        expect(th.entries.size() == 1 && th.entries[0].id == 1,
               "prune drops expired entries");
        expect(ui::prune_toasts(th, 999) == 0, "empty host returns 0 deadline");
    }

    // ModalHost: show/close lifecycle.
    {
        ui::ModalHost mh;
        bool closed = false;
        ui::modal_show(mh, L"Title", nullptr, [&] { closed = true; });
        expect(mh.current.has_value() && mh.current->title == L"Title",
               "modal_show stores the spec");
        mh.current->on_close();
        ui::modal_close(mh);
        expect(closed && !mh.current.has_value(), "modal_close clears after on_close");
    }
}

void test_menus() {
    const Palette pal = palette(ThemeMode::light);

    // Select trigger: shows the selected option, click flips open.
    {
        ui::SelectHost sel;
        sel.selected = 1;
        bool opened = false;
        auto trigger = ui::select(pal, sel, {L"Red", L"Green", L"Blue"}, nullptr,
                                  [&](bool open) { opened = open; });
        expect(trigger->type == L"button" && trigger->props.text == L"Green",
               "select trigger shows the selected option");
        trigger->props.on_click();
        expect(opened, "select trigger click requests open");
    }

    // Context menu host lifecycle.
    {
        ui::MenuHost menu;
        bool ran = false;
        ui::menu_show_at(menu,
                         {ui::MenuItem{L"Copy", [&] { ran = true; }}, ui::MenuItem{L"-", nullptr}},
                         100.0f, 50.0f);
        expect(menu.open && menu.items.size() == 2 && menu.ax == 100.0f,
               "menu_show_at stores position + items");
        menu.items[0].on_activate();
        ui::menu_close(menu);
        expect(ran && !menu.open && menu.items.empty(), "menu_close clears the host");
    }

    // Command filter: case-insensitive substring, empty query = all.
    {
        std::vector<ui::CommandItem> cmds = {
            {L"Go to Dashboard", nullptr},
            {L"Toggle Dark Mode", nullptr},
            {L"Open Settings", nullptr},
        };
        expect(ui::filter_commands(cmds, L"").size() == 3, "empty query matches all");
        const auto m1 = ui::filter_commands(cmds, L"dash");
        expect(m1.size() == 1 && m1[0] == 0, "lowercase query matches capitalized title");
        expect(ui::filter_commands(cmds, L"MODE").size() == 1, "uppercase query matches");
        expect(ui::filter_commands(cmds, L"zzz").empty(), "no match yields empty");
    }

    // on_key / autofocus are interaction-time only (not paint-dirty).
    {
        Element base = *view();
        Element v = base;
        v.props.on_key = [](unsigned) {};
        v.props.autofocus = true;
        expect(DamageTracker::paint_props_equal(base.props, v.props),
               "on_key/autofocus are NOT paint-dirty");
    }
}

void test_layout_components() {
    const Palette pal = palette(ThemeMode::light);

    // Splitter: [a, drag_handle, b] with grow ratios; handle reports 0..1.
    {
        float got = -1.0f;
        auto sp = ui::splitter(pal, view(), view(), 0.5f, [&](float t) { got = t; });
        expect(sp->children.size() == 3, "splitter is pane/handle/pane");
        expect(near_eq(sp->children[0]->props.flex_grow, 0.5f) &&
               near_eq(sp->children[2]->props.flex_grow, 0.5f),
               "splitter grow ratios split 50/50");
        const Element& handle = *sp->children[1];
        expect(handle.type == L"drag_handle" && handle.props.on_value_change != nullptr,
               "splitter handle is a drag_handle with a value callback");
        handle.props.on_value_change(0.75f);
        expect(near_eq(got, 0.75f), "drag_handle reports the ratio");
    }

    // Sidenav: toggle + entries; collapsed hides labels but keeps tooltips.
    {
        int picked = -1;
        auto nav = ui::sidenav(
            pal,
            {ui::NavEntry{L"Home", IconKind::inbox}, ui::NavEntry{L"Settings", IconKind::hamburger}},
            0, [&](int i) { picked = i; }, true, [] {});
        // toggle + 2 entries
        expect(nav->children.size() == 3, "sidenav = toggle + entries");
        expect(nav->children[1]->props.tooltip_text == L"Home",
               "collapsed sidenav keeps entry tooltips");
        expect(nav->children[2]->props.on_click != nullptr, "entries are clickable");
        nav->children[2]->props.on_click();
        expect(picked == 1, "sidenav entry callback carries its index");
    }

    // Drawer slide math is an Animated<float>: open tweens toward 1.
    {
        ui::DrawerHost dh;
        ui::drawer_open(dh);
        expect(dh.open && dh.slide.target() == 1.0f, "drawer_open targets slide=1");
        ui::drawer_close(dh);
        expect(!dh.open && dh.slide.target() == 0.0f, "drawer_close targets slide=0");
    }
}

void test_data_components() {
    const Palette pal = palette(ThemeMode::light);

    // visible_range: window math with clamping and overscan.
    {
        const auto v = ui::visible_range(500.0f, 200.0f, 20.0f, 100, 4);
        expect(v.first == 21, "first = offset/row - overscan (clamped)");
        expect(v.count == 200 / 20 + 1 + 8, "count = viewport rows + 1 + 2*overscan");
        const auto z = ui::visible_range(0.0f, 200.0f, 20.0f, 5, 4);
        expect(z.first == 0 && z.count == 5, "tiny list renders everything");
    }

    // table_sorted: stable lexicographic sort, both directions.
    {
        const std::vector<std::vector<std::wstring>> rows = {
            {L"b", L"2"}, {L"a", L"3"}, {L"c", L"1"}};
        const auto asc = ui::table_sorted(rows, 0, true);
        expect(asc[0][0] == L"a" && asc[2][0] == L"c", "ascending sort by column 0");
        const auto desc = ui::table_sorted(rows, 0, false);
        expect(desc[0][0] == L"c" && desc[2][0] == L"a", "descending sort");
    }

    // data_table structure: header + rows; selection paints.
    {
        auto t = ui::data_table(pal, {L"Name", L"Qty"},
                                {{L"alpha", L"1"}, {L"beta", L"2"}}, 0, true, nullptr,
                                1, nullptr);
        expect(t->children.size() == 3, "header + two rows");
        expect(t->children[0]->children.size() == 2, "one header cell per column");
        expect(t->children[2]->props.background.value_or(pal.surface) == pal.selection,
               "selected row highlighted");
    }

    // tree_view: expanded node reveals children; collapsed hides them.
    {
        std::vector<ui::TreeItem> items = {
            {L"parent", {{L"child a", {}}, {L"child b", {}}}},
            {L"leaf", {}},
        };
        auto open = ui::tree_view(pal, items, {{0}}, nullptr, nullptr);
        // parent row, child a, child b, leaf
        expect(open->children.size() == 4, "expanded tree shows nested children");
        auto closed = ui::tree_view(pal, items, {}, nullptr, nullptr);
        expect(closed->children.size() == 2, "collapsed tree hides children");
    }

    // virtual_list: spacer + only the visible window built.
    {
        int built = 0;
        auto col = ui::virtual_list(pal, 1000, 20.0f, 200.0f, 500.0f,
                                    [&](int) { ++built; return view(); });
        // 200/20 = 10 visible + 1 + 2*4 overscan = 19 rows.
        expect(built == 19, "only the visible window is built");
        // Leading spacer carries 21 * 20 = 420 px.
        expect(near_eq(col->children[0]->props.height.value_or(0.0f), 420.0f),
               "leading spacer carries the scrolled-out prefix");
    }
}

void test_pickers_math() {
    expect(ui::days_in_month(2026, 0) == 31 && ui::days_in_month(2026, 3) == 30,
           "days_in_month month lengths");
    expect(ui::days_in_month(2024, 1) == 29 && ui::days_in_month(2026, 1) == 28,
           "February leap / non-leap");
    // 2026-08-14 is a Saturday (6).
    expect(ui::first_weekday(2026, 7) == 6, "first_weekday: Aug 2026 starts Saturday");
    // 2026-01-01 is a Thursday (4).
    expect(ui::first_weekday(2026, 0) == 4, "first_weekday: Jan 2026 starts Thursday");
}

void test_charts() {
    // value_range pads flat series so the line sits mid-box.
    {
        const auto [lo, hi] = ui::value_range({5.0f, 5.0f, 5.0f});
        expect(lo == 4.0f && hi == 6.0f, "flat series pads by 1");
        const auto r = ui::value_range({1.0f, 3.0f, 2.0f});
        expect(r.first == 1.0f && r.second == 3.0f, "value_range min/max");
    }
    // line_points: normalized endpoints, low value -> larger y.
    {
        const auto pts = ui::line_points({0.0f, 10.0f}, 0.0f, 0.0f, 100.0f, 50.0f);
        expect(pts.size() == 2, "one point per value");
        expect(near_eq(pts[0].first, 0.0f) && near_eq(pts[1].first, 100.0f),
               "x spans the width");
        expect(pts[0].second > pts[1].second, "higher value -> smaller y");
    }
    // pie_slices: sweeps sum to 360, proportionality respected.
    {
        const auto s = ui::pie_slices({1.0f, 3.0f});
        expect(s.size() == 2, "one slice per value");
        float total = 0.0f;
        for (const auto& sl : s) total += sl.sweep_deg;
        expect(near_eq(total, 360.0f), "sweeps sum to 360");
        expect(near_eq(s[1].sweep_deg, 270.0f), "slice proportional to value");
        expect(near_eq(s[0].start_deg, 0.0f) && near_eq(s[1].start_deg, 90.0f),
               "slices start where the previous ended");
    }
    // arc_points: endpoints on the circle at 0/180 degrees.
    {
        const auto a = ui::arc_points(50.0f, 50.0f, 40.0f, 0.0f, 180.0f, 10.0f);
        expect(a.size() >= 2, "arc has points");
        expect(near_eq(a.front().first, 50.0f) && near_eq(a.front().second, 10.0f),
               "arc starts at 12 o'clock");
        expect(near_eq(a.back().first, 50.0f) && near_eq(a.back().second, 90.0f),
               "half arc ends at 6 o'clock");
    }
    // Chart components carry a paint callback + data-derived paint_id.
    {
        const Palette pal = palette(ThemeMode::light);
        auto c1 = ui::line_chart(pal, {1.0f, 2.0f, 3.0f});
        auto c2 = ui::line_chart(pal, {1.0f, 2.0f, 4.0f});
        expect(c1->props.paint != nullptr, "line_chart installs a painter");
        expect(c1->props.paint_id != c2->props.paint_id,
               "different series -> different paint_id (diff-friendly)");
        auto g = ui::gauge(pal, 0.5f, L"CPU");
        expect(g->props.paint != nullptr && !g->children.empty(),
               "gauge has painter + caption overlay");
    }
}

void test_animation() {
    // Curves: endpoints exact, clamped, front/back-loaded as designed.
    expect(near_eq(apply_curve(0.0f, Curve::linear), 0.0f) &&
           near_eq(apply_curve(1.0f, Curve::linear), 1.0f), "curve linear endpoints");
    expect(near_eq(apply_curve(0.0f, Curve::ease_in_out), 0.0f) &&
           near_eq(apply_curve(1.0f, Curve::ease_in_out), 1.0f),
           "curve ease_in_out endpoints");
    expect(near_eq(apply_curve(0.5f, Curve::ease_in_out), 0.5f),
           "ease_in_out symmetric midpoint");
    expect(apply_curve(0.25f, Curve::ease_out) > 0.25f, "ease_out front-loads");
    expect(near_eq(apply_curve(2.0f, Curve::ease_out), 1.0f), "curve clamps t > 1");
    expect(near_eq(apply_curve(-1.0f, Curve::ease_in_out), 0.0f), "curve clamps t < 0");

    // approach_blend: steady states snap, ramps go the right way.
    expect(near_eq(approach_blend(true, true, 1000, 0, 100), 1.0f), "blend steady-on");
    expect(near_eq(approach_blend(false, false, 1000, 0, 100), 0.0f), "blend steady-off");
    expect(near_eq(approach_blend(true, false, 50, 0, 100), 0.5f), "blend ramping in");
    expect(near_eq(approach_blend(false, true, 50, 0, 100), 0.5f), "blend ramping out");
    expect(near_eq(approach_blend(true, false, 500, 0, 100), 1.0f), "blend clamped past end");

    // Animated<float> with an injected clock (no real timer involved).
    Animated<float> a{0.0f};
    a.set_at(1.0f, 100, Curve::linear, 0);
    expect(a.active_at(0) && a.active_at(99) && !a.active_at(100),
           "active window is half-open [start, start+dur)");
    expect(near_eq(a.current_at(0), 0.0f), "tween starts at from");
    expect(near_eq(a.current_at(50), 0.5f), "linear midpoint");
    expect(near_eq(a.current_at(100), 1.0f) && near_eq(a.current_at(500), 1.0f),
           "tween settles at to and stays");
    expect(a.remaining_at(25) == 75 && a.remaining_at(500) == 0, "remaining math");

    // Retarget mid-flight continues from the current visual value.
    a.set_at(0.5f, 100, Curve::linear, 50);   // visual value at t=50 was 0.5
    expect(near_eq(a.current_at(50), 0.5f), "retarget starts from visual value");
    expect(near_eq(a.target(), 0.5f), "target tracks latest set");

    // duration 0 or same-value set snaps (no tween).
    Animated<float> b{2.0f};
    b.set_at(9.0f, 0, Curve::ease_in_out, 0);
    expect(!b.active_at(0) && near_eq(b.current_at(0), 9.0f), "zero-duration snaps");
    b.set_at(9.0f, 500, Curve::linear, 0);
    expect(!b.active_at(0), "same-value set is a no-op tween");

    // Animated<Color> interpolates per channel.
    Animated<Color> c{rgb(0, 0, 0)};
    c.set_at(rgb(200, 100, 50), 100, Curve::linear, 0);
    expect(c.current_at(50) == rgb(100, 50, 25), "Animated<Color> channel lerp");

    // Animated<int> rounds to nearest.
    Animated<int> n{0};
    n.set_at(10, 100, Curve::linear, 0);
    expect(n.current_at(50) == 5 && n.current_at(75) == 8, "Animated<int> rounds");
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

    // New paint props (Round 1) must participate in the paint diff;
    // cursor is interaction-time only and must NOT.
    {
        Element base = *view();
        base.x = 0; base.y = 0; base.w = 10; base.h = 10;
        Element v = base; v.props.opacity = 0.5f;
        expect(!DamageTracker::paint_props_equal(base.props, v.props),
               "opacity change is paint-dirty");
        v = base; v.props.shadow_elevation = 2;
        expect(!DamageTracker::paint_props_equal(base.props, v.props),
               "shadow change is paint-dirty");
        v = base; v.props.background_gradient = Gradient{rgb(0, 0, 0), rgb(255, 255, 255)};
        expect(!DamageTracker::paint_props_equal(base.props, v.props),
               "gradient change is paint-dirty");
        v = base; v.props.icon = IconKind::close;
        expect(!DamageTracker::paint_props_equal(base.props, v.props),
               "icon change is paint-dirty");
        v = base; v.props.cursor = Cursor::hand;
        expect(DamageTracker::paint_props_equal(base.props, v.props),
               "cursor is NOT paint-dirty");
        v = base; v.props.tooltip_text = L"hint";
        expect(DamageTracker::paint_props_equal(base.props, v.props),
               "tooltip_text is NOT paint-dirty");
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
    test_animation();
    test_justify();
    test_scroll_layout();
    test_overlay_anchoring();
    test_components();
    test_menus();
    test_layout_components();
    test_data_components();
    test_pickers_math();
    test_charts();
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

