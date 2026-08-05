# DESIGN.md — WinFlux design token contract

This file is the in-repo source of truth for the design tokens that the
WinFlux UI library uses. `include/flux/Theme.hpp` mirrors these values
verbatim; the smoke test asserts they stay in sync.

> Message: `tests/smoke.cpp` line 45 — *"design tokens match the DESIGN.md contract"*.

The token names below are exactly the C++ identifiers in the
`flux::tokens` namespace. Any change here must also change `Theme.hpp` and
re-run the smoke test.

## Spacing scale (8-pt grid)

| Token                | Value (logical px) |
|----------------------|--------------------|
| `spacing_xs`         | `4`                |
| `spacing_sm`         | `8`                |
| `spacing_md`         | `16`               |
| `spacing_lg`         | `24`               |
| `spacing_xl`         | `32`               |

## Control heights

| Token                | Value (logical px) |
|----------------------|--------------------|
| `control_height_sm`  | `24`               |
| `control_height_md`  | `32`               |
| `control_height_lg`  | `40`               |

## Corner radii

| Token             | Value (logical px) |
|-------------------|--------------------|
| `radius_sm`       | `4`                |
| `radius_control`  | `6`                |
| `radius_md`       | `8`                |
| `radius_card`     | `10`               |
| `radius_lg`       | `12`               |

## Type ramp (points, 96 DPI baseline)

| Token            | Value |
|------------------|-------|
| `font_caption`   | `12.0`|
| `font_body`      | `14.0`|
| `font_subtitle`  | `16.0`|
| `font_title`     | `20.0`|
| `font_hero`      | `28.0`|

## Motion & component dimensions

| Token              | Value         | Meaning                                       |
|--------------------|---------------|-----------------------------------------------|
| `theme_fade_ms`    | `200`         | Theme-mode cross-fade duration (ms).          |
| `checkbox_box`     | `18`          | Checkbox glyph box (logical px).              |
| `progress_height`  | `10`          | Progress bar track height (logical px).       |

## Palettes

Three palettes — `light`, `dark`, `high_contrast` (WCAG-AAA profile, 7:1
text contrast) — are produced by `flux::palette(ThemeMode)` in
`src/theme/Theme.cpp`. The `high_contrast` background must be pure black
and its text pure white (`#000000` / `#FFFFFF`); the smoke test enforces
this.

## Anti-guidelines

- Do not introduce a 6-pt grid step. Everything is on the 8-pt grid above.
- Do not introduce new type sizes without extending both this file and
  `Theme.hpp` together.
- Do not change a radius without re-running `WinFluxSmokeTest` — it is the
  shared visual contract between WinFlux and the sibling nfui library.