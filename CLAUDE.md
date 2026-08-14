# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

WinFlux is a C++20 / Win32 / Direct2D declarative UI library: a React-style scene tree
(Element / Props / `State<T>` / flex layout / theme tokens) rendered into a single HWND
through a DirectComposition presentation layer with damage-region repaint. There are no
per-control HWNDs.

See [`AGENTS.md`](AGENTS.md) for the module/file map and [`DESIGN.md`](DESIGN.md) for the
design-token contract. This file covers the cross-cutting invariants that are not obvious
from any single file.

## Build & test

```sh
cmake --preset x64-debug          # configure -> out/build/x64-debug
cmake --build  --preset x64-debug
ctest    --preset x64-debug --output-on-failure
```

`x64-release` mirrors it. x64-only, Visual Studio 17 2022 generator. Binaries land in
`out/build/<preset>/<Debug|Release>/`.

Every target gets `/W4 /permissive- /EHsc /utf-8 /FS /MP /Zc:__cplusplus` via
`cmake/WinFluxCompilerOptions.cmake` — **keep the build zero-warning**.

### Running one test

Both test targets are plain `main()` executables with a hand-rolled `expect()`/`check()`
counter — no test framework, so there is **no name filter**. `ctest -R <name>` selects a
target, not a case:

```sh
ctest --preset x64-debug -R WinFluxSmokeTest --output-on-failure
out/build/x64-debug/Debug/WinFluxSmokeTest.exe      # same thing, direct
```

To isolate a single case, comment out the other `test_*()` calls in `main()` — do not add a
framework for this.

| Target | Scope |
|---|---|
| `WinFluxSmokeTest` | Pure CPU / headless: theme, builders, flex layout, DamageTracker. Always must pass. |
| `WinFluxHostLifecycleTest` | Creates a real HWND: Host lifecycle, presenter selection, paint round-trip. Needs a window session. |

The split is deliberate: a GUI-session failure in the lifecycle target must not mask the
always-fast headless checks.

## Frame pipeline

`Host::paint_frame()` (`src/core/Host.cpp`) is the whole render model:

```
root_builder_()  ->  flex_layout()  ->  DamageTracker::compute()  ->  presenter frame
  rebuild tree      fill x/y/w/h        diff vs prev_tree_          draw dirty rect only
```

The tree is rebuilt from scratch **every frame**. Key consequences:

- **`request_render()` paints synchronously.** It calls `paint_frame()` directly, then
  `ValidateRect` to suppress the follow-up `WM_PAINT`. Do not "fix" this by calling
  `InvalidateRect` — the presenter is driven directly by design. `painting_` guards re-entrancy.
- **Element identity is positional.** `hover_path_` / `pressed_path_` / `focus_path_` are
  `std::vector<int>` child-index paths (`Host::resolve`). They survive a rebuild only while the
  tree *shape* is stable. A builder that conditionally omits a child shifts every later sibling's
  identity.
- **Handlers fire after the repaint.** `handle_lbutton_up` clears the pressed state and renders
  *before* invoking `on_click`, so a handler that mutates `State<T>` re-enters `request_render()`
  cleanly.

## Damage tracking — the biggest footgun

`DamageTracker` (`src/render/Damage.cpp`) diffs two laid-out trees by walking children in
parallel *by index*, pushing both the old and new rect of each changed node, merging overlaps,
inflating by 1px for AA, and collapsing to fullscreen if the root type changed or the count
exceeds `max_rects` (8). An empty result means **the frame is skipped entirely**.

Two things must stay in sync with it, or changes silently fail to repaint:

1. **Interaction state is invisible to the diff.** Hover / pressed / focus live in `Host` paths,
   not in the element tree, and are applied at render time in `Host::render_element`. Two
   consecutive trees are byte-identical when only hover moved, so the diff returns empty and
   nothing redraws. **Every mutation of `hover_path_` / `pressed_path_` / `focus_path_` must set
   `force_full_dirty_ = true` before `request_render()`.** The same applies to resize, DPI change,
   and theme fade (which also forces full via `fading_`).

2. **New paintable `Props` fields must be added to `DamageTracker::paint_props_equal`.** It
   compares an explicit field list. A field you forget is a field whose change never triggers a
   repaint. Layout-only fields (`flex_grow`, `direction`, `padding`, `margin`, `gap`, `align`) are
   deliberately excluded — they surface as different `x/y/w/h`.

Never compare layout rects before `flex_layout` has run on *both* trees.

## Presentation layer

`Host` owns a `DCompPresenter` (default) and an `HwndPresenter` (fallback when DComp creation
fails); `presenter_` points at whichever is live, chosen once in `Host::create`. `D2DRenderer`
does **not** own a render target — it is `attach`ed to an `IPresenter` and draws into the
per-frame `SurfaceFrame`. Do not bind D2D device resources outside the presenter.

`SurfaceFrame::offsetX/Y` is the `IDCompositionSurface::BeginDraw` update offset in *physical*
pixels; the renderer subtracts it from the world transform so logical coordinates land correctly
inside a sub-allocated surface. Device loss (`D2DERR_RECREATE_TARGET`) rebuilds D2D + surface and
clears the renderer's brush/format caches.

**Coordinates: every public API — layout, draw calls, dirty rects, `IPresenter` — is in logical
units at a 96 DPI baseline.** Only the presenter and `Host::to_logical_px` (which converts raw
`WM_MOUSEMOVE` pixels) touch physical pixels.

## Layout

`flex_layout` is a single main-axis pass per node: desired sizes → distribute `flex_grow` (or
shrink proportionally when overflowing) → position and resolve cross axis. **No flex-wrap.**
Leaf content comes from `LayoutContext::measure_leaf`, supplied by the Host — that lambda also
encodes composite sizing (e.g. a checkbox is `checkbox_box + spacing_sm + text width`).

## Adding an element type

A new type touches several files at once:

1. Builder declared in `include/flux/Element.hpp`, defined in `src/core/Host.cpp` (builders live
   there, not in a separate file).
2. Paint branch in `Host::render_element`, keyed off `e.type` (a `std::wstring`).
3. `measure_leaf` in `Host::paint_frame` if its intrinsic size is not just text.
4. `DamageTracker::paint_props_equal` if it added a `Props` field.
5. `DESIGN.md` **and** `include/flux/Theme.hpp` together if it added a token — the smoke test
   asserts they match, and the tokens are a shared contract with the sibling nfui library.

## Repo notes

- Root-level `*.ps1` files are untracked scratch from a screenshot pixel-diffing session (they
  read PNGs from a temp dir). They are not build inputs and not referenced by CMake.
- `.qoder/` is generated tooling output, also untracked.
