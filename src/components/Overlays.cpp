// flux::ui overlay feedback components — alert / badge (static) and the
// toast + modal hosts (declarative overlays declared from the root builder
// every frame; see Components.hpp for the lifetime contract).

#include <flux/Components.hpp>

#include <vector>

namespace flux::ui {

// ---- Static feedback ----------------------------------------------------

ElementPtr alert(const Palette& pal, AlertKind kind, std::wstring text,
                  std::function<void()> on_dismiss, Props p) {
    Color stripe = pal.info;
    switch (kind) {
    case AlertKind::info:    stripe = pal.info;    break;
    case AlertKind::success: stripe = pal.success; break;
    case AlertKind::warning: stripe = pal.warning; break;
    case AlertKind::danger:  stripe = pal.danger;  break;
    }

    // [kind-coloured stripe][text][optional ×] — the stripe stretches to
    // full height via the row's cross-axis stretch.
    Props outer_p = std::move(p);
    outer_p.direction = Direction::row;
    outer_p.align = Align::stretch;
    outer_p.radius = static_cast<float>(tokens::radius_md);
    outer_p.background = pal.surface_variant;
    auto outer = view(std::move(outer_p));

    Props stripe_p;
    stripe_p.width = 4.0f;
    stripe_p.background = stripe;
    outer->children.push_back(view(std::move(stripe_p)));

    Props txt_p;
    txt_p.text_color = pal.text;
    txt_p.font_size_pt = tokens::font_body;
    txt_p.text_align = Align::start;
    txt_p.flex_grow = 1.0f;
    txt_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                               static_cast<float>(tokens::spacing_md),
                               static_cast<float>(tokens::spacing_sm),
                               static_cast<float>(tokens::spacing_md)};
    outer->children.push_back(label(text, std::move(txt_p)));

    if (on_dismiss) {
        Props x_p;
        x_p.icon = IconKind::close;
        x_p.width = 22.0f;
        x_p.height = 22.0f;
        x_p.text_color = pal.text_secondary;
        x_p.hover_background = pal.surface_hover;
        x_p.radius = static_cast<float>(tokens::radius_sm);
        x_p.tooltip_text = L"Dismiss";
        x_p.align = Align::center;
        ClickHandler dismiss = std::move(on_dismiss);
        outer->children.push_back(flux::button(L"", dismiss, std::move(x_p)));
    }
    return outer;
}

ElementPtr badge(const Palette& pal, BadgeKind kind, std::wstring text, Props p) {
    Color bg = pal.accent;
    Color fg = pal.on_accent;
    switch (kind) {
    case BadgeKind::neutral:  bg = pal.surface_variant; fg = pal.text_secondary; break;
    case BadgeKind::accent:   bg = pal.accent;          fg = pal.on_accent;      break;
    case BadgeKind::success:  bg = pal.success;                                  break;
    case BadgeKind::warning:  bg = pal.warning;                                  break;
    case BadgeKind::danger:   bg = pal.danger;                                   break;
    case BadgeKind::info:     bg = pal.info;                                     break;
    }

    Props lp = std::move(p);
    lp.background = bg;
    lp.text_color = fg;
    lp.padding = EdgeInsets{static_cast<float>(tokens::spacing_sm),
                            static_cast<float>(tokens::spacing_xs),
                            static_cast<float>(tokens::spacing_sm),
                            static_cast<float>(tokens::spacing_xs)};
    lp.radius = 999.0f;
    lp.font_size_pt = tokens::font_caption;
    return label(text, std::move(lp));
}

// ---- Toast host ---------------------------------------------------------

namespace {

Color toast_stripe(const Palette& pal, ToastKind kind) noexcept {
    switch (kind) {
    case ToastKind::info:    return pal.info;
    case ToastKind::success: return pal.success;
    case ToastKind::warning: return pal.warning;
    case ToastKind::danger:  return pal.danger;
    }
    return pal.info;
}

} // namespace

void toast_show(ToastHost& th, ToastKind kind, std::wstring text) noexcept {
    th.entries.push_back(ToastSpec{th.next_id++, GetTickCount64() + th.duration_ms,
                                   std::move(text), kind});
}

unsigned long long prune_toasts(ToastHost& th, unsigned long long now_ms) noexcept {
    unsigned long long next_deadline = 0;
    size_t w = 0;
    for (size_t i = 0; i < th.entries.size(); ++i) {
        if (th.entries[i].deadline_ms > now_ms) {
            th.entries[w++] = std::move(th.entries[i]);
            if (next_deadline == 0 || th.entries[w - 1].deadline_ms < next_deadline) {
                next_deadline = th.entries[w - 1].deadline_ms;
            }
        }
    }
    th.entries.resize(w);
    return next_deadline;
}

