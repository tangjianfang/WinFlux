#pragma once

// WinFlux component library — reusable compositions over the element
// primitives, promoted from the Gallery sample (Round 3). Builders are
// pure functions: spec + state in, ElementPtr + callbacks out. There is
// deliberately no App/Host dependency in the static components; the
// overlay-based ones (toast/modal) take the Host& because they declare
// overlays from the root builder every frame.

#include <functional>
#include <optional>
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

} // namespace flux::ui
