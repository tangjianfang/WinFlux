// WinFlux Dashboard — a realistic analytics workbench built entirely from
// flux::ui components (Round 5 dogfooding target). SideNav shell +
// Ctrl+K command palette + sortable tables + charts + drawer details +
// toast feedback. No hand-rolled widgets: every control below is a
// library component or an element primitive.

#include <windows.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <flux/flux.hpp>
#include <flux/Components.hpp>

using namespace flux;

namespace {

struct App {
    Host host;
    State<int> nav;
    State<bool> nav_collapsed;
    // Overview / servers table.
    State<int> sort_col;
    State<bool> sort_asc;
    State<int> selected_server;
    State<float> virt_offset;
    // Settings.
    State<bool> auto_refresh;
    State<bool> email_alerts;
    State<int> density;
    // Overlay hosts.
    ui::ToastHost toast_host;
    ui::CommandHost cmd_host;
    ui::DrawerHost drawer_host;
    ui::SelectHost select_host;
};

const std::vector<std::vector<std::wstring>> kServers = {
    {L"api-gateway-01", L"healthy", L"99.98%"},
    {L"api-gateway-02", L"healthy", L"99.91%"},
    {L"worker-eu-1", L"degraded", L"97.20%"},
    {L"worker-us-3", L"healthy", L"99.75%"},
    {L"db-primary", L"healthy", L"99.99%"},
    {L"db-replica-2", L"maintenance", L"96.10%"},
    {L"cache-redis", L"healthy", L"99.87%"},
};

const std::vector<std::vector<std::wstring>> kEvents = {
    {L"12:04", L"deploy", L"api-gateway-01"},
    {L"11:52", L"alert", L"worker-eu-1"},
    {L"11:31", L"scale-up", L"worker-us-3"},
    {L"10:58", L"backup", L"db-primary"},
    {L"10:12", L"restart", L"cache-redis"},
    {L"09:47", L"deploy", L"worker-eu-1"},
};

ElementPtr stat_card(const Palette& pal, const wchar_t* title,
                     std::function<ElementPtr(const Palette&)> make_chart) {
    Props card_p;
    card_p.background = pal.surface;
    card_p.radius = static_cast<float>(tokens::radius_card);
    card_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                static_cast<float>(tokens::spacing_md),
                                static_cast<float>(tokens::spacing_md),
                                static_cast<float>(tokens::spacing_md)};
    card_p.gap = static_cast<float>(tokens::spacing_sm);
    card_p.flex_grow = 1.0f;
    card_p.shadow_elevation = 1;
    auto card = view(std::move(card_p));

    Props title_p;
    title_p.text = title;
    title_p.text_color = pal.text_secondary;
    title_p.font_size_pt = tokens::font_caption;
    title_p.text_align = Align::start;
    card->children.push_back(label(title, std::move(title_p)));
    card->children.push_back(make_chart(pal));
    return card;
}

ElementPtr page_overview(App& app) {
    const Palette& pal = app.host.palette();

    auto stats = view();
    stats->props.direction = Direction::row;
    stats->props.gap = static_cast<float>(tokens::spacing_md);
    stats->props.align = Align::stretch;
    stats->children.push_back(stat_card(pal, L"CPU load", [](const Palette& p) {
        return ui::gauge(p, 0.38f, L"avg 38%");
    }));
    stats->children.push_back(stat_card(pal, L"Requests / min", [](const Palette& p) {
        return ui::area_chart(p, {40, 55, 48, 62, 58, 75, 70, 82, 78, 90, 86, 95}, Props{});
    }));
    stats->children.push_back(stat_card(pal, L"Traffic mix", [](const Palette& p) {
        return ui::pie_chart(p, {6, 3, 1}, {L"API", L"Web", L"Other"});
    }));

    Props table_title_p;
    table_title_p.text = L"Recent events (click a header to sort)";
    table_title_p.text_color = pal.text_secondary;
    table_title_p.font_size_pt = tokens::font_caption;
    table_title_p.text_align = Align::start;

    auto table = ui::data_table(pal, {L"Time", L"Kind", L"Target"}, kEvents,
                               app.sort_col.get(), app.sort_asc.get(),
                               [&app](int col) {
                                   if (col == app.sort_col.get())
                                       app.sort_asc.set(!app.sort_asc.get());
                                   else {
                                       app.sort_col.set(col);
                                       app.sort_asc.set(true);
                                   }
                               },
                               -1, nullptr);

    Props col_p;
    col_p.direction = Direction::column;
    col_p.gap = static_cast<float>(tokens::spacing_md);
    col_p.align = Align::stretch;
    col_p.flex_grow = 1.0f;
    auto col = view(std::move(col_p));
    col->children.push_back(std::move(stats));
    col->children.push_back(label(table_title_p.text, table_title_p));
    col->children.push_back(std::move(table));
    return col;
}

