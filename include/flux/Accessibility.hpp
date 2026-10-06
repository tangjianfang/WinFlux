#pragma once

// UIA accessibility bridge, level 0 (review 2026-10-06, X-12 / 08-F12):
// a window-level IRawElementProviderSimple so screen readers see a named
// window instead of nothing. Level 1 (per-element name/role/Invoke peers
// driven by the element tree) builds on this — the Host owns one provider
// and answers WM_GETOBJECT with it.

#include <windows.h>
#include <ole2.h>   // the project defines WIN32_LEAN_AND_MEAN — UIA needs COM basics
#include <UIAutomation.h>
#include <memory>
#include <string>

namespace flux::a11y {

// Creates (lazily, once per window) the window-level provider. The name is
// refreshed from the live window title on every UIA query, so rename works.
[[nodiscard]] IRawElementProviderSimple* window_provider(HWND hwnd) noexcept;

// Call on WM_DESTROY for the HWND that owns providers — disconnects them
// from UIA so late client queries can't touch freed memory.
void shutdown_for(HWND hwnd) noexcept;

// Focus-change announcement (EVENT_OBJECT_FOCUS) — Narrator/NVDA follow it.
void notify_focus(HWND hwnd) noexcept;

} // namespace flux::a11y
