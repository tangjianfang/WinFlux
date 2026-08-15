// flux::ui selection & menus — select dropdown, context menu, and the
// Ctrl+K command palette. All three follow the overlay-host pattern: an
// app-side host struct holds the state, show/overlay functions mutate and
// declare it, and the overlay roots close on outside-click / Esc via the
// transparent-root on_click idiom.

#include <flux/Components.hpp>

#include <algorithm>
#include <cwctype>

namespace flux::ui {

namespace {

// Transparent full-canvas root anchored at (ax, ay): spacer(h) + row{spacer(w), content}.
// The root's on_click closes on any outside click; Esc reaches it too
// (Host dispatches Esc to the topmost overlay root).
ElementPtr anchored_popup(float ax, float ay, ClickHandler on_outside,
                          ElementPtr content) {
    Props root_p;
    root_p.direction = Direction::column;
    root_p.on_click = std::move(on_outside);
    auto root = view(std::move(root_p));

    Props gap_p;
    gap_p.height = ay + 4.0f;
    root->children.push_back(view(std::move(gap_p)));

    Props row_p;
    row_p.direction = Direction::row;
    auto row = view(std::move(row_p));
    Props indent_p;
    indent_p.width = ax;
    row->children.push_back(view(std::move(indent_p)));
    row->children.push_back(std::move(content));
    root->children.push_back(std::move(row));
    return root;
}

ElementPtr menu_panel(const Palette& pal) {
    auto card = popup_card(pal, 2);
    card->props.padding = EdgeInsets{4.0f, 4.0f, 4.0f, 4.0f};
    return card;
}

} // namespace

// ---- Select ----------------------------------------------------------------

ElementPtr select(const Palette& pal, const SelectHost& sel,
                  std::vector<std::wstring> options, std::function<void(int)> /*on_change*/,
                  std::function<void(bool)> on_open, Props p) {
    Props bp = std::move(p);
    const std::wstring current = (sel.selected >= 0 &&
                                  sel.selected < static_cast<int>(options.size()))
                                     ? options[static_cast<size_t>(sel.selected)]
                                     : L"—";
    bp.text = current;
    bp.text_color = pal.text;
    bp.background = pal.surface;
    bp.hover_background = pal.surface_hover;
    bp.border_color = pal.border;
    bp.border_width = 1.0f;
    if (!bp.width.has_value()) bp.width = 180.0f;
    if (!bp.height.has_value()) bp.height = static_cast<float>(tokens::control_height_md);
    bp.icon = IconKind::chevron_down;
    bp.text_align = Align::start;
    bp.tooltip_text = L"Opens a dropdown list";
    return flux::button(current, [on_open, open = sel.open] {
        if (on_open) on_open(!open);
    }, std::move(bp));
}

void overlay_select(Host& host, SelectHost& sel, const Palette& pal,
                    std::vector<std::wstring> options,
                    std::function<void(int)> on_change) noexcept {
    if (!sel.open || options.empty()) return;

    auto panel = menu_panel(pal);
    for (size_t i = 0; i < options.size(); ++i) {
        const int idx = static_cast<int>(i);
        panel->children.push_back(menu_item_button(
            pal, options[i], idx == sel.selected,
            [&sel, on_change, idx] {
                sel.open = false;
                if (on_change) on_change(idx);
            }, 28.0f, static_cast<float>(tokens::spacing_sm)));
    }
    host.overlay(anchored_popup(sel.ax, sel.ay, [&sel] { sel.open = false; },
                                std::move(panel)));
}

// ---- Context menu ------------------------------------------------------------

void menu_show_at(MenuHost& menu, std::vector<MenuItem> items, float x,
                  float y) noexcept {
    menu.open = true;
    menu.ax = x;
    menu.ay = y;
    menu.items = std::move(items);
}

void menu_close(MenuHost& menu) noexcept {
    menu.open = false;
    menu.items.clear();
}

void overlay_menu(Host& host, MenuHost& menu, const Palette& pal) noexcept {
    if (!menu.open) return;
    auto panel = menu_panel(pal);
    for (auto& item : menu.items) {
        if (item.label == L"-") {
            Props sep_p;
            sep_p.height = 1.0f;
            sep_p.background = pal.divider;
            sep_p.margin = EdgeInsets{0.0f, 3.0f, 0.0f, 3.0f};
            panel->children.push_back(view(std::move(sep_p)));
            continue;
        }
        auto activate = item.on_activate;
        panel->children.push_back(menu_item_button(
            pal, item.label, false, [&menu, activate] {
                menu_close(menu);
                if (activate) activate();
            }, 28.0f, static_cast<float>(tokens::spacing_md)));
    }
    host.overlay(anchored_popup(menu.ax, menu.ay, [&menu] { menu_close(menu); },
                                std::move(panel)));
}

// ---- Command palette ---------------------------------------------------------

std::vector<size_t> filter_commands(const std::vector<CommandItem>& items,
                                    const std::wstring& query) noexcept {
    std::vector<size_t> out;
    // Empty query: everything, in order.
    if (query.empty()) {
        for (size_t i = 0; i < items.size(); ++i) out.push_back(i);
        return out;
    }
    const auto lower = [](std::wstring s) {
        for (wchar_t& c : s) c = static_cast<wchar_t>(std::towlower(c));
        return s;
    };
    const std::wstring q = lower(query);
    for (size_t i = 0; i < items.size(); ++i) {
        // Locale-aware case-insensitive substring match.
        if (lower(items[i].title).find(q) != std::wstring::npos) out.push_back(i);
    }
    return out;
}

void overlay_command_palette(Host& host, CommandHost& cmd, const Palette& pal,
                             std::vector<CommandItem> items) noexcept {
    if (!cmd.open) return;

    Props root_p;
    root_p.direction = Direction::column;
    root_p.on_click = [&cmd] { cmd.open = false; };
    auto root = view(std::move(root_p));

    // Card near the top centre, modal-lite (no scrim; click-outside closes).
    Props wrap_p;
    wrap_p.height = 72.0f;   // top spacer
    root->children.push_back(view(std::move(wrap_p)));

    Props center_p;
    center_p.direction = Direction::row;
    center_p.justify = Justify::center;
    auto center = view(std::move(center_p));

    auto card = popup_card(pal, 3, 460.0f);
    card->props.radius = static_cast<float>(tokens::radius_lg);

    // Query input (autofocus drives WM_CHAR here right after Ctrl+K) with
    // arrow/enter/esc handling on the same element (Props.on_key).
    const std::vector<size_t> matches_copy = filter_commands(items, cmd.query);
    // NOTE: the key handler re-filters on press (captures the query, not the
    // snapshot) so arrow/Enter always match what's rendered.
    Props input_p;
    input_p.background = std::nullopt;
    input_p.border_color = std::nullopt;
    input_p.text_color = pal.text;
    input_p.font_size_pt = tokens::font_subtitle;
    input_p.tooltip_text = L"";
    input_p.autofocus = true;
    input_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_sm),
                                 static_cast<float>(tokens::spacing_md),
                                 static_cast<float>(tokens::spacing_sm)};
    input_p.on_key = [&cmd, matches_copy, items](unsigned vk) {
        if (vk == VK_ESCAPE) {
            cmd.open = false;
        } else if (vk == VK_UP && !matches_copy.empty()) {
            cmd.selected = (cmd.selected + static_cast<int>(matches_copy.size()) - 1) %
                           static_cast<int>(matches_copy.size());
        } else if (vk == VK_DOWN && !matches_copy.empty()) {
            cmd.selected = (cmd.selected + 1) % static_cast<int>(matches_copy.size());
        } else if (vk == VK_RETURN && !matches_copy.empty()) {
            if (cmd.selected >= static_cast<int>(matches_copy.size()))
                cmd.selected = 0;
            const size_t item = matches_copy[static_cast<size_t>(cmd.selected)];
            cmd.open = false;
            if (item < items.size() && items[item].run) items[item].run();
        }
        // The key handler lives in props of an element that will be
        // replaced next frame — request the repaint that re-reads state.
    };
    card->children.push_back(text_input(cmd.query, [&cmd](std::wstring q) {
        cmd.query = std::move(q);
        cmd.selected = 0;
    }, std::move(input_p)));

    // Result list (first 8).
    const size_t shown = std::min<size_t>(matches_copy.size(), 8);
    if (shown == 0) {
        Props none_p;
        none_p.text = L"No matching commands";
        none_p.text_color = pal.text_secondary;
        none_p.font_size_pt = tokens::font_caption;
        none_p.text_align = Align::start;
        none_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_md), 0.0f, 0.0f,
                                    static_cast<float>(tokens::spacing_md)};
        card->children.push_back(label(none_p.text, none_p));
    } else {
        Props list_p;
        list_p.direction = Direction::column;
        list_p.padding = EdgeInsets{4.0f, 4.0f, 4.0f, 4.0f};
        auto list = view(std::move(list_p));
        for (size_t m = 0; m < shown; ++m) {
            const size_t item_idx = matches_copy[m];
            auto run = items[item_idx].run;
            auto row = menu_item_button(
                pal, items[item_idx].title,
                static_cast<int>(m) == cmd.selected, [&cmd, run] {
                    cmd.open = false;
                    if (run) run();
                }, 30.0f, static_cast<float>(tokens::spacing_sm));
            row->props.radius = static_cast<float>(tokens::radius_sm);
            list->children.push_back(std::move(row));
        }
        card->children.push_back(std::move(list));
    }

    // Hint row.
    Props hint_p;
    hint_p.text = L"↑↓ navigate   Enter run   Esc close";
    hint_p.text_color = pal.text_secondary;
    hint_p.font_size_pt = tokens::font_caption;
    hint_p.text_align = Align::end;
    hint_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                                static_cast<float>(tokens::spacing_sm),
                                static_cast<float>(tokens::spacing_sm),
                                static_cast<float>(tokens::spacing_sm)};
    card->children.push_back(label(hint_p.text, hint_p));

    center->children.push_back(std::move(card));
    root->children.push_back(std::move(center));
    host.overlay(std::move(root));
}

} // namespace flux::ui