ElementPtr page_servers(App& app) {
    const Palette& pal = app.host.palette();
    Props hint_p;
    hint_p.text = L"Click a row to open its details in the drawer.";
    hint_p.text_color = pal.text_secondary;
    hint_p.font_size_pt = tokens::font_caption;
    hint_p.text_align = Align::start;

    auto table = ui::data_table(pal, {L"Server", L"Status", L"Uptime"}, kServers,
                               -1, true, nullptr, app.selected_server.get(),
                               [&app](int r) {
                                   app.selected_server.set(r);
                                   ui::drawer_open(app.drawer_host);
                               });

    Props col_p;
    col_p.direction = Direction::column;
    col_p.gap = static_cast<float>(tokens::spacing_md);
    col_p.flex_grow = 1.0f;
    auto col = view(std::move(col_p));
    col->children.push_back(label(hint_p.text, hint_p));
    col->children.push_back(std::move(table));
    return col;
}

ElementPtr page_logs(App& app) {
    const Palette& pal = app.host.palette();
    Props sv_p;
    sv_p.height = 520.0f;
    sv_p.border_color = pal.border;
    sv_p.border_width = 1.0f;
    sv_p.radius = static_cast<float>(tokens::radius_md);
    sv_p.flex_grow = 1.0f;
    sv_p.on_scroll = [&app](float off) { app.virt_offset.set(off); };
    auto sv = scroll_view(nullptr, std::move(sv_p));
    sv->children.push_back(ui::virtual_list(pal, 5000, 26.0f, 520.0f,
        app.virt_offset.get(), [&](int i) {
            Props rp;
            const bool err = i % 97 == 0;
            rp.text = std::wstring(err ? L"[ERROR] " : L"[info]  ") +
                      L"2026-08-15 log line " + std::to_wstring(i);
            rp.text_color = err ? pal.danger : pal.text_secondary;
            rp.font_size_pt = tokens::font_caption;
            rp.text_align = Align::start;
            rp.height = 26.0f;
            rp.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm), 4.0f, 4.0f,
                                    static_cast<float>(tokens::spacing_sm)};
            return label(rp.text, rp);
        }));
    return sv;
}

