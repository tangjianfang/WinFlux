# AGENTS.md — entry point for AI agents and contributors

WinFlux is a C++20 / Win32 / Direct2D declarative UI library with a React-style
scene tree (Element / Props / `State<T>` / flex layout / theme tokens) and a
new DirectComposition presentation layer with damage-region repaint. This file
is the in-repo navigation and command reference so an agent does not have to
reverse-engineer build presets, module boundaries, or the design contract.

## Build & test

CMakePresets drive everything; pick the configuration explicitly:

```sh
cmake --preset x64-debug        # configure, output under out/build/x64-debug
cmake --build  --preset x64-debug
ctest    --preset x64-debug --output-on-failure
```

The Release flow mirrors with `x64-release`. Targets are x64-only (Visual
Studio 17 2022 generator). The compiler-options module (`cmake/WinFluxCompilerOptions.cmake`)
enforces `/W4 /permissive- /EHsc /utf-8 /FS /MP /Zc:__cplusplus` on every
target — keep the build zero-warning.

## Tests

Two CTest targets, intentionally split (see `tests/`):

| Target                     | What it covers                                                                                       |
|----------------------------|------------------------------------------------------------------------------------------------------|
| `WinFluxSmokeTest`         | Pure CPU: theme tokens, element builders, flex layout, **DamageTracker** diff, animation, scroll layout, overlay anchoring, flux::ui components, chart/tree/menu math. |
| `WinFluxHostLifecycleTest` | Creates a real HWND, exercises Host lifecycle, presenter selection, and paint round-trips (incl. scroll_view + overlay). |

Headless core checks always run in CI; the lifecycle target is gated behind
a window session (ctest still runs it — failure is isolated from smoke).

## Module map

| Layer           | Headers (public)                                   | Sources                                  |
|-----------------|----------------------------------------------------|------------------------------------------|
| Theme           | `include/flux/Theme.hpp`                           | `src/theme/Theme.cpp`                    |
| Element model   | `include/flux/Element.hpp`                         | (inline builders in `src/core/Host.cpp`) |
| Animation       | `include/flux/Animation.hpp`                       | (header-only: `Animated<T>`, curves)     |
| Flex layout     | `include/flux/FlexLayout.hpp`                      | `src/layout/FlexLayout.cpp`              |
| Presentation    | `include/flux/Presenter.hpp`, `DCompPresenter.hpp`, `HwndPresenter.hpp` | `src/render/DCompPresenter.cpp`, `HwndPresenter.cpp` |
| Rendering       | `include/flux/D2DRenderer.hpp`                     | `src/render/D2DRenderer.cpp`             |
| Damage          | `include/flux/Damage.hpp`                          | `src/render/Damage.cpp`                  |
| Host (window)   | `include/flux/Host.hpp`                            | `src/core/Host.cpp`                      |
| Components      | `include/flux/Components.hpp` (`flux::ui`)         | `src/components/*.cpp`                   |
| Platform glue   | `include/flux/flux.hpp`                            | `src/platform/AppHost.cpp`               |

The `flux::ui` component library layers on the primitives: navigation
(tabs/segmented/pagination/accordion/sidenav), feedback (alert/badge,
ToastHost/ModalHost overlays, host-native tooltip), selection & menus
(select/context menu/Ctrl+K command palette), layout containers
(splitter/drawer), data display (sortable table/tree view/virtualized
list), charts (line/area/pie/sparkline/gauge), and pickers (radio/colour/
date/calendar). Overlay components follow the "app-side host struct +
declaration from the root builder every frame" pattern — see
`samples/Dashboard/Dashboard.cpp` for a complete app assembled from them.

The Host owns the `DCompPresenter` (with `HwndPresenter` fallback) and a
`D2DRenderer` attached to whichever presenter is live. The renderer never
holds its own render target — it draws into the presenter's per-frame
`SurfaceFrame` and clears its brush cache on device-lost recovery.

## Design contract

Design tokens (spacing, radii, type ramp, animation timings) live in
`include/flux/Theme.hpp` and are mirrored verbatim in [`DESIGN.md`](DESIGN.md).
The smoke test asserts the in-repo values match the contract — keep both
files in sync if a token changes.

## What NOT to do without checking

- Do not bind D2D device resources outside the presenter (`D2DRenderer` no
  longer owns an `ID2D1HwndRenderTarget` or `ID2D1Factory`).
- Do not call `InvalidateRect` from `Host::request_render` — the presenter
  is driven directly and `ValidateRect` is used to suppress double-draw.
- Do not compare layout rects in `DamageTracker` before `flex_layout` has
  filled `x/y/w/h` on both trees.