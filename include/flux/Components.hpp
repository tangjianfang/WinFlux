#pragma once

// WinFlux component library — reusable compositions over the element
// primitives, promoted from the Gallery sample (Round 3). Builders are
// pure functions: spec + state in, ElementPtr + callbacks out. There is
// deliberately no App/Host dependency in the static components; the
// overlay-based ones (toast/modal) take the Host& because they declare
// overlays from the root builder every frame.

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Animation.hpp"
#include "Element.hpp"
#include "Host.hpp"

namespace flux::ui {

// ---- Navigation ---------------------------------------------------------

// Underlined tab strip (active tab: accent + bold). Returns the strip +
// divider only — callers compose their own body panel below.
[[nodiscard]] ElementPtr tabs(const Palette& pal, std::vector<std::wstring> labels,
                              int active, std::function<void(int)> on_change,
                              Props p = {});

// Compact button group; the selected segment fills with the accent.
[[nodiscard]] ElementPtr segmented(const Palette& pal,
                                   std::vector<std::wstring> options, int active,
                                   std::function<void(int)> on_change, Props p = {});

// "< 1 2 3 .. n >" pager. `current` is clamped; prev/next disable at the
// edges.
[[nodiscard]] ElementPtr pagination(const Palette& pal, int current, int total_pages,
                                    std::function<void(int)> on_change, Props p = {});

struct AccordionSection {
    std::wstring title;
    ElementPtr body;   // shown when this section is open
};

// One-open-at-a-time accordion (open_index == -1 => all closed).
[[nodiscard]] ElementPtr accordion(const Palette& pal,
                                   std::vector<AccordionSection> sections,
                                   int open_index,
                                   std::function<void(int)> on_toggle, Props p = {});

// ---- Static feedback ----------------------------------------------------

enum class AlertKind { info, success, warning, danger };

// Inline banner with a kind-coloured stripe and an optional dismiss "x".
[[nodiscard]] ElementPtr alert(const Palette& pal, AlertKind kind,
                               std::wstring text, Props p = {});

enum class BadgeKind { neutral, accent, success, warning, danger, info };

// Small pill chip (counts, status).
[[nodiscard]] ElementPtr badge(const Palette& pal, BadgeKind kind,
                               std::wstring text, Props p = {});

// ---- Overlay feedback ---------------------------------------------------

enum class ToastKind { info, success, warning, danger };

struct ToastSpec {
    int id = 0;
    unsigned long long deadline_ms = 0;
    std::wstring text;
    ToastKind kind = ToastKind::info;
};

// App-side toast queue. toast_show() from any handler; overlay_toasts()
// from the ROOT BUILDER every frame (it prunes expired entries, arms the
// animation clock, and declares the bottom-right corner overlay).
struct ToastHost {
    std::vector<ToastSpec> entries;
    int next_id = 0;
    unsigned duration_ms = 2600;
};

void toast_show(ToastHost&, ToastKind, std::wstring text) noexcept;
void overlay_toasts(Host&, ToastHost&, const Palette&) noexcept;
// Pure prune step (headless-testable): drops expired entries, returns the
// earliest remaining deadline (0 when empty).
[[nodiscard]] unsigned long long prune_toasts(ToastHost&,
                                              unsigned long long now_ms) noexcept;

struct ModalSpec {
    std::wstring title;
    ElementPtr body;   // optional; may be nullptr
    std::function<void()> on_close;
};

// App-side modal slot. modal_show() from any handler; overlay_modal() from
// the ROOT BUILDER every frame (scrim + centred card; Esc / scrim click /
// the card's Close wiring all funnel to ModalSpec::on_close).
struct ModalHost {
    std::optional<ModalSpec> current;
};

void modal_show(ModalHost&, std::wstring title, ElementPtr body,
                std::function<void()> on_close) noexcept;
void modal_close(ModalHost&) noexcept;
void overlay_modal(Host&, ModalHost&, const Palette&) noexcept;

// ---- Charts ----------------------------------------------------------------
// All charts draw through the Props::paint escape hatch; `paint_id`
// derives from the series so unchanged charts keep the partial-repaint
// fast path. Geometry helpers are pure functions (smoke-tested).

struct PieSlice {
    float start_deg = 0.0f;
    float sweep_deg = 0.0f;
};

[[nodiscard]] std::pair<float, float> value_range(const std::vector<float>& values) noexcept;
[[nodiscard]] std::vector<std::pair<float, float>> line_points(
    const std::vector<float>& values, float x, float y, float w, float h) noexcept;
[[nodiscard]] std::vector<PieSlice> pie_slices(const std::vector<float>& values) noexcept;
[[nodiscard]] std::vector<std::pair<float, float>> arc_points(
    float cx, float cy, float r, float start_deg, float sweep_deg,
    float step_deg) noexcept;

[[nodiscard]] ElementPtr line_chart(const Palette& pal, std::vector<float> values,
                                    Props p = {});
[[nodiscard]] ElementPtr area_chart(const Palette& pal, std::vector<float> values,
                                    Props p = {});
[[nodiscard]] ElementPtr sparkline(const Palette& pal, std::vector<float> values,
                                   Props p = {});
[[nodiscard]] ElementPtr pie_chart(const Palette& pal, std::vector<float> values,
                                   std::vector<std::wstring> labels, Props p = {});
[[nodiscard]] ElementPtr gauge(const Palette& pal, float value /* 0..1 */,
                               std::wstring caption, Props p = {});

// ---- Selection & menus ------------------------------------------------------

// Dropdown select. The trigger button is part of the app's tree; the open
// list is an overlay anchored at SelectHost::ax/ay. Apps set the anchor in
// their on_open handler (typically from Host::last_click_pos()).
struct SelectHost {
    bool open = false;
    int selected = 0;
    float ax = 0.0f;
    float ay = 0.0f;
};

[[nodiscard]] ElementPtr select(const Palette& pal, const SelectHost&,
                                std::vector<std::wstring> options,
                                std::function<void(int)> on_change,
                                std::function<void(bool)> on_open, Props p = {});
void overlay_select(Host&, SelectHost&, const Palette&,
                    std::vector<std::wstring> options,
                    std::function<void(int)> on_change) noexcept;

struct MenuItem {
    std::wstring label;   // "-" renders a separator
    std::function<void()> on_activate;
};

struct MenuHost {
    bool open = false;
    float ax = 0.0f;
    float ay = 0.0f;
    std::vector<MenuItem> items;
};

void menu_show_at(MenuHost&, std::vector<MenuItem> items, float x, float y) noexcept;
void menu_close(MenuHost&) noexcept;
void overlay_menu(Host&, MenuHost&, const Palette&) noexcept;

struct CommandItem {
    std::wstring title;
    std::function<void()> run;
};

struct CommandHost {
    bool open = false;
    std::wstring query;
    int selected = 0;
};

// Case-insensitive substring filter over the command titles.
[[nodiscard]] std::vector<size_t> filter_commands(const std::vector<CommandItem>&,
                                                  const std::wstring& query) noexcept;

// Ctrl+K palette: near-top-centred card, autofocus query input (WM_CHAR
// works right after opening), arrow/Enter/Esc via Props.on_key, first 8
// results. Apps open it from Host::set_key_hook.
void overlay_command_palette(Host&, CommandHost&, const Palette&,
                             std::vector<CommandItem> items) noexcept;

// ---- Layout containers --------------------------------------------------------

// Two panes split by a draggable handle. `ratio` (0.05..0.95) is pane a's
// share of the main axis; drag reports the new ratio through on_ratio.
[[nodiscard]] ElementPtr splitter(const Palette& pal, ElementPtr a, ElementPtr b,
                                  float ratio, std::function<void(float)> on_ratio,
                                  bool vertical = false);

struct DrawerHost {
    bool open = false;
    Animated<float> slide{0.0f};
    float width = 320.0f;
};

void drawer_open(DrawerHost&) noexcept;
void drawer_close(DrawerHost&) noexcept;
// Right-edge slide-in panel (ease_out in / ease_in-out out). Stays
// declared while the closing tween is still in flight.
void overlay_drawer(Host&, DrawerHost&, const Palette&, ElementPtr content) noexcept;

struct NavEntry {
    std::wstring label;
    IconKind icon = IconKind::chevron_right;
};

// Collapsible icon rail: entries keep tooltips when collapsed.
[[nodiscard]] ElementPtr sidenav(const Palette& pal, std::vector<NavEntry> entries,
                                 int active, std::function<void(int)> on_change,
                                 bool collapsed, std::function<void()> on_toggle);

// ---- Data display ---------------------------------------------------------------

struct VisibleRange {
    int first = 0;
    int count = 0;
};

// Visible-row window for a fixed-row-height virtualized list (pure).
[[nodiscard]] VisibleRange visible_range(float offset_y, float viewport_h,
                                         float row_h, int total_rows,
                                         int overscan) noexcept;

// Lexicographic column sort (stable, toggling direction). Pure — exported
// for tests and for apps that need the same ordering elsewhere.
[[nodiscard]] std::vector<std::vector<std::wstring>> table_sorted(
    const std::vector<std::vector<std::wstring>>& rows, int col, bool asc) noexcept;

// Sortable, selectable table. Header clicks toggle the sort column; row
// clicks select. Wrap in a scroll_view for long bodies.
[[nodiscard]] ElementPtr data_table(
    const Palette& pal, std::vector<std::wstring> columns,
    std::vector<std::vector<std::wstring>> rows, int sort_col, bool sort_asc,
    std::function<void(int)> on_sort, int selected_row,
    std::function<void(int)> on_select, Props p = {});

struct TreeItem {
    std::wstring label;
    std::vector<TreeItem> children;
};

// Indented tree; nodes toggle open/closed through `expanded` (index paths),
// leaves report their path through on_select.
[[nodiscard]] ElementPtr tree_view(
    const Palette& pal, std::vector<TreeItem> items,
    std::set<std::vector<int>> expanded,
    std::function<void(std::vector<int>)> on_toggle,
    std::function<void(std::vector<int>)> on_select, Props p = {});

// Content builder for a virtualized scroll list: leading spacer of
// `first * row_h` rows + only the visible window of rows. Feed the
// offset from Props::on_scroll; the spacer keeps the scrollbar honest.
[[nodiscard]] ElementPtr virtual_list(const Palette& pal, int total_rows, float row_h,
                                      float viewport_h, float offset_y,
                                      std::function<ElementPtr(int)> make_row,
                                      Props p = {});

} // namespace flux::ui
