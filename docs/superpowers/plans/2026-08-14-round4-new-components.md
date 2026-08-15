# Round 4 — 新组件四批 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`) tracking.

**Goal:** 四批新组件全部以 `flux::ui` 库形态落地：选择菜单（Select/Menu/ContextMenu/CommandPalette）、布局容器（Splitter/Drawer/SideNav）、数据展示（DataTable/TreeView/ListView 性能演示 + `on_scroll` 虚拟化钩子）、图表（折线/面积/饼/Sparkline/Gauge）；顺带提升 Gallery 剩余组件（RadioGroup/ColorPicker/DatePicker/Calendar/BarChart/AvatarGroup/Breadcrumb）。

**Architecture:** 浮层组件沿 ToastHost/ModalHost 模式（app 侧 host 状态 + root builder 声明 overlay，锚点 = last_click_pos）。通用新能力：`Props.on_key`（focused 元素键盘回调，CommandPalette 箭头/回车需要）、`Props.drag_value`（按住拖动泛化，Splitter 需要）、`Props.on_scroll`（scroll_view 滚动回调，虚拟化钩子）、`WM_CONTEXTMENU`。图表加 renderer `draw_polyline/draw_polygon/draw_pie_slice`，点/扇形生成器做成可测纯函数。

## Global Constraints

（同前：零警告双 preset、新画笔字段进 paint_props_equal、每批一提交。）

---

### T1: 图表渲染地基 + 点生成纯函数
- [x] renderer：`draw_polyline(std::vector<D2D1_POINT_2F>→用 float 对, Color, stroke, alpha)`、`draw_polygon(closed+fill)`、`draw_pie_slice(cx,cy,r,start,sweep,Color)`（path geometry，进程级缓存不必要——按帧构建即可，图表节点数少）
- [x] `src/components/Charts.cpp`：纯函数 `line_points(values, Rect, ymin, ymax)`（归一化→折线顶点）、`pie_slices(values)`（→{start,sweep} 列表）+ smoke（数值断言：归一化端点、角度总和=2π）
- [x] commit `Round 4: chart geometry foundation`

### T2: 五件图表组件
- [x] `line_chart(pal, values, Rect hint, Props)`、`area_chart`（closed fill 半透明）、`pie_chart(pal, values, labels)`、`sparkline(pal, values, Props{width,height})`、`gauge(pal, float 0..1, label)`（半圆弧 + 百分比文字）
- [x] smoke：组件返回元素含子结构（图例/轴标签数量正确）
- [x] commit `Round 4: flux::ui charts (line/area/pie/sparkline/gauge)`

### T3: 通用键盘 + 选择菜单四件
- [x] `Props.on_key`（`std::function<void(WPARAM)>`，focused 时 Host handle_key_down 分发；不进 paint diff）+ smoke（props 差异不 paint-dirty）
- [x] `SelectHost{open,selected,anchor_x/y}` + `select()` 触发按钮 + `overlay_select()`（锚定浮层列表，点击项/外部关闭，Esc 关）
- [x] `MenuHost{items,open,anchor}` + `overlay_menu()`；wndproc 加 `WM_CONTEXTMENU` → 记录右键点 → app 声明（Gallery 演示）
- [x] `CommandPaletteHost{open, query, selected}` + Ctrl+K 全局钩子（Host：WM_KEYDOWN 'K' + Ctrl → 回调 `Host::set_command_palette_toggle(Fn<void()>)`? 简化：app 在各处不好拦截——Host 加 `std::function<bool(WPARAM)> key_hook_`：handle_keydown 最先调用，返回 true 消费）+ overlay（居中 + text_input + 过滤列表 + on_key 上下/回车/Esc）
- [x] smoke：SelectHost 状态机（open 翻转/selected 回调）；过滤纯函数 `filter_commands(items, query)` 
- [x] commit `Round 4: flux::ui selection & menus (select/menu/context/command palette)`

### T4: 布局容器三件
- [x] `Props.drag_value`（bool horizontal/vertical 语义合并为 Cursor 派生）+ Host：pressed 命中带 on_value_change 的元素时 mouse_move 按父容器宽/高归一回调（slider 泛化，slider 类型保留兼容）
- [x] `splitter(pal, ElementPtr a, ElementPtr b, float ratio, Fn<void(float)>, Props{vertical})`：[pane a (grow=ratio), drag handle (8px), pane b]
- [x] `DrawerHost{open}` + `overlay_drawer()`（贴右滑入，Animated<float> 位置动画 ease_out）
- [x] `sidenav(pal, items, active, on_change, collapsed, on_toggle)`：图标列 + 展开标签 / 折叠仅图标
- [x] commit `Round 4: flux::ui layout containers (splitter/drawer/sidenav)`

### T5: 数据展示三件 + on_scroll
- [x] `Props.on_scroll`（scroll 偏移变化时 Host 回调；不进 paint diff）+ smoke
- [x] `data_table(pal, columns, rows, sort_col, asc, on_sort, selected_row, on_select)`（表头排序点击、斑马纹、选中高亮；外部套 scroll_view 滚动）
- [x] `TreeItem{label, children, icon}` + `tree_view(pal, items, expanded_set, on_toggle, on_select, depth=0)`（缩进 + chevron）
- [x] `list_view` 性能演示（1000 行固定高，scroll_view + on_scroll → State offset → 只构建可见 ±overscan 行 = 真虚拟化）+ smoke（可见窗口计算纯函数 `visible_range(offset, viewport, row_h, total)`）
- [x] commit `Round 4: flux::ui data display (table/tree/virtual list + on_scroll)`

### T6: Gallery 剩余组件提升
- [x] `src/components/Pickers.cpp`：radio_group/color_picker/date_picker（迁移，参数化）→ `flux::ui::`；`src/components/DataViz.cpp`：calendar/bar_chart/avatar_group/breadcrumb
- [x] Gallery 对应 section 改调库版；删本地实现
- [x] commit `Round 4: promote pickers + data viz into flux::ui`

### T7: Gallery「Components」总览页 + 收口
- [x] 新 tab：Splitter 演示（可拖分栏）、DataTable（可排序）、TreeView、图表墙（5 图表）、Select/Menu/ContextMenu/CommandPalette 触发行、Drawer、虚拟列表性能卡（帧时间读数）
- [x] 全 tab 视觉走查 + 双 preset ctest + commit 收口

## 验收清单
- [x] 四批组件全部库内可用且 Gallery 演示
- [x] Ctrl+K 命令面板可过滤/键盘导航/执行
- [x] 1000 行列表滚动流畅（debug 帧时间 < 8ms 目标）
- [x] 右键菜单在 Gallery 任意处弹出
- [x] 双 preset 零警告全绿
