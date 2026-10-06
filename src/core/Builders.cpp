#include <flux/Element.hpp>

#include <algorithm>
#include <memory>
#include <utility>

// Component builders (review 2026-10-06 06-R3 step 1): extracted verbatim
// from Host.cpp — the Host keeps the frame pipeline, input handling, and
// painting; the builders are pure Element construction.

namespace flux {

// =====================================================================
// Component builders
// =====================================================================

namespace {
ElementPtr make(std::wstring type, Props p) {
    auto e = std::make_shared<Element>();
    e->type = std::move(type);
    e->props = std::move(p);
    return e;
}
} // namespace

ElementPtr view(Props p) { return make(L"view", std::move(p)); }

ElementPtr label(std::wstring text, Props p) {
    p.text = std::move(text);
    p.text_align = Align::start;
    return make(L"label", std::move(p));
}

ElementPtr button(std::wstring text, ClickHandler on_click, Props p) {
    p.text = std::move(text);
    p.on_click = std::move(on_click);
    p.focusable = true;
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::control_height_md);
    if (p.padding.left == 0.0f && p.padding.right == 0.0f) {
        p.padding.left = static_cast<float>(tokens::spacing_md);
        p.padding.right = static_cast<float>(tokens::spacing_md);
    }
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_control);
    if (!p.text_color.has_value()) p.text_color = Color{255, 255, 255};
    p.cursor = Cursor::hand;
    return make(L"button", std::move(p));
}

ElementPtr checkbox(std::wstring text, bool checked, std::function<void(bool)> on_change, Props p) {
    p.text = std::move(text);
    p.checked = checked;
    p.focusable = true;
    auto handler = std::move(on_change);
    p.on_click = [handler, checked] {
        if (handler) handler(!checked);
    };
    return make(L"checkbox", std::move(p));
}

ElementPtr progress(float value, Props p) {
    p.progress_value = std::clamp(value, 0.0f, 1.0f);
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::progress_height);
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_sm);
    return make(L"progress", std::move(p));
}

ElementPtr image(std::wstring path, Props p) {
    // F10: WIC-decoded file, drawn aspect-fit. Intrinsic size comes from the
    // decoded dimensions (see measure_leaf); an undecodable path measures
    // 64x64 and draws nothing.
    p.image_path = std::move(path);
    return make(L"image", std::move(p));
}

ElementPtr icon(IconKind kind, Props p) {
    p.icon = kind;
    if (!p.width.has_value()) p.width = 24.0f;
    if (!p.height.has_value()) p.height = 24.0f;
    return make(L"icon", std::move(p));
}

ElementPtr scroll_view(ElementPtr content, Props p) {
    p.focusable = true;
    auto e = make(L"scroll_view", std::move(p));
    if (content != nullptr) e->children.push_back(std::move(content));
    return e;
}

ElementPtr text_input(std::wstring value, std::function<void(std::wstring)> on_change, Props p) {
    // The text input owns its visual chrome. The change handler is stored in
    // on_text_change; the host's wndproc dispatches WM_CHAR/VK_BACK to the
    // element at focused_input_path_ by calling that handler with the new
    // text. A click focuses the input — see handle_lbutton_down.
    p.text = std::move(value);
    p.focusable = true;
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::control_height_md);
    if (p.padding.left == 0.0f) p.padding.left = static_cast<float>(tokens::spacing_md);
    if (p.padding.right == 0.0f) p.padding.right = static_cast<float>(tokens::spacing_sm);
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_control);
    if (!p.background.has_value()) p.background = std::optional<Color>{};
    if (!p.border_color.has_value()) p.border_color = std::optional<Color>{};
    if (p.border_width == 0.0f) p.border_width = 1.0f;
    if (!p.text_color.has_value()) p.text_color = std::optional<Color>{};
    if (p.text_align == Align::center) p.text_align = Align::start;
    p.cursor = Cursor::ibeam;
    p.on_text_change = std::move(on_change);
    return make(L"text_input", std::move(p));
}

ElementPtr slider(float value, float vmin, float vmax,
                  std::function<void(float)> on_change, Props p) {
    p.slider_min = vmin;
    p.slider_max = vmax;
    if (vmax > vmin) {
        p.slider_value = (std::clamp(value, vmin, vmax) - vmin) / (vmax - vmin);
    } else {
        p.slider_value = 0.0f;
    }
    p.focusable = true;
    if (!p.height.has_value()) p.height = static_cast<float>(tokens::control_height_md);
    if (p.radius == 0.0f) p.radius = static_cast<float>(tokens::radius_control);
    // Click + drag both route through on_value_change: handle_lbutton_down
    // and the mouse_move-while-pressed branch both compute the normalised x
    // and invoke this handler with the domain value.
    p.cursor = Cursor::size_we;
    p.on_value_change = std::move(on_change);
    return make(L"slider", std::move(p));
}

ElementPtr toggle_switch(bool on, std::function<void(bool)> on_change, Props p) {
    p.checked = on;
    p.focusable = true;
    if (!p.height.has_value()) p.height = 24.0f;
    if (!p.width.has_value()) p.width = 44.0f;
    p.on_click = [handler = std::move(on_change), on] {
        if (handler) handler(!on);
    };
    return make(L"toggle", std::move(p));
}

ElementPtr with_children(ElementPtr parent, std::initializer_list<ElementPtr> children) {
    if (parent != nullptr) {
        parent->children.reserve(parent->children.size() + children.size());
        for (const auto& c : children) parent->children.push_back(c);
    }
    return parent;
}

} // namespace flux
