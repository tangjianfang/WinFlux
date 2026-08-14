# Round 3 — 组件库成立 + Gallery 瘦身 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `flux::ui` 组件层（`include/flux/Components.hpp` + `src/components/`）：从 Gallery 提升 9 件组件（Tabs/Accordion/SegmentedControl/Pagination/Alert/Badge 参数化迁移；Modal/Toast/Tooltip 基于浮层重写真件），Gallery 瘦身为纯展示。

**Architecture:** 纯 builder 函数（规格+状态进、ElementPtr+回调出、无 App& 依赖）。浮层组件走「app 侧 Host 状态 + root builder 每帧声明」模式：`ToastHost`/`ModalHost` 存数据，`overlay_toasts(host, th)`/`overlay_modal(host, mh)` 在 root builder 里调用。Tooltip 做成**宿主原生**：`Props.tooltip_text` + Host hover 延迟自建浮层（组件零参与）。

**Tech Stack:** 既有原语 + Round 1/2 能力（overlay/justify/Animated/Color alpha）。

## Global Constraints

（同前：零警告双 preset、token 三处同步、新画笔字段进 paint_props_equal、每任务一提交。）

---

### Task 1: Components.hpp 骨架 + Navigation 组件（tabs/segmented/pagination/accordion）

**Files:** Create `include/flux/Components.hpp`、`src/components/Navigation.cpp`；Modify `CMakeLists.txt`（flux 库源列表加 `src/components/Navigation.cpp`，实现时读现文件确认列表写法）；Test `tests/smoke.cpp`

**Interfaces:**
```cpp
namespace flux::ui {
struct TabSpec { std::wstring label; IconKind icon = /*none sentinel?*/; };  // v1: 无图标字段，仅 label
ElementPtr tabs(std::vector<std::wstring> labels, int active,
                std::function<void(int)> on_change, Props p = {});
ElementPtr segmented(std::vector<std::wstring> options, int active,
                     std::function<void(int)> on_change, Props p = {},
                     Animated<float>* thumb = nullptr);   // 滑块 x 位移动画（app 侧持有）
ElementPtr pagination(int current, int total_pages,
                      std::function<void(int)> on_change, Props p = {});
struct AccordionSection { std::wstring title; ElementPtr body; };
ElementPtr accordion(std::vector<AccordionSection> sections, int open_index,
                     std::function<void(int)> on_toggle, Props p = {});
}
```
行为契约：迁移 Gallery.cpp 同名函数（`tabs` L743、`segmented_control` L861、`pagination` L893、`accordion` L793）的视觉实现，参数化：去掉 App&，样式全部来自 `Host::palette()`？——**组件无 Host 访问**，Palette 由 Props 传入？妥协：Palette 作首参（组件库消费者必有 palette）。

- [x] smoke：a) `tabs({A,B}, 1, cb)` 返回子数=2 且每个子 on_click 调 cb(0)/cb(1)（直接调 props.on_click 断言回调值）b) `accordion` open_index=0 时 section0 有 body 子、section1 无 c) pagination(2,5) 生成 1..5 + prev/next 且回调边界 clamp
- [x] 实现四组件（Palette 首参版：`tabs(const Palette&, std::vector<std::wstring>, int, Fn<void(int)>, Props={}`）
- [x] segmented 滑块动画：thumb 位置 = Animated<float>（若传入）的 current()（app 调 host.animate() 后传入），否则直接 active 定位；Gallery 用法演示
- [x] 双 preset + commit `Round 3: flux::ui Navigation (tabs/segmented/pagination/accordion)`

### Task 2: Overlays 静态组件（alert/badge）

**Files:** `src/components/Overlays.cpp`（新）；Components.hpp 声明

```cpp
enum class AlertKind { info, success, warning, danger };
ElementPtr alert(const Palette&, AlertKind, std::wstring text, Props p = {});
enum class BadgeKind { neutral, accent, success, warning, danger };
ElementPtr badge(const Palette&, BadgeKind, std::wstring text, Props p = {});
```
迁移 Gallery `alert` L935 / `badge` L990；kind→色映射（info/success/warning/danger → palette 对应色）。

- [x] smoke：alert(info) 有 background 且 danger 与 info 背景色不同；badge(accent) 宽度含 text
- [x] 双 preset + commit `Round 3: flux::ui alert + badge`

