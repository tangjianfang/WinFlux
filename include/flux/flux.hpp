#pragma once

// WinFlux umbrella header.
// A C++20 / Win32 / Direct2D declarative UI library with a React-like
// component model: one HWND per window, scene-tree rendering, flex layout,
// palette-driven theming. See docs/DIRECT_RENDERING_UI_PLAN.md in the
// NativeFrameUI repository for the design rationale.

#include "Theme.hpp"
#include "Element.hpp"
#include "FlexLayout.hpp"
#include "D2DRenderer.hpp"
#include "Host.hpp"

namespace flux {

// Platform helpers (src/platform/AppHost.cpp).
bool enable_per_monitor_dpi_v2() noexcept;
int run_message_loop() noexcept;

} // namespace flux
