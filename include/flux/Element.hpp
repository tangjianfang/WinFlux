#pragma once

// WinFlux element model. An Element is an immutable description of one
// scene-tree node (React "virtual DOM" analogue). Component builder
// functions (view/label/button/checkbox/progress) are the JSX analogue:
// pure functions returning element descriptions. The Host rebuilds the tree
// from the root builder every frame, then lays it out and paints it —
// there is no per-control HWND anywhere.

#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Theme.hpp"

namespace flux {

enum class Direction { row, column };
enum class Align { start, center, end, stretch };

// Per-element mouse cursor (WM_SETCURSOR resolves the hovered element's
// effective cursor walking up from the hit target; arrow is the default).
enum class Cursor { arrow, hand, ibeam, size_ns, size_we, cross };

// 24x24 stroke-grid icon ids, drawn as D2D path geometry (no bitmap/WIC).
enum class IconKind {
    chevron_down, chevron_up, chevron_left, chevron_right,
    close, check, search, plus, minus, hamburger,
    user, trash, edit, arrow_right, calendar, inbox,
    play, pause, skip_prev, skip_next,
    info, success, warning, danger,
};

// Horizontal left-to-right background gradient (v1: no angle — nothing
// needs a diagonal yet).
struct Gradient {
    Color from{};
    Color to{};
    [[nodiscard]] constexpr bool operator==(const Gradient&) const noexcept = default;
};

struct EdgeInsets {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

using ClickHandler = std::function<void()>;
using TextChangeHandler = std::function<void(std::wstring)>;
using ValueChangeHandler = std::function<void(float)>;

struct Props {
    // Layout
    std::optional<float> width;
    std::optional<float> height;
    float flex_grow = 0.0f;
    Direction direction = Direction::column;
    Align align = Align::start;   // cross-axis alignment of children
    float gap = 0.0f;
    EdgeInsets padding{};
    EdgeInsets margin{};

    // Paint
    std::optional<Color> background;
    std::optional<Color> hover_background;
    std::optional<Color> pressed_background;
    std::optional<Color> disabled_background;
    std::optional<Color> border_color;
    float border_width = 0.0f;
    float radius = 0.0f;
    float opacity = 1.0f;                          // self-draw alpha, 0..1
    std::optional<Gradient> background_gradient;   // wins over background
    int shadow_elevation = 0;                      // 0..3 hand-drawn falloff
    std::optional<IconKind> icon;                  // icon element payload, or inline glyph on button/label

    // Text
    std::wstring text;
    float font_size_pt = tokens::font_body;
    std::optional<Color> text_color;
    std::optional<Color> disabled_text_color;
    bool bold = false;
    Align text_align = Align::center;

    // Interaction
    ClickHandler on_click;
    TextChangeHandler on_text_change;
    ValueChangeHandler on_value_change;
    bool focusable = false;
    bool enabled = true;
    Cursor cursor = Cursor::arrow;

    // Typed-element payloads
    std::optional<bool> checked;   // present => checkbox glyph rendering
    float progress_value = 0.0f;   // 0..1, used by progress()
    float slider_value = 0.0f;     // 0..1 normalised, used by slider()
    float slider_min = 0.0f;       // domain minimum, used by slider()
    float slider_max = 1.0f;       // domain maximum, used by slider()
};

struct Element {
    std::wstring type;   // "view" | "label" | "button" | "checkbox" | "progress" | "text_input" | "slider" | "toggle"
    Props props;
    std::vector<std::shared_ptr<Element>> children;

    // Layout output — absolute coordinates in logical px, filled by the
    // layout pass and consumed by paint + hit-test in the same frame.
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

using ElementPtr = std::shared_ptr<Element>;

// Component builders (the React-function analogues).
[[nodiscard]] ElementPtr view(Props p = {});
[[nodiscard]] ElementPtr label(std::wstring text, Props p = {});
[[nodiscard]] ElementPtr button(std::wstring text, ClickHandler on_click, Props p = {});
[[nodiscard]] ElementPtr checkbox(std::wstring text, bool checked,
                                  std::function<void(bool)> on_change, Props p = {});
[[nodiscard]] ElementPtr progress(float value, Props p = {});
[[nodiscard]] ElementPtr icon(IconKind kind, Props p = {});

// Interactive primitives beyond button/checkbox. Each owns its own element
// type so the paint branch can pick the right glyph; builders take the
// current value plus a callback so the host-side State<T> stays the source
// of truth (props.* are per-frame snapshots).
[[nodiscard]] ElementPtr text_input(std::wstring value, std::function<void(std::wstring)> on_change,
                                    Props p = {});
[[nodiscard]] ElementPtr slider(float value, float vmin, float vmax,
                                std::function<void(float)> on_change, Props p = {});
[[nodiscard]] ElementPtr toggle_switch(bool on, std::function<void(bool)> on_change, Props p = {});

[[nodiscard]] ElementPtr with_children(ElementPtr parent,
                                       std::initializer_list<ElementPtr> children);

} // namespace flux