### Task 3: ToastHost + ModalHost（浮层真件）

**Files:** `src/components/Overlays.cpp`、Components.hpp

```cpp
struct ToastSpec { int id; unsigned long long deadline_ms; std::wstring text; ToastKind kind; };
enum class ToastKind { info, success, warning, danger };
struct ToastHost {
    std::vector<ToastSpec> entries; int next_id = 0; unsigned duration_ms = 2600;
};
void toast_show(ToastHost&, ToastKind, std::wstring);
// root builder 每帧调用：过期清理 + animate_for + 贴角 overlay 声明
void overlay_toasts(Host&, ToastHost&, const Palette&);

struct ModalSpec { std::wstring title; ElementPtr body; std::function<void()> on_close; };
struct ModalHost { std::optional<ModalSpec> current; };
void modal_show(ModalHost&, std::wstring title, ElementPtr body, std::function<void()> on_close);
void modal_close(ModalHost&);
// root builder 每帧调用：scrim + 居中卡 + Esc/scrim 关闭
void overlay_modal(Host&, ModalHost&, const Palette&);
```
迁移 Gallery Round 2 手写逻辑（declare_layer_overlays 的 modal/toast 分支为蓝本），入场淡入用 Props.opacity + Animated<float>（ModalHost/ToastHost 内嵌 Animated<float> fade；overlay_* 每帧读 host.animate()）。

- [x] smoke：toast_show 后 entries.size()==1 且 id 自增；overlay_toasts 过期裁剪逻辑（deadline 已过的条目消失）——直接调 overlay_toasts 需要 Host……拆纯函数 `prune_expired(ToastHost&, now)` 测试
- [x] 双 preset + commit `Round 3: flux::ui ToastHost + ModalHost overlays`

### Task 4: 宿主原生 Tooltip（Props.tooltip_text）

**Files:** Element.hpp（`std::wstring tooltip_text;`——交互字段，**不进** paint_props_equal）、Host.hpp/.cpp

- [x] smoke：tooltip_text 差异不触发 paint-dirty（paint_props_equal 相等）
- [x] Host：`tooltip_armed_ms_`（hover 稳定阈值 = motion_slow_ms）、`tooltip_shown_` {text,x,y,shown}；handle_mouse_move：hover 稳定超阈值且 resolve(hover) 有 tooltip_text → 记录位置（鼠标 +12,+16）；paint_frame builder 之后：若 shown → `frame_overlays_.push_back(内置 tooltip 卡)`（透明根 + spacer 锚定 + surface 卡 + border + shadow_elevation 2 + caption 文字）；hover 离开/无文本 → 不显示。淡入：opacity 由 approach_blend(hover_stable)——复用 animate_for
- [x] commit `Round 3: host-native tooltip (Props.tooltip_text)`

### Task 5: Gallery 瘦身接库

**Files:** `samples/Gallery/Gallery.cpp`、CMakeLists（WinFluxGallery 链接 flux 已有，无改）

- [x] patterns_left/right_section 与 overlays_section 改调 `flux::ui::*`；删除本地 tabs/accordion/segmented_control/pagination/alert/badge/toast_bar/modal_card/tooltip_demo 与 Phase E 假浮层状态（toasts/modal_open/tooltip_visible/tooltip_hint 改走 ToastHost/ModalHost/tooltip_text）
- [x] Scroll & Layer 页的 declare_layer_overlays 改用 overlay_toasts/overlay_modal（保留 anchored popup 演示）
- [x] Gallery 行数目标 ≤ ~2600（从 3100+）
- [x] seed 模式扩：modal/toast 走新组件；视觉验证 Patterns/Overlays/Scroll 三 tab 无回归（截图对比）
- [x] 双 preset ctest + commit `Round 3: Gallery consumes flux::ui (slim-down)`

### Task 6: Round 3 收口

- [x] 全量双 preset + ctest + Gallery 全 tab 截图走查（tab 0-8）
- [x] 计划勾选 + commit

## 验收清单

- [x] `flux::ui` 9 组件可用且 Gallery 消费
- [x] Modal/Toast 为真浮层（scrim/贴角/自动过期/Esc）
- [x] tooltip 原生悬停出现（Gallery 按钮 tooltip_text 演示）
- [x] Gallery 减重 ≥ 400 行
- [x] 双 preset 零警告 + 测试全绿 + 无视觉回归
