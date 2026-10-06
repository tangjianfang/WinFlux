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

// Inline banner with a kind-coloured stripe; a non-null on_dismiss adds
// an "×" close affordance on the right.
[[nodiscard]] ElementPtr alert(const Palette& pal, AlertKind kind,
                               std::wstring text,
                               std::function<void()> on_dismiss = nullptr,
                               Props p = {});

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

// ---- Form field + validators ---------------------------------------------------

// Caption + input + (optional) red error caption column. Give the input
// Props.input_error = !error_text.empty() for the danger border.
[[nodiscard]] ElementPtr field(const Palette& pal, std::wstring caption,
                               ElementPtr input, std::wstring error_text = {},
                               Props p = {});

// Return the message when invalid, empty when OK.
[[nodiscard]] std::wstring validate_not_empty(
    const std::wstring& value, std::wstring message) noexcept;
[[nodiscard]] std::wstring validate_email(
    const std::wstring& value, std::wstring message) noexcept;

// ---- Shared chrome helpers -----------------------------------------------------

// Kind -> palette colour (alert and toast kinds share the mapping).
[[nodiscard]] Color kind_color(const Palette&, AlertKind) noexcept;
[[nodiscard]] Color kind_color(const Palette&, ToastKind) noexcept;
// Floating-card chrome: surface + border + radius_md + shadow elevation.
[[nodiscard]] ElementPtr popup_card(const Palette&, int elevation, float width = 0.0f,
                                    Props p = {});
// The selected/unselected menu-row button used by dropdowns, menus and the
// command-palette result list.
[[nodiscard]] ElementPtr menu_item_button(const Palette&, const std::wstring& text,
                                          bool selected, ClickHandler on_click,
                                          float height = 28.0f,
                                          float pad_h = static_cast<float>(12));
// Secondary-text caption label.
[[nodiscard]] ElementPtr caption(const Palette&, std::wstring text, Props p = {});
// Icon-only button with tooltip + hover.
[[nodiscard]] ElementPtr icon_button(const Palette&, IconKind kind, std::wstring tip,
                                     ClickHandler on_click, float size = 28.0f);

// ---- App chrome (F23) -------------------------------------------------------
// Bottom status bar: first section stretches left, the rest pack right.
[[nodiscard]] ElementPtr status_bar(const Palette&, std::vector<std::wstring> sections,
                                    Props p = {});
// Row of icon buttons from (icon, tooltip, on_click) triples.
struct ToolbarAction {
    IconKind icon;
    std::wstring tooltip;
    std::function<void()> on_click;
};
[[nodiscard]] ElementPtr toolbar(const Palette&, std::vector<ToolbarAction> actions,
                                 Props p = {});
// Loading placeholder: `lines` staircase-width shimmer bars (static v1).
[[nodiscard]] ElementPtr skeleton(const Palette&, int lines, Props p = {});
// Centred icon + title + hint for empty lists/filters.
[[nodiscard]] ElementPtr empty_state(const Palette&, IconKind kind, std::wstring title,
                                     std::wstring hint, Props p = {});

// ---- Charts ----------------------------------------------------------------
// All charts draw through the Props::paint escape hatch; `paint_id`
// derives from the series so unchanged charts keep the partial-repaint
// fast path. Geometry helpers are pure functions (smoke-tested).

struct PieSlice {
    float start_deg = 0.0f;
    float sweep_deg = 0.0f;
};

[[nodiscard]] std::pair<float, float> value_range(const std::vector<float>& values) noexcept;
// Index of the nearest series point for a normalized x position (0..1).
[[nodiscard]] size_t nearest_index(const std::vector<float>& values, float t) noexcept;
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

// Radar (spider) chart: one polygon per series over the shared axes.
[[nodiscard]] std::vector<std::pair<float, float>> radar_points(
    const std::vector<float>& normalized, float cx, float cy, float r) noexcept;
[[nodiscard]] ElementPtr radar_chart(const Palette& pal, std::vector<std::wstring> axes,
                                     std::vector<std::vector<float>> series,
                                     Props p = {});

// Scatter plot: value pairs mapped into the plot rect (auto-ranged).
[[nodiscard]] std::pair<float, float> scatter_map(
    float vx, float vy, float x, float y, float w, float h, float xmin, float xmax,
    float ymin, float ymax) noexcept;
[[nodiscard]] ElementPtr scatter_chart(const Palette& pal,
                                       std::vector<std::pair<float, float>> points,
                                       Props p = {});

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
    std::wstring hint;   // right-aligned accelerator hint ("Ctrl+T", F7)
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

// ---- Pickers & data viz (promoted from Gallery) ------------------------------

[[nodiscard]] int days_in_month(int year, int month) noexcept;
[[nodiscard]] int first_weekday(int year, int month) noexcept;   // 0=Sunday

[[nodiscard]] ElementPtr radio_group(const Palette& pal, std::vector<std::wstring> options,
                                     int selected, std::function<void(int)> on_change,
                                     Props p = {});
[[nodiscard]] ElementPtr color_picker(const Palette& pal, std::vector<Color> swatches,
                                      int selected, std::function<void(int)> on_change,
                                      int per_row = 6, Props p = {});
[[nodiscard]] ElementPtr date_picker(const Palette& pal, int year, int month, int day,
                                     std::function<void(int, int, int)> on_change,
                                     Props p = {});

[[nodiscard]] ElementPtr bar_chart(const Palette& pal,
                                   const std::vector<std::wstring>& labels,
                                   const std::vector<float>& values, float max_value,
                                   Props p = {});
[[nodiscard]] ElementPtr avatar_group(const Palette& pal,
                                      const std::vector<Color>& backgrounds,
                                      const std::vector<std::wstring>& initials,
                                      int max_visible = 4, Props p = {});
[[nodiscard]] ElementPtr breadcrumbs(const Palette& pal,
                                     const std::vector<std::wstring>& items,
                                     std::function<void(int)> on_select, Props p = {});
[[nodiscard]] ElementPtr calendar(const Palette& pal, int year, int month,
                                  int selected_day,
                                  std::function<void(int)> on_month_delta,
                                  std::function<void(int)> on_select_day, Props p = {});

} // namespace flux::ui
