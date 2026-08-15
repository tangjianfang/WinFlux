# Round 8 — 清理债 + 浮层滚动 + CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Checkbox tracking.

**Goal:** 一次性收掉三个遗留方向：①评审 cleanup 债（浮层框/弹出卡/菜单项/caption 等六处分叉收拢为共享 helper）②`scroll_view` 在浮层内的布局支持 ③GitHub Actions CI（双 preset 构建 + 测试）。

**Architecture:** 共享 helper 落在两层：锚定浮层根 `anchored_overlay_root()` 提为 `flux` 命名空间 builder（Host tooltip 与 flux::ui 共用，无反向依赖）；其余（popup_card / kind_color / menu_item_button / caption / icon_button）为 `flux::ui` 内部共享（Components.hpp 声明 + Overlays.cpp 实现）。浮层滚动：`layout_scroll_views` 增走 overlay 根（哨兵路径键，注册表/滚轮/渲染链路全部天然兼容）。CI 用 preset 直跑，GUI 生命周期测试经 `WINFLUX_SKIP_GUI_TESTS` 显式豁免（runner 会话无保证）。

## Tasks

- [ ] 8a 共享 helper：`flux::anchored_overlay_root(ax, ay, on_outside, content)`（Element.hpp/Host.cpp，tooltip 改用）；`ui::popup_card(pal, elevation, width)`、`ui::kind_color(pal, AlertKind)`（ToastKind 复用映射）、`ui::menu_item_button(pal, text, selected, on_click, height)`、`ui::caption(pal, text, Props)`、`ui::icon_button(pal, IconKind, tooltip, on_click, size)`；重写 Overlays/Menus/Layout/DataViz/Charts 内部分叉点；命令面板去掉 matches_copy 快照（按键时重算）；filter_commands 用 towlower 简化。smoke：helper 结构断言
- [ ] 8b 浮层内滚动：paint_frame 对每个 overlay 根跑 `layout_scroll_views(root, {-1, i}, ctx, next)`；滚轮/键盘/拖拽链路核对（命中路径已带哨兵，注册表键一致即可用）；Gallery 演示：modal 带滚动长文 body。smoke：无新增纯函数（布局链路靠 lifecycle 已有 round-trip + 手动验证）
- [ ] 8c CI：`.github/workflows/ci.yml`（windows-latest，双 preset configure/build/ctest）；host_lifecycle 尊重 `WINFLUX_SKIP_GUI_TESTS`（跳过并打印原因）；AGENTS.md 注明
- [ ] 收口：全 tab 视觉走查 + 双 preset 全绿 + 提交

## 验收
- [ ] Menus/Overlays/Layout/Host 中不再有分叉的锚定/卡片/菜单项样板
- [ ] modal 内 scroll_view 可滚
- [ ] CI 工作流文件就绪（推送后自动跑）
- [ ] 双 preset 零警告全绿
