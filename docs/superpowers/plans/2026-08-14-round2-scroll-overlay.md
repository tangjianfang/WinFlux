# Round 2 — 滚动 + 浮层地基 (Scroll & Overlay) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 库级 `scroll_view`（滚轮/裁剪/滚动条/拖拽/键盘/自动隐藏）+ 声明式浮层通道 `Host::overlay`（后绘、先命中、scrim、纯 flex 锚定、Esc 关闭）。

**Architecture:** 滚动偏移存 Host 侧按路径索引的注册表（同 hover 模式）；内容以自然尺寸布局后把 `(x, y − offset)` 烘焙进绝对坐标——复用现有 diff/裁剪/命中全链路，零特殊分支。浮层 = 每帧由 root builder 调用 `host.overlay(root)` 收集（声明式，与树重建同生命周期），布局铺满画布，渲染盖顶、命中优先，overlay 元素路径用 `{-1, idx, …}` 哨兵前缀区分。锚定（居中/贴角/锚矩形下方）全部用 spacer+justify 的纯 flex 组合。

**Tech Stack:** 同 Round 1。新增 token：`scrollbar_width=8`。`Color` 增加 alpha 通道（默认 255，全链路透明色支持）。

## Global Constraints

（同 Round 1：零警告双 preset、token 三处同步、新画笔字段进 paint_props_equal、逻辑单位、每任务一提交。）

---

### Task 1: Color alpha 通道

**Files:** `include/flux/Theme.hpp`（Color+rgba+lerp4）、`src/theme/Theme.cpp`（lerp_palette 不动——palette 色全不透明）、`src/render/D2DRenderer.cpp`（to_d2d 用 a、color_key 打包 alpha）、`tests/smoke.cpp`

- [ ] smoke 失败断言：`rgba(0,0,0,128).a == 128`、`lerp(rgba(0,0,0,0), rgba(0,0,0,255), 0.5f).a == 127 或 128`（四舍五入实现取整）、`rgb(1,2,3).a == 255`
- [ ] Color 加 `unsigned char a{255};`（`constexpr == default` 已覆盖）；新增 `constexpr Color rgba(r,g,b,a)`；`lerp` 四通道插值（round）
- [ ] `to_d2d` alpha = c.a/255；`color_key` = `(r<<24)|(g<<16)|(b<<8)|a`（unsigned，够 32 位）
- [ ] 双 preset + commit `Round 2: Color alpha channel + rgba()`

### Task 2: FlexLayout `justify`（主轴分布）

**Files:** `include/flux/Element.hpp`（enum Justify + Props.justify）、`src/layout/FlexLayout.cpp`（pass 3 起点）、`tests/smoke.cpp`

- [ ] smoke 失败断言（zero measure ctx）：row 容器 200 宽、两个 50 宽子项、justify=center → 子1.x==50；justify=end → 子2 右缘==200；column 同理纵向
- [ ] `enum class Justify { start, center, end };` + `Props.justify = Justify::start;`
- [ ] layout_node pass 3：`free_space > 0 && grow_sum == 0` 时起始 cursor = start:0 / center:free/2 / end:free（free_space 在 pass 2 后重算 avail_main − total）
- [ ] justify 为布局字段不进 paint_props_equal（同 direction）
- [ ] 双 preset + commit `Round 2: flex justify (start/center/end)`

### Task 3: `scrollbar_width` token

- [ ] smoke 断言 `tokens::scrollbar_width == 8`；Theme.hpp + DESIGN.md Motion 表加行
- [ ] commit `Round 2: scrollbar_width token`

### Task 4: `scroll_view` 元素 + 布局管线

**Files:** `include/flux/Element.hpp`（builder 声明）、`src/core/Host.cpp`（builder + 布局 walk + 注册表）、`include/flux/FlexLayout.hpp`/`.cpp`（measure 特判 + layout 跳过 + `layout_scroll_view`）、`tests/smoke.cpp`

**Interfaces:**
- `ElementPtr scroll_view(ElementPtr content, Props p = {})` — 单子契约；type `L"scroll_view"`
- `float layout_scroll_view(Element& sv, float& offset_y, const LayoutContext& ctx) noexcept` — 内容按视口宽/无界高布局，clamp offset，烘焙 `(content_box.x, content_box.y − offset)`；返回 content_h
- Host: `struct ScrollState { float offset_y; float content_h; float viewport_h; unsigned long long last_scroll_ms; };` + `std::map<Path, ScrollState> scrolls_;` + `void layout_scroll_views(Element& root, const LayoutContext&)`（收集路径、承载偏移、剪枝）

- [ ] smoke 失败断言：a) scroll_view 无显式高时 measure 期望 {w:0,h:0}（不泄漏子内容）b) 显式 100×200、内容 30 行×20 → layout_scroll_view 后 content_h==600、子.y 烘焙 == sv.y + pad − offset、offset clamp 到 400 c) offset 超界回夹
- [ ] FlexLayout：measure_impl 首行加 scroll_view 特判（当作 intrinsic {width.value_or(0), height.value_or(0)}+pad 的叶子）；layout_node 设完 x/y/w/h 后 `if (e.type == L"scroll_view") return;`
- [ ] `layout_scroll_view` 实现（内容无界高 1e6 布局 → content_h → clamp → 烘焙偏移遍历）
- [ ] Host::paint_frame：flex_layout 后调 `layout_scroll_views(*tree_, ctx)`
- [ ] builder：默认无样式，padding 交给调用方
- [ ] 双 preset + commit `Round 2: scroll_view element + content layout pipeline`