void overlay_toasts(Host& host, ToastHost& th, const Palette& pal) noexcept {
    const unsigned long long now = GetTickCount64();
    const size_t before = th.entries.size();
    const unsigned long long next_deadline = prune_toasts(th, now);
    if (th.entries.size() != before) host.request_render();
    if (th.entries.empty()) return;
    host.animate_for(static_cast<unsigned>(next_deadline - now) + 2u);

    Props root_p;
    root_p.direction = Direction::column;
    root_p.justify = Justify::end;
    root_p.align = Align::end;
    root_p.gap = static_cast<float>(tokens::spacing_sm);
    root_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg)};
    auto root = view(std::move(root_p));
    for (const ToastSpec& t : th.entries) {
        Props row_p;
        row_p.direction = Direction::row;
        row_p.align = Align::stretch;
        row_p.radius = static_cast<float>(tokens::radius_md);
        row_p.background = pal.surface;
        row_p.border_color = pal.border;
        row_p.border_width = 1.0f;
        row_p.shadow_elevation = 2;
        auto card = view(std::move(row_p));

        Props stripe_p;
        stripe_p.width = 4.0f;
        stripe_p.background = toast_stripe(pal, t.kind);
        card->children.push_back(view(std::move(stripe_p)));

        Props txt_p;
        txt_p.text_color = pal.text;
        txt_p.font_size_pt = tokens::font_caption;
        txt_p.text_align = Align::start;
        txt_p.padding = EdgeInsets{10.0f, 10.0f, 10.0f, 10.0f};
        txt_p.flex_grow = 1.0f;
        card->children.push_back(label(t.text, std::move(txt_p)));

        // Early-dismiss ×: removes this entry from the host by id.
        Props x_p;
        x_p.icon = IconKind::close;
        x_p.width = 20.0f;
        x_p.height = 20.0f;
        x_p.text_color = pal.text_secondary;
        x_p.hover_background = pal.surface_hover;
        x_p.radius = static_cast<float>(tokens::radius_sm);
        x_p.tooltip_text = L"Dismiss";
        const int tid = t.id;
        ClickHandler dismiss = [&th, tid] {
            std::erase_if(th.entries, [tid](const ToastSpec& e) { return e.id == tid; });
        };
        card->children.push_back(flux::button(L"", dismiss, std::move(x_p)));
        root->children.push_back(std::move(card));
    }
    host.overlay(std::move(root));
}

// ---- Modal host ---------------------------------------------------------

void modal_show(ModalHost& mh, std::wstring title, ElementPtr body,
                std::function<void()> on_close) noexcept {
    mh.current = ModalSpec{std::move(title), std::move(body), std::move(on_close)};
}

void modal_close(ModalHost& mh) noexcept {
    if (mh.current.has_value()) mh.current.reset();
}

void overlay_modal(Host& host, ModalHost& mh, const Palette& pal) noexcept {
    if (!mh.current.has_value()) return;

    Props scrim_p;
    scrim_p.background = rgba(0, 0, 0, 96);
    scrim_p.direction = Direction::column;
    scrim_p.justify = Justify::center;
    scrim_p.align = Align::center;
    auto close_it = [&mh] {
        if (mh.current->on_close) mh.current->on_close();
        mh.current.reset();
    };
    scrim_p.on_click = close_it;
    auto root = view(std::move(scrim_p));

    Props card_p;
    card_p.background = pal.surface;
    card_p.radius = static_cast<float>(tokens::radius_lg);
    card_p.width = 360.0f;
    card_p.direction = Direction::column;
    card_p.gap = static_cast<float>(tokens::spacing_md);
    card_p.padding = EdgeInsets{static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg),
                                static_cast<float>(tokens::spacing_lg)};
    card_p.shadow_elevation = 3;
    auto card = view(std::move(card_p));

    Props title_p;
    title_p.text_color = pal.text;
    title_p.font_size_pt = tokens::font_title;
    title_p.text_align = Align::start;
    card->children.push_back(label(mh.current->title, std::move(title_p)));

    // Copy (not move): the body lives in the app-owned ModalHost across
    // frames; moving it would null the host's copy after the first render.
    if (mh.current->body != nullptr) card->children.push_back(mh.current->body);

    Props close_p;
    close_p.background = pal.accent;
    close_p.hover_background = pal.accent_hover;
    close_p.text_color = pal.on_accent;
    card->children.push_back(flux::button(L"Close", close_it, std::move(close_p)));

    root->children.push_back(std::move(card));
    host.overlay(std::move(root));
}

} // namespace flux::ui