ElementPtr page_billing(App& app) {
    const Palette& pal = app.host.palette();
    Props col_p;
    col_p.direction = Direction::column;
    col_p.gap = static_cast<float>(tokens::spacing_md);
    col_p.flex_grow = 1.0f;
    auto col = view(std::move(col_p));
    col->children.push_back(ui::bar_chart(
        pal, {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun", L"Jul"},
        {210, 260, 240, 310, 290, 340, 380}, 400.0f));
    col->children.push_back(ui::line_chart(
        pal, {5, 9, 7, 12, 10, 15, 13, 17, 16, 20}, Props{}));
    return col;
}

ElementPtr page_settings(App& app) {
    const Palette& pal = app.host.palette();
    auto mk_toggle = [&](const wchar_t* label_text, State<bool>& st) {
        Props row_p;
        row_p.direction = Direction::row;
        row_p.align = Align::center;
        row_p.gap = static_cast<float>(tokens::spacing_md);
        Props lbl_p;
        lbl_p.text = label_text;
        lbl_p.text_color = pal.text;
        lbl_p.font_size_pt = tokens::font_body;
        lbl_p.text_align = Align::start;
        lbl_p.flex_grow = 1.0f;
        auto row = view(std::move(row_p));
        row->children.push_back(label(label_text, lbl_p));
        row->children.push_back(toggle_switch(st.get(), [&st](bool v) { st.set(v); }));
        return row;
    };

    Props col_p;
    col_p.direction = Direction::column;
    col_p.gap = static_cast<float>(tokens::spacing_md);
    col_p.flex_grow = 1.0f;
    auto col = view(std::move(col_p));
    col->children.push_back(mk_toggle(L"Auto-refresh every 30s", app.auto_refresh));
    col->children.push_back(mk_toggle(L"Email alerts", app.email_alerts));
    col->children.push_back(ui::radio_group(pal, {L"Comfortable", L"Compact", L"Dense"},
                                            app.density.get(),
                                            [&app](int i) { app.density.set(i); }));
    col->children.push_back(ui::alert(pal, ui::AlertKind::info,
                                      L"Settings apply immediately — no save button."));

    // Wrapped paragraph + single-line ellipsis demos.
    Props wrap_p;
    wrap_p.text = L"This paragraph wraps at its laid-out width because "
                  L"text_wrap is on — resize the window and the line "
                  L"breaks follow.";
    wrap_p.text_color = pal.text_secondary;
    wrap_p.font_size_pt = tokens::font_body;
    wrap_p.text_align = Align::start;
    wrap_p.text_wrap = true;
    wrap_p.width = 320.0f;
    col->children.push_back(label(wrap_p.text, wrap_p));

    Props ell_p;
    ell_p.text = L"A very long single line that will not fit and ends with an ellipsis…";
    ell_p.text_color = pal.text_secondary;
    ell_p.font_size_pt = tokens::font_caption;
    ell_p.text_align = Align::start;
    ell_p.text_ellipsis = true;
    ell_p.width = 220.0f;
    col->children.push_back(label(ell_p.text, ell_p));
    return col;
}

ElementPtr build(App& app) {
    const Palette& pal = app.host.palette();

    Props root_p;
    root_p.direction = Direction::row;
    root_p.gap = 0.0f;
    root_p.background = pal.background;
    root_p.align = Align::stretch;
    root_p.flex_grow = 1.0f;
    auto root = view(std::move(root_p));

    // Shell: sidenav + page column.
    root->children.push_back(ui::sidenav(
        pal,
        {ui::NavEntry{L"Overview", IconKind::hamburger},
         ui::NavEntry{L"Servers", IconKind::inbox},
         ui::NavEntry{L"Logs", IconKind::search},
         ui::NavEntry{L"Billing", IconKind::calendar},
         ui::NavEntry{L"Settings", IconKind::user}},
        app.nav.get(), [&app](int i) { app.nav.set(i); },
        app.nav_collapsed.get(), [&app] { app.nav_collapsed.set(!app.nav_collapsed.get()); }));

    Props page_p;
    page_p.direction = Direction::column;
    page_p.gap = static_cast<float>(tokens::spacing_md);
    page_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg)};
    page_p.flex_grow = 1.0f;
    // Stretch: without it the column's cross axis (width) defaults to each
    // child's measured width — and a width-less scroll_view measures 0.
    page_p.align = Align::stretch;
    auto page = view(std::move(page_p));

    // Header: title + Ctrl+K hint.
    Props header_row_p;
    header_row_p.direction = Direction::row;
    header_row_p.align = Align::center;
    header_row_p.gap = static_cast<float>(tokens::spacing_md);
    auto header_row = view(std::move(header_row_p));
    Props title_p;
    title_p.text = L"Fleet Dashboard";
    title_p.text_color = pal.text;
    title_p.font_size_pt = tokens::font_title;
    title_p.bold = true;
    title_p.text_align = Align::start;
    title_p.flex_grow = 1.0f;
    header_row->children.push_back(label(L"Fleet Dashboard", title_p));

    Props kbtn_p;
    kbtn_p.text = L"Ctrl+K";
    kbtn_p.tooltip_text = L"Open the command palette";
    header_row->children.push_back(button(L"Ctrl+K", [&app] {
        app.cmd_host.open = true;
        app.cmd_host.query.clear();
        app.cmd_host.selected = 0;
        app.host.request_render();
    }, std::move(kbtn_p)));
    page->children.push_back(std::move(header_row));

    switch (app.nav.get()) {
    case 0: default: page->children.push_back(page_overview(app)); break;
    case 1: page->children.push_back(page_servers(app)); break;
    case 2: page->children.push_back(page_logs(app)); break;
    case 3: page->children.push_back(page_billing(app)); break;
    case 4: page->children.push_back(page_settings(app)); break;
    }
    root->children.push_back(std::move(page));

    // Overlays, re-declared every frame.
    Host& host = app.host;
    ui::overlay_toasts(host, app.toast_host, pal);
    ui::overlay_command_palette(host, app.cmd_host, pal,
        {ui::CommandItem{L"Go to Overview", [&app] { app.nav.set(0); }},
         ui::CommandItem{L"Go to Logs", [&app] { app.nav.set(2); }},
         ui::CommandItem{L"Go to Settings", [&app] { app.nav.set(4); }},
         ui::CommandItem{L"Push healthy toast", [&app] {
             ui::toast_show(app.toast_host, ui::ToastKind::success, L"All systems nominal");
         }},
         ui::CommandItem{L"Toggle theme", [&app] {
             app.host.set_theme_mode(app.host.theme_mode() == ThemeMode::light
                                         ? ThemeMode::dark
                                         : ThemeMode::light);
         }}});
    if (app.selected_server.get() >= 0 &&
        app.selected_server.get() < static_cast<int>(kServers.size())) {
        const auto& srv = kServers[static_cast<size_t>(app.selected_server.get())];
        Props body_p;
        body_p.text = srv[0] + L"\nStatus: " + srv[1] + L"\nUptime: " + srv[2] +
                       L"\nLast incident: none in the reporting window.";
        body_p.text_color = pal.text;
        body_p.font_size_pt = tokens::font_body;
        body_p.text_align = Align::start;
        body_p.text_wrap = true;
        body_p.width = 248.0f;   // wrap target (drawer width minus chrome)
        ui::overlay_drawer(host, app.drawer_host, pal,
                           label(body_p.text, body_p));
    } else {
        ui::overlay_drawer(host, app.drawer_host, pal, nullptr);
    }
    return root;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd_line, int) {
    enable_per_monitor_dpi_v2();

    // Optional initial page from the command line (0..4; e.g.
    // `WinFluxDashboard.exe 2` opens on Logs) — screenshot/perf harnesses
    // use this instead of synthetic clicks.
    int initial_page = 0;
    if (cmd_line != nullptr && *cmd_line != L'\0') {
        const int parsed = std::wcstol(cmd_line, nullptr, 10);
        if (parsed >= 0 && parsed <= 4) initial_page = parsed;
    }

    App app;
    app.nav            = app.host.make_state<int>(initial_page);
    app.nav_collapsed  = app.host.make_state<bool>(false);
    app.sort_col       = app.host.make_state<int>(0);
    app.sort_asc       = app.host.make_state<bool>(true);
    app.selected_server = app.host.make_state<int>(-1);
    app.virt_offset    = app.host.make_state<float>(0.0f);
    app.auto_refresh   = app.host.make_state<bool>(true);
    app.email_alerts   = app.host.make_state<bool>(false);
    app.density        = app.host.make_state<int>(0);
    app.host.set_root_builder([&app] { return build(app); });
    app.host.set_key_hook([&app](unsigned vk) {
        if (vk == 'K' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            app.cmd_host.open = !app.cmd_host.open;
            app.cmd_host.query.clear();
            app.host.request_render();
            return true;
        }
        return false;
    });

    Host::Config cfg;
    cfg.title = L"WinFlux Dashboard";
    cfg.width = 1180;
    cfg.height = 760;
    if (!app.host.create(cfg, instance)) return 1;
    return app.host.run();
}