### Task 5: 滚动交互（wheel / 滚动条 / 键盘 / 自动隐藏）

**Files:** `src/core/Host.cpp`、`include/flux/Host.hpp`

- [ ] `handle_mouse_wheel(int delta, int px, int py)`：命中 → 沿路径向上找最近 scroll_view 且 content_h > viewport_h → `offset += (delta/120) * 3 * control_height_md`（delta 正=向上滚）→ clamp → 记 last_scroll_ms → `animate_for(motion_medium)`（驱动隐藏淡出）→ request_render
- [ ] wndproc 加 `WM_MOUSEWHEEL`
- [ ] render_element：scroll_view 分支——背景/边框按 props；children 循环前 `push_clip(视口)`、后 `pop_clip` + 画滚动条（track 3px 淡色、thumb 全宽按比例，alpha = approach_blend(自 last_scroll_ms 起 motion_medium 内可见)）
- [ ] thumb/track 拖拽：handle_lbutton_down 先查命中点是否落在某 scroll_view 的滚动条区（视口右缘 scrollbar_width+4px 内）→ `drag_scroll_ = {path, grab_offset}`；mouse_move 按着时按 thumb 几何反解 offset；track 空白点击 = 翻页
- [ ] 键盘：handle_key_down 中 focus_path_ 解析为 scroll_view 时处理 ↑↓(1 行)/PgUp/PgDn(视口)/Home/End
- [ ] 视觉验证（Task 8 demo 页一起）
- [ ] commit `Round 2: scroll interactions (wheel/bar/keys/auto-hide)`

### Task 6: 浮层通道 `Host::overlay`

**Files:** `include/flux/Host.hpp`、`src/core/Host.cpp`、`tests/smoke.cpp`

**Interfaces:**
- `void overlay(ElementPtr root) noexcept`（仅 root builder 内调用；每帧重置）
- 命中优先 overlay（顶层→底层），再主树；overlay 元素路径 = `{-1, idx, …}`
- overlay 根有 `background`（scrim，用 rgba 半透明）或命中其子 → 拦截；否则穿透
- Esc → 顶层 overlay 根的 `on_click`（关闭语义）
- overlay 数量变化 → 全脏；同数量 → 逐个 damage diff 追加

- [ ] smoke 失败断言（纯 flex 锚定，zero ctx）：a) 居中：root column justify=center align=center，480×280 卡片在 1000×600 → 卡片中心 == (500,300) b) 贴角：column justify=end align=end + pad → 卡片右下角 == (1000−p, 600−p) c) 锚定矩形 (100,60,200,30) 下方：spacer(h=90) + row{spacer(w=100), menu} → menu.x==100 && menu.y==90
- [ ] Host 成员 `std::vector<ElementPtr> frame_overlays_, prev_overlays_;` + `overlay()`（builder 期间 append；paint_frame 开头 clear）
- [ ] paint_frame：主树布局后逐个 `flex_layout(*ov, canvas_w, canvas_h)`；damage：数量不同→force 全脏，否则逐 idx `damage_tracker_.compute(prev, next)` 追加（超 max_rects*2 全脏）；快照 prev_overlays_
- [ ] 绘制：每个 dirty rect 主树之后升序渲染 overlay，路径前缀 {-1, i}
- [ ] `hit_test_all()`：overlay 反向优先（root 内先子后根；根拦截条件=有 background；miss→下一层），miss 全部→主树；`resolve()` 支持 -1 哨兵；hover/press/focus/click/collect_focusable 全部走 hit_test_all/resolve（含 overlay）
- [ ] `handle_key_down`：VK_ESCAPE → 顶层 overlay 根 on_click
- [ ] commit `Round 2: overlay channel (draw-on-top, hit-first, scrim, Esc)`

### Task 7: host_lifecycle 覆盖

- [ ] root builder 加 scroll_view（20 行）+ `host.overlay(...)` 调用 → request_render round-trip 两次 + check 通过
- [ ] commit `Round 2: lifecycle test covers scroll_view + overlay`

### Task 8: Gallery「Scroll & Layer」页 + 收口

- [ ] 新 tab idx 7：a) 30 行长列表 scroll_view（高 360）b) 真·Modal（scrim + 居中卡 + Close/Esc）c) 贴角 Toast 栈（2s 自动过期——builder 检查 deadline + animate_for）d) 锚定弹出（按钮下沿弹出菜单预览，点外部关闭）
- [ ] 初始 tab 命令行参数扩到 7；`WinFluxGallery.exe 7` 启动 + PrintWindow 截图验证（列表裁剪/滚动条/遮罩/贴角）
- [ ] 双 preset ctest + commit `Round 2: Gallery Scroll & Layer demo`（收口）

## 验收清单

- [ ] 滚轮/拖 thumb/键盘都能滚动且裁剪正确、自动隐藏淡出
- [ ] Modal 有半透明 scrim、点击外部/Esc 关闭、内容可交互
- [ ] 贴角 Toast 自动过期；锚定弹出定位正确
- [ ] 全部 smoke + lifecycle 双 preset 绿；现有 Gallery 各页无回归
