# 08 — WinFlux 功能扩展机会评审（2026-10-06）

评审人角色：产品经理 + 资深工程师。方法：通读 CLAUDE.md / AGENTS.md / DESIGN.md / docs/DIRECT_RENDERING_UI_PLAN.md / docs/superpowers 全部 plans+specs / include/flux 全部 12 个头文件 / src 全模块（Host.cpp 1615 行全文关键段、Damage.cpp、FlexLayout、Presenter、D2DRenderer 头与实现关键段、Theme.cpp、components 8 个源文件中的 Menus/Charts/Layout 全文与其余抽查）/ 两个样例 / CMakeLists / CI。仓库只读，未运行任何构建、测试、样例或脚本。每条"缺失"结论均给出 grep/读码证据；推断处标 (推断)。

---

## 0. 结论摘要

WinFlux 的**渲染/布局/损伤追踪/主题/组件覆盖**已是成熟可用状态（约 40+ 组件与原语），真正的短板集中在**输入栈（文本输入无选区/剪贴板/IME/placeholder——单 HWND 自绘库最难也最刚需的缺件）、事件原语（无双击/元素级右键/通用拖拽）、系统集成（持久化/剪贴板/文件拖放/图片/深色标题栏全缺）、开发者体验（无调试 HUD、无消费者级文档、无 find_package）**四条线上。立项文档 M4 承诺的 PropertyGrid/IconView/命令路由/持久化全部未交付，IME 也停在"留 M4"状态。建议 Now 阶段做：文本输入 Phase1 + 事件原语补全 + CMake 消费化 + 持久化原语；Next 阶段做：调试 HUD、IME、快捷键注册表、主题系统集成、焦点管理器。明确不做：打印、热重载、跨平台、Ribbon/Docking、TSF 全量富文本。

---

## 1. 现有功能清单表

成熟度定义：**成熟**＝API 完整、有测试、样例在用；**可用**＝能干活但有明显缺口；**骨架**＝入口存在但功能不全；**孤儿**＝无消费者/无测试。

### 1.1 框架核心（flux 命名空间）

| 功能 | 入口 | 成熟度 |
|---|---|---|
| 单 HWND Host / 帧管线（rebuild→layout→damage→paint） | `Host::paint_frame`（src/core/Host.cpp:1119） | 成熟 |
| 声明式元素树 + 构建器（view/label/button/checkbox/progress/icon） | include/flux/Element.hpp:160-170 | 成熟 |
| `State<T>` / `Host::make_state`（useState 等价） | include/flux/Host.hpp:32-52, 118-123 | 成熟 |
| Flex 布局（row/column/grow/gap/padding/margin/align/justify，无 wrap） | include/flux/FlexLayout.hpp:20 | 成熟 |
| 损伤追踪部分重绘（含交互态 diff、同树快路径） | src/render/Damage.cpp:99-224 | 成熟 |
| DComp 呈现 + Hwnd RT 回退 | src/render/DCompPresenter.cpp / HwndPresenter.cpp | 成熟 |
| D2D 绘制原语（圆角矩形/文本多行/省略号/渐变/手绘阴影/24 种路径图标/折线/多边形/扇形/裁剪栈/文本测量缓存） | include/flux/D2DRenderer.hpp:43-83 | 成熟 |
| 垂直 scroll_view（滚轮/拖拽/键盘/自动隐藏滚动条/浮层内滚动） | Host.hpp:129-142；Host.cpp:367+ | 成熟（**仅垂直**） |
| text_input（WM_CHAR 追加 + VK_BACK 尾删 + 末尾光标 + danger 边框） | Host.cpp:998-1113, 1488-1513 | **可用**（无选区/光标移动/剪贴板/IME/placeholder/password 属性） |
| slider / toggle_switch | Element.hpp:178-180；Host.cpp:1516-1560 | 成熟 |
| drag_handle（splitter 拖拽，drag_vertical） | src/components/Layout.cpp:12-46 | 可用 |
| Tab 遍历 + Space/Enter 激活 + 焦点元素 on_key | Host.cpp:1062-1093 | 可用（无 tabindex、无 focus() API、焦点环无专门样式） |
| 声明式浮层通道（overlay()/Esc/outside-close/scrim） | Host.hpp:78-88；Host.cpp:331-335 | 成熟 |
| 主题三模式 + 200ms 交叉渐变 | Theme.cpp:11-38；Host.cpp:347-356 | 成熟（**无自定义 Palette 注入、无系统亮暗跟随**，grep RegOpenKey/AppsUseLightTheme 无匹配） |
| Animated\<T\> 补间（3 缓动曲线 + animate_for 时钟） | include/flux/Animation.hpp | 成熟 |
| 悬停/按压微过渡（approach_blend） | Animation.hpp:42-56；Host.cpp:236-240 | 成熟 |
| Host 原生 tooltip（dwell 触发、跟随指针） | Host.cpp:250-256 区域；update_tooltip | 成熟（纯文本） |
| app 级 key_hook / context_menu_handler / last_click_pos | Host.hpp:96-105 | 可用（key_hook 只收 vk 无修饰键，app 需自行 GetKeyState，见 Dashboard.cpp:383） |
| 逐帧性能日志（WINFLUX_PERF_LOG，120 帧一行摘要） | Host.cpp:1349 附近 log_frame_summary | 可用 |
| Per-Monitor DPI V2（create 时即 set_dpi + WM_DPICHANGED） | Host.cpp:315-318, 208-213；AppHost.cpp:9-19 | 成熟 |

### 1.2 组件库（flux::ui，include/flux/Components.hpp 357 行 + src/components/8 文件）

| 功能 | 入口 | 成熟度 |
|---|---|---|
| 导航：tabs / segmented / pagination / accordion / sidenav（可折叠+tooltip） | Navigation.cpp；Components.hpp:26-49, 270-278 | 成熟 |
| 反馈：alert / badge / ToastHost / ModalHost | Overlays.cpp | 成熟 |
| 表单：field 包装 + validate_not_empty / validate_email | Components.hpp:117-125 | 可用（仅 2 个校验器，无 DSL/提交聚合） |
| 共享 chrome：kind_color / popup_card / menu_item_button / caption / icon_button | Components.hpp:127-145 | 成熟 |
| 下拉选择 select（SelectHost + overlay_select） | Menus.cpp:51-92 | 可用（**无键盘导航**——无 on_key/autofocus；长选项列表无高度钳制/滚动，Menus.cpp:80-89 面板无限增高） |
| 右键/锚定菜单 menu（MenuItem + "-"分隔线） | Menus.cpp:96-130 | 可用（无子菜单/图标/勾选态/快捷键提示列） |
| Ctrl+K 命令面板（autofocus + 方向键/Enter/Esc + 前 8 条） | Menus.cpp:154-263 | 可用（大小写不敏感子串匹配，无 fuzzy 评分/MRU/分组/快捷键提示） |
| 布局：splitter / DrawerHost（右滑入+缓动） | Layout.cpp | 成熟 |
| 数据：data_table（列排序/行选择）/ tree_view / virtual_list（可见窗口+overscan） | Data.cpp | 成熟（tree 无懒加载；table 无列宽拖拽） |
| 图表：line/area（十字线+数值气泡）/ bar（柱 tooltip）/ pie / sparkline / gauge / radar / scatter | Charts.cpp（series_color 5 语义色轮换 :124-132） | 成熟（**静态+悬停**；无缩放/平移/框选/拖拽编辑/undo-redo） |
| 选择器：radio_group / color_picker / date_picker / calendar（days_in_month/first_weekday 纯函数） | Pickers.cpp / DataViz.cpp | 成熟 |
| 数据展示：avatar_group / breadcrumbs | DataViz.cpp | 成熟 |

### 1.3 明确不存在的能力（全部 grep 核实过）

剪贴板（OpenClipboard/CF_TEXT 全仓库无匹配）、文件拖放（WM_DROPFILES/DragAcceptFiles 无）、IME（WM_IME/Imm*/CreateCaret 无）、UIA（uiautomation 无）、位图/WIC 图片加载（仅 DCompPresenter 内部 CreateBitmapFromDxgiSurface，非图片 API）、双击（窗口类未设 CS_DBLCLKS，Host.cpp:284-293 wc.style 缺省；无 WM_LBUTTONDBLCLK 分支）、元素级右键（context_menu_handler 只给坐标无元素）、多行文本输入、placeholder/password Props 字段（Gallery.cpp:431-434 手工叠 label 冒充 placeholder）、下划线/删除线/内联富文本（D2DRenderer.cpp 无 SetUnderline/SetStrikethrough）、持久化（src 内唯一文件 IO 是 perf log 的 fopen，Host.cpp:1349）、水平滚动（ScrollState 仅 offset_y，Host.hpp:129-137）、StatusBar/PropertyGrid/ComboBox/IconView（grep 无匹配）、打印（无）、CMake install/export（CMakeLists.txt 无 install 块，include 仅 `$<BUILD_INTERFACE>`，:35-37）、README（仓库根无）。

---

## 2. 文档 vs 代码差异（会误导提议的点）

1. **"key" 属性从未实现**：立项文档 §3.2 说 "key → M2 reconcile 引入"；实际 Element/Props 无 key 字段（Element.hpp 全文核实），身份是位置索引路径（CLAUDE.md 明示为 footgun）。提议任何"依赖稳定身份"的功能（如列表项动画、状态保持）必须先面对这一点。
2. **M0 描述已过时**：立项文档 §3.3/§5 写 "D2D 1.1 HWND Render Target"；实际已是 DComp presenter + Hwnd 回退（CLAUDE.md 已更新，历史文档未回改）。读旧文档会低估呈现层。
3. **M3/M4 承诺与现状差距大**：§4 里程碑表 M3 承诺图表"含拖拽编辑/缩放/十字准线"——只有十字准线/悬停落地；M4 的 PropertyGrid/IconView/命令路由/持久化集成**全部未做**；§6 风险表 "IME 留 M4" 同样未做。这些不是"被否决"，是"排了没做"，属于本评审重点评估是否重开的对象。
4. **flux.hpp:6-7 注释漂移**：指向 "docs/DIRECT_RENDERING_UI_PLAN.md in the NativeFrameUI repository"，文件实际在本仓库 docs/ 下。
5. **spec 2026-08-14 非目标清单**（位图/WIC、字体图标、RTL、flex-wrap、双向滚动、触摸惯性、overlay 独立 damage、绝对定位/z-index、UIA）**仍然有效且被代码遵守**——不是文档漂移，是真实边界；下文逐项给出"是否值得重开"的取舍。
6. CLAUDE.md/AGENTS.md/DESIGN.md 与代码一致（模块表、token、测试拆分、行数均核实无漂移）。

---

## 3. 立项文档愿景 vs 当前实现差距表（nfui 已有 → WinFlux）

依据：立项文档 §1.3 的 nfui 盘点逐项对本仓库 grep/读码核实。

| nfui 能力 | WinFlux 现状 | 判定 | 证据 |
|---|---|---|---|
| Edit（文本编辑，原生 HWND 自带选区/剪贴板/IME） | text_input 追加式单行：WM_CHAR push_back、VK_BACK 尾删、光标恒在末尾 | **部分存在**（基础输入有） | Host.cpp:998-1113, 1488-1513 |
| — 选区/光标移动/Home/End/Delete | 无 | **Confirmed 缺失** | Host.cpp handle_key_down/handle_char 全文 |
| — 剪贴板 Ctrl+C/V/X | 无（无 OpenClipboard） | **Confirmed 缺失** | 全仓库 grep |
| — IME（中文输入/组合窗/候选窗跟随） | 无（无 WM_IME/Imm*/CreateCaret） | **Confirmed 缺失** | grep 无匹配；TranslateMessage 在消息循环（AppHost.cpp:23），默认 IME 组合窗会挂在客户区原点、无候选定位 (Likely，未实测) |
| — placeholder / password 掩码 | 无 Props 字段；Gallery 手工叠 label | **Confirmed 缺失** | Element.hpp Props；Gallery.cpp:431-434 |
| 多行 Edit | 无（renderer 支持 \n/换行，但 text_input 单行） | **Confirmed 缺失** | D2DRenderer.hpp:54 注释；Host.cpp:1488 渲染分支 |
| ComboBox（可编辑下拉） | select 为纯下拉（触发按钮+浮层列表） | **Confirmed 缺失**（可编辑部分） | Menus.cpp:51-92 |
| ListBox | virtual_list + scroll_view | **已存在**（且更现代：虚拟化） | Data.cpp |
| ListView | data_table（排序/选择）；无图标视图模式 | **部分存在** | Data.cpp |
| TreeView | tree_view（展开集 + 路径回调；无懒加载/重命名/勾选节点） | **已存在** | Components.hpp:305-316 |
| IconView | 无；图标仅 24 个内置 IconKind 路径几何，无自定义图标 | **Confirmed 缺失** | Element.hpp:34-40 |
| TabControl | tabs（条带，app 自组 body；无 Ctrl+Tab、无溢出滚动） | **部分存在** | Navigation.cpp |
| StatusBar | 无标准件（view+label 可手工拼） | **Confirmed 缺失**（组件级） | grep 无 |
| PropertyGrid | 无 | **Confirmed 缺失** | grep 无 |
| Slider / Scrollbar / Tooltip / Splitter / ProgressBar / Panel | slider / scroll_view 滚动条 / tooltip / splitter / progress / view | **已存在** | — |
| 命令系统（ID/处理器/路由/Accelerator 统一路由） | key_hook（裸 vk）+ CommandHost 面板组件；无 chord/作用域/注册表 | **部分存在** | Host.hpp:96-101；Menus.cpp:154 |
| 布局（Anchor/线性/Panel/Splitter 比例持久化） | flex + splitter；**比例/位置持久化无** | **部分存在** | — |
| 图表：Line/Bar/Area/Pie/Scatter/Sparkline/KpiTile | line/bar/area/pie/scatter/sparkline/gauge(+radar) | **已存在**（KpiTile≈stat_card 模式，Dashboard.cpp:60-82） | Charts.cpp |
| 图表交互：拖拽编辑/框选/滚轮缩放/平移/undo-redo | 全无 | **Confirmed 缺失** | Charts.cpp grep zoom/pan/drag 仅命中无关行 |
| 图表交互：十字准线/悬停 tooltip/选中高亮 | line/area 十字线+数值气泡；bar 柱 tooltip | **部分存在**（pie/radar/scatter 无交互） | Charts.cpp:106；round7 计划 |
| 图表 8 色系列色板 | series_color 轮换 accent/success/warning/danger/info 5 色 | **部分存在**（语义色兼任分类色，会误导读图） | Charts.cpp:124-132 |
| 主题：System/Light/Dark/HC + ThemeBroker 跨窗口同步 | light/dark/high_contrast 静态三套；**无系统主题检测/跟随**、单窗口无 Broker | **部分存在** | Theme.cpp:11-38；grep RegOpenKey 无 |
| 主题：state_palette 六态解析（rest/…/error） | 每元素 hover/pressed/disabled_background Props + input_error；无集中解析器 | **部分存在** | Element.hpp:75-77, 108 |
| 主题：HC 委托 GetSysColor | 静态 HC 调色板，不读系统 HC 色 | **部分存在** | Theme.cpp:21-28 |
| 持久化（逻辑单位、损坏容错） | 无任何持久化 | **Confirmed 缺失** | src 唯一文件 IO＝perf log |
| 动画/缓动（Clock/Easing） | Animated\<T\>+3 曲线+animate_for | **已存在** | Animation.hpp |
| 模态/非模态 Dialog | ModalHost 应用内浮层；无 OS 级第二窗口 | **部分存在**（by design） | Overlays.cpp |
| Per-Monitor V2 DPI | 有（含 create 即设 DPI） | **已存在** | Host.cpp:315-318 |
| 资源上下文/ResourceContext（资源模块句柄） | 无（无资源加载体系） | **Confirmed 缺失** | — |
| Diagnostics Result/Error | 帧管线 noexcept + WndProc try/catch；无 Result 类型 | **部分存在** | Host.cpp:263-265 |

**结论**：WinFlux 已超额完成 M0-M2 与 M3 的静态图表；差距集中在 (a) 输入栈深度（选区/IME/剪贴板）、(b) M4 全部内容（PropertyGrid/IconView/命令路由/持久化/UIA）、(c) nfui 图表高级交互、(d) 系统集成面（剪贴板/拖放/图片/主题跟随）。另有 WinFlux 独有优势（损伤区重绘、DComp、虚拟化列表、命令面板、toast/drawer、校验器、CI、perf log）为 nfui 所无。

---

## 4. 候选功能卡片总表

评分：用户价值 1-5（对"内部开发者用 flux 搭桌面工具"）；频次＝日常使用密度；成本 S(<200 行)/M(200-800)/L(>800 或高风险)；复用＝可直接骑乘的现有模块。

| # | 候选 | 价值 | 频次 | 成本 | 复用 | 需新增 | 风险 | 下游复杂度 | 优先级 |
|---|---|---|---|---|---|---|---|---|---|
| F1 | 文本输入 Phase1：选区+光标移动+Delete/Home/End+Ctrl+A/C/V/X+placeholder/password | 5 | 每次输入 | M | text_input 元素、paint_props_equal、measure_leaf | Props 字段（placeholder/password/caret 数据走 app State）、剪贴板工具函数、hit-test 字符定位 | 性能（每键重绘须限 damage 内）；DPI 下字符定位 | 无新概念，纯增强 | **P0** |
| F2 | 事件原语补全：on_double_click（CS_DBLCLKS）、元素级 on_context_menu、通用 on_drag | 5 | 每天 | S-M | hit-test、context_menu_handler、drag_handle 机制 | Props 3 个回调字段 + wndproc 分支 | 双击与单击时序冲突（单击延迟 500ms 或接受双击时先触发两次单击——建议后者+文档） | 极低 | **P0** |
| F3 | CMake 消费化：install/export/find_package/FetchContent 兼容 + 消费者 README | 5 | 接入一次 | S | 现有 target 结构 | install 规则、Config 文件、README | 无 | 降低（少一次 copy-paste 接入） | **P0** |
| F4 | 调试 HUD：元素边框/damage 矩形可视化/树结构查看器/逐帧耗时叠加 | 5 | 调试期高频 | M | DamageTracker 输出、paint_frame 计时已有、overlay 通道、Props::paint | debug 叠加渲染 pass、树查看器浮层（复用 tree_view） | 仅调试构建启用即无风险；误开在发布版有泄露布局风险 | 无 | **P0** |
| F5 | 持久化原语：settings load/save（窗口 rect/主题/最大化态/State 注册表→ini），损坏容错 | 5 | 每个工具 | S-M | Host Config、theme、State<T> | flux::settings 命名空间（ini 解析纯函数可 smoke 测） | 写盘时机（勿每帧）；损坏容错 | 低（opt-in） | **P0** |
| F6 | IME：WM_IME_SETCONTEXT/COMPOSITION/ImmSetCompositionWindow+候选窗跟随+组合中文本下划线 | 5（中文用户刚需） | 每次中文输入 | L | F1 的选区/光标模型、TranslateMessage 已在 | IME 消息分支、组合态 Props、composition 定位 | 单 HWND 自绘库最深的坑；DPI/多监视器定位 | 低 | **P1**（依赖 F1） |
| F7 | 快捷键注册表：chord（"Ctrl+K"/"Ctrl+Shift+P"）+作用域（全局/浮层）+菜单提示列 | 4 | 每天 | M | key_hook（升级为注册表查询）、MenuItem/CommandItem | flux::Hotkeys 注册表 + 解析纯函数 | 与现有 key_hook 兼容（保留为最高优先级逃生门） | 中（app 改用注册表） | **P1** |
| F8 | 主题系统集成：系统亮暗跟随（AppsUseLightTheme+WM_SETTINGCHANGE）、自定义 Palette 注入（set_palette）、深色标题栏（DwmSetWindowAttribute）、窗口 Config 扩展（icon/min size/居中/可缩放开关） | 4 | 每个工具 | S-M | set_theme_mode 渐变、palette() | Host API + Theme.cpp 检测函数 | 标题栏属性失败需静默降级；主题文件格式若无则不做（YAGNI） | 低 | **P1** |
| F9 | 焦点管理器：tabindex/focus_skip、Host::focus(path) API、focus-visible 统一样式、F6/焦点框 | 4 | 键盘用户日常 | S-M | collect_focusable、focus_path_ | Props.tab_index + Host API + 渲染分支 | 无 | 低 | **P1** |
| F10 | 图片/图标资源：WIC 解码（PNG/JPEG）→D2D bitmap 缓存 + 自定义矢量路径 IconSet（24 网格描述格式） | 4 | 常用 | M | D2DRenderer 设备生命周期、设备丢失清缓存模式 | draw_bitmap API、IconCache、icon(path) 构建器 | 大图同步解码卡帧→限尺寸+懒解码；设备丢失重建 | 低 | **P1** |
| F11 | 状态时间旅行：State 变更 ring buffer + 导出/导入 JSON + 树查看器回放 | 3 | 调试期 | M | State::set notify 钩子、F4 HUD | 历史 buffer（上限截断）、序列化 | 内存上限；闭包态无法序列化（只记 State<T> 数值型） | 低 | P2 |
| F12 | UIA 无障碍桥（IRawElementProviderSimple 虚拟树） | 3（内部工具低） | 无障碍用户 | L | Element 树路径 | UIA provider 模块 | 大；单 HWND 自绘映射复杂 | 无 | P2（内部场景延后） |
| F13 | 多窗口支持：双 Host 生命周期测试+文档化+主题跨窗同步（ThemeBroker 等价） | 3 | 需要时 | M | 机制已备（类重注册容错 Host.cpp:290-293 + per-HWND userdata :181-190） | 测试、文档、broker（回调链） | 未实测 Likely 可行；消息循环共享语义需澄清 | 低 | P2 |
| F14 | 超链接/富文本：label 内联范围（下划线/着色/on_click 链接）、删除线 | 3 | 常用 | M | draw_text/DWrite TextLayout 缓存 | Props.ranges 或 label(segments) 构建器、命中测试 | 文本命中测试成本；measure 缓存 key 需含 ranges | 低 | P2 |
| F15 | 动画原语标准化：列表项增删（FLIP 式位移过渡）、页面/tab 切换过渡、进入/退出预设 | 4 | 常用 | M | Animated\<T\>、animate_for、overlay 模式 | ui::animated_list 包装（diff 行集→补间）、过渡预设 | 位置身份约束：增删动画需 key（见差距#1）→首版限"尾增尾删+位移" | 低 | P2 |
| F16 | 表单 DSL：FormHost（fields/提交/校验聚合/dirty 态/Enter 提交） | 3 | 表单页常用 | M | field+validators、State、F1 | ui::form 组件 + 校验器扩展 | 无 | 低 | P2 |
| F17 | PropertyGrid（名/值/类型编辑器网格） | 3 | 工具型 app 常用 | M | data_table、F1、select/slider | ui::property_grid + 值编辑器分发 | 依赖 F1（字符串编辑） | 低 | P2（M4 遗产） |
| F18 | 图表交互：滚轮缩放/拖拽平移/框选/undo-redo | 3 | 分析型工具 | L | paint hover ctx、Animated、F2 拖拽 | 视口 State、几何纯函数（smoke 可测）、命令栈 | 性能（每帧重算几何）；与虚拟化滚动手势冲突 | 中 | P2 |
| F19 | 水平滚动 + shift-滚轮横滚 | 3 | 宽表格常用 | M | layout_scroll_view、ScrollState | offset_x、双向裁剪、横向滚动条 | 布局引擎改动（主轴假设）；spec 非目标之一 | 低 | P2（重开理由：宽 data_table 真实痛点） |
| F20 | flex-wrap | 2 | 偶尔 | M | flex_layout | 行断点算法 + 二次行定位 | 每帧布局成本；与虚拟列表交互 | 低 | Backlog（chip 流式布局可用"手动分行 view"顶替） |
| F21 | overlay 独立 damage diff | 2 | — | M | DamageTracker | overlay 树 diff 通道 | 正确性风险（现全脏是保守正确） | 无 | Backlog（先量化：perf log 证明浮层动画是热点再开） |
| F22 | 系统文件/消息对话框包装（IFileOpenDialog/TaskDialogIndirect） | 4 | 桌面工具高频 | S | 无需渲染改动（Win32 原生） | flux::sys::open_file_dialog/save/message_box | COM 初始化归属（CoInitialize 在 app） | 极低 | **P1**（可并入 F8 系统集成包） |
| F23 | chrome 组件包：status_bar / toolbar / skeleton 骨架屏 / empty_state 空状态 | 3 | 常用 | S | view/label/icon_button/popup_card 全套 | 4 个纯构建器（零新机制） | 无 | 低 | P1（顺手） |
| F24 | ComboBox 可编辑（输入过滤+下拉选择） | 3 | 常用 | M | select + F1 | ui::combo_host | 依赖 F1 | 低 | P2 |
| F25 | 多行文本 textarea | 3 | 设置/备注页 | M-L | F1 全套 + text_wrap | 行布局、垂直光标移动、滚动联动 | 依赖 F1/F6；行高与 DPI | 低 | P2 |
| F26 | Gallery→组件文档生成 + 性能回归门禁（perf log 阈值断言） | 3 | 维护期 | S-M | Gallery 页结构、WINFLUX_PERF_LOG、CI | 文档提取脚本（或注释约定）+ CI perf job | perf 门禁抖动（阈值放宽+只挡回归>50%） | 无 | P2 |
| F27 | 打印 | 1 | 极低 | L | — | 打印 DC 路径 | DPI/分页全雷 | — | **不做** |
| F28 | 热重载（builder 代码重载） | 2 | 开发期 | L | — | 需 DLL 拆分或脚本驱动重编译重启 | 边界破坏（无线程/子进程约束） | — | **不做**（F5 持久化 + 快启动覆盖 80% 价值） |

---

## 5. Top 10 推荐功能（含实现草案）

### 1) F1 文本输入 Phase1（P0）——库的第一刚需

- **接口**：`Props.placeholder`（wstring）、`Props.masked`（bool，密码圆点）、新增 `Props.caret`/`Props.selection` 不进 Props——**选区是交互态**，学 Host 现有模式放 Host 侧：`focused_input_path_` 已存在，扩成 `struct { Path path; size_t caret; size_t anchor; }`（anchor≠caret 即选区）。`text_input(value, on_change)` 签名不变。
- **数据流**：WM_KEYDOWN 处理 VK_LEFT/RIGHT/HOME/END（+Shift 扩 anchor）、VK_DELETE、Ctrl+A（全选）/C/X/V（经 `flux::clipboard_get/set_text`，新小文件 src/platform/Clipboard.cpp，OpenClipboard→CF_UNICODETEXT）；WM_CHAR 追加改为"替换选区再插入"。单击命中→用 `D2DRenderer::measure_text` 的宽度对字符前缀做二分定位 caret（复用 text_layouts_ 缓存，须给 measure 暴露 caret 命中 API）。渲染分支（Host.cpp:1488-1513）加：选区高亮矩形（surface_variant 透明叠层）、caret 按闪烁相位（animate_for 驱动，仅 caret 闪烁期间限制在输入框 damage 内——注意 animating() 现在是全屏脏，**需要新增"局部动画"机制**：闪烁只 dirty 输入框 rect，这是本项最大的架构改动点，可在 Host 加 `partial_animating_rect_`）。
- **UI 入口**：Gallery "Form controls" 页已有 inputs_section，加选区/placeholder/密码演示。
- **测试策略**：剪贴板 round-trip（lifecycle）、caret 定位纯函数（把"字符前缀宽度二分"抽成 smoke 可测的独立函数 `caret_hit_test(layout_widths, x)`）、DamageTracker 新字段进 paint_props_equal（placeholder/masked 必须加，否则违反 CLAUDE.md 不变量）。
- **性能护栏**：每键重绘已天然限制在输入框 damage（text 已在 paint_props_equal，Damage.cpp:112——已核实）；新字段照抄。caret 闪烁若走全屏脏会退化，必须做局部。

### 2) F2 事件原语补全（P0）——半天量级、天天用

- **接口**：`Props.on_double_click`、`Props.on_context_menu`（元素级；现全局 handler 保留为 fallback）、`Props.on_drag`（`std::function<void(DragEvent)>`，DragEvent{start_x/y, cur_x/y, phase}）。
- **数据流**：窗口类加 `CS_DBLCLKS`（Host.cpp:284-293 一行）+ WM_LBUTTONDBLCLK 分支走既有 hit-test→冒泡链；WM_CONTEXTMENU 先元素级（resolve 右键坐标命中的元素，沿树向上找第一个带 on_context_menu 的）再退回全局 handler；拖拽复用 drag_scroll_ 捕获模式：按下时记录候选，移动超 3px 阈值进入 drag_phase，抬起结束。
- **UI 入口**：data_table 行双击打开详情（Dashboard 演示）、树节点右键菜单。
- **测试策略**：lifecycle 发 WM_LBUTTONDBLCLK round-trip；右键元素命中纯逻辑可 smoke（抽出 `find_context_menu_target(tree, x, y)` 纯函数）。双击会先触发两次 on_click——文档明示，组件层需要双击语义的地方（列表）用 debounce 由 app 决定。
- **风险**：低。唯一注意点是 on_click 与 on_double_click 的时序契约写进 Element.hpp 注释。

### 3) F3 CMake 消费化 + 消费者文档（P0）——"从样例库走向被消费"的门票

- **接口**：CMakeLists 加 `install(TARGETS flux EXPORT fluxConfig ...) install(DIRECTORY include/ ...)` + `fluxConfig.cmake`（静态依赖 d2d1/dwrite/dcomp/d3d11/dxgi/windowscodecs 顺带 EXPORT）；include 路径补 `$<INSTALL_INTERFACE:include>`。README（新建，面向消费者）：10 行接入示例（add_subdirectory / FetchContent / find_package 三种）、最小 main、链接依赖表。
- **数据流**：无运行时改动。
- **测试策略**：CI 加一个 `winflux-consumer` job：FetchContent 拉本体 + 编译 20 行最小 app。
- **风险**：无。git tag + 版本号（CMake project VERSION 已有 0.1.0）。

### 4) F5 持久化原语（P0）

- **接口**：`namespace flux::settings { struct Schema{...}; void save(const std::wstring& ini_path, const std::vector<std::pair<std::wstring,std::wstring>>& kv); std::map<...> load(const std::wstring& ini_path); }` 纯字符串 kv，类型编解码在调用侧；`Host` 提供 `save_window_state()/restore_window_state()` 便利封装（rect/最大化/主题 mode）。app 侧把 `State<T>` 注册进一张表（`std::vector<std::pair<std::string, std::function<std::wstring()>>>`）即可整体落盘。
- **数据流**：写盘时机＝WM_CLOSE/destroy + 显示 debounce；读盘＝create 前。损坏容错：解析失败返回空表（nfui 同款语义）。
- **UI 入口**：Dashboard 演示"重开记住窗口与主题"。
- **测试策略**：ini 解析/序列化纯函数 smoke（含损坏文件、注释、BOM）；窗口态 round-trip lifecycle。

### 5) F4 调试 HUD（P0）——React DevTools 等价物

- **接口**：环境变量 `WINFLUX_DEBUG_HUD=1`（或 Config 标志，`#ifndef NDEBUG` 默认可用）。四层：(a) 元素边框叠加（render_element 尾部按深度着色描 1px 线）；(b) damage 矩形叠加（paint_frame 拿到 dirty_rects 后画红框，下一帧再画新的）；(c) 树查看器浮层（把 tree_ 喂给现成 `ui::tree_view`，经 `Host::overlay()` 显示，点击节点滚动到/闪显对应元素）；(d) 帧耗时角标（复用 qpc 计时，画 build/layout/damage/draw 四段横条）。
- **数据流**：全部只读现有状态（dirty_rects、tree_、qpc 字段），不碰帧管线主路径；叠加绘制放在 presenter end() 前最后一个 pass。
- **测试策略**：无新逻辑需单测；lifecycle 冒烟（开关不崩）。
- **风险**：发布版默认关闭即无泄露/性能风险；文档写明"HUD 帧不计入 perf log 统计"。

### 6) F6 IME（P1，中文团队实际刚需）

- **接口**：Host 处理 `WM_IME_SETCONTEXT`（关系统组合窗，自绘）、`WM_IME_STARTCOMPOSITION/COMPOSITION`（ImmGetCompositionString 取 GCS_COMPSTR/GCS_CURSORPOS → 组合中文本+光标渲染）、`WM_IME_ENDCOMPOSITION`（提交结果走 on_text_change）；`ImmSetCompositionWindow` 把系统组合/候选窗定位到 F1 的 caret 屏幕坐标（ClientToScreen + DPI 换算）。
- **数据流**：组合态存 Host（`ime_composition_` wstring + caret pos），渲染分支在 text_input 文本后画组合段（下划线 accent）；DamageTracker 用 paint_id 变化触发（或直接 InteractionState 式局部脏）。
- **UI 入口**：Gallery 输入框直接可用中文输入验证。
- **测试策略**：组合字符串解析纯函数 smoke；真 IME 行为人工矩阵（微软拼音：组合/候选/回车提交/Esc 取消/中英切换）。
- **风险**：全库最高。护栏：仅对 `focused_input_path_` 非空时启用；失败模式=退回现状（追加式），不影响英文输入路径。

### 7) F7 快捷键注册表 + 命令面板增强（P1）

- **接口**：`struct Hotkey { unsigned vk; bool ctrl, shift, alt; }; class HotkeyMap { void bind(Hotkey, std::function<void()>, std::wstring label); bool dispatch(unsigned vk); };` Host 内部在 handle_key_down 顶端（现 key_hook_ 位置）查询，修饰键由 Host 统一 GetKeyState（现在每个 app 自己查，Dashboard.cpp:383）。命令面板 CommandItem 加 `Hotkey display`（右侧灰字提示列）与 fuzzy 评分（子序列匹配打分排序，纯函数可测）+ MRU（CommandHost 加 recent 向量）。
- **测试策略**：Hotkey 解析（"Ctrl+K" 字符串→结构）纯函数 smoke；fuzzy 打分纯函数 smoke。

### 8) F8 主题系统集成 + 窗口 polish（P1）

- **接口**：`Host::set_palette_override(const Palette*)`（自定义品牌色，fade 链路复用）；`Host::follow_system_theme(bool)`（读 AppsUseLightTheme 注册表键 + WM_SETTINGCHANGE 监听）；`Host::Config` 扩展 `icon`（HICON/资源 id）、`min_width/min_height`、`resizable`、`centered`；create 后按 mode 设 `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`（深色主题配深标题栏——现在暗色主题白标题栏，视觉断层）。
- **测试策略**：注册表读纯函数 smoke（注入假值）；DWM 属性失败静默（老系统）不阻断 create。

### 9) F9 焦点管理器（P1）

- **接口**：`Props.tab_index`（int，缺省按树序）、`Props.focus_skip`（容器跳过）、`Host::focus_element(const Path&)`（编程聚焦）、`Props.focus_ring`（自定义焦点环色/宽，缺省用 accent 1.5px——现在焦点视觉只有 text_input caret 与按钮 hover，Tab 焦点不可见，键盘可达性名存实亡）。
- **数据流**：collect_focusable 排序规则改为 tab_index 稳定排序；渲染分支画 focus ring（damage 交互 diff 已含 focus_path_，天然局部重绘——已核实 Damage.cpp:209-213）。
- **测试策略**：焦点序纯函数 smoke。

### 10) F10 图片/图标资源 + F22/F23 系统集成与 chrome 包（P1 合并交付）

- **接口**：`D2DRenderer::draw_bitmap(id, rect, alpha)`；`flux::ImageCache::load(path) -> ImageId`（WIC 解码，按 (path,mtime) 缓存，设备丢失时随 brush 缓存一起清——照抄现有失效模式 D2DRenderer.hpp:99-101）；`ElementPtr image(ImageId, Props)`；自定义矢量图标：`IconKind` 之外加 `icon_path(path_desc)`（复用 24 网格 stroke 描述格式，从文件/内嵌字符串加载 path geometry）。`flux::sys::open_file_dialog(...)/message_box(...)`（IFileOpenDialog/TaskDialogIndirect 薄包装）。`ui::status_bar/toolbar/skeleton/empty_state` 四个纯构建器。
- **测试策略**：ImageCache 键/失效纯逻辑 smoke；对话框函数人工验证；chrome 组件结构 smoke（照 test_components 模式）。
- **风险**：大图解码卡帧——护栏：解码上限（如 >4096px 拒绝或下采样）、首版同步解码+文档写明无线程边界。

---

## 6. 分阶段路线图

| 阶段 | 内容 | 理由 |
|---|---|---|
| **Now**（下一轮，约 1 个 round） | F2 事件原语（S，半天级杠杆）→ F3 CMake 消费化 + README（S）→ F5 持久化（S-M）→ F1 文本输入 Phase1（M）→ F8 中的深色标题栏/Config 扩展（S 子集）→ F23 chrome 包（S） | 全部 S-M 成本、无架构冒险、直接回应"被真实项目消费"的阶段目标；F1 是 F6/F24/F25 的地基 |
| **Next** | F4 调试 HUD → F6 IME → F7 快捷键注册表+命令面板增强 → F9 焦点管理器 → F8 其余（系统主题跟随/自定义 Palette）→ F22 文件对话框 | IME 与 HUD 是两大体验跃迁；均依赖 Now 的 F1 |
| **Later** | F10 图片/图标 → F13 多窗口（测试+文档+broker）→ F14 超链接/富文本 → F15 列表/过渡动画 → F16 表单 DSL → F17 PropertyGrid → F18 图表交互 → F19 水平滚动 → F24 ComboBox → F25 textarea → F26 文档生成+perf 门禁 | 价值实但各有依赖或风险，逐轮排 |
| **Backlog** | F11 状态时间旅行、F12 UIA、F20 flex-wrap、F21 overlay 独立 diff | 等 Later 交付后的真实痛点信号再定 |

---

## 7. 不做清单（及理由）

| 项 | 理由 |
|---|---|
| 打印 | 内部桌面工具打印需求≈0；DPI/分页/DC 全是深坑；立项也从未承诺 |
| 热重载 | 需要进程外编译/注入，破坏"无线程/无子进程"边界；F5 持久化 + 秒级重启覆盖大部分价值 |
| 跨平台 / 跨语言绑定 | 立项非目标，架构（DComp/Win32 深耦合）不支持 |
| Ribbon / Docking / 插件系统 | 立项非目标；内部工具无此需求信号 |
| TSF 全量富文本编辑（ITextStoreACP） | 全功能文本服务框架成本极高（数千行+焦点管理协议）；F1+F6 覆盖工具场景 95% |
| 网络/数据源内置 | 产品边界：无网络库（grep 证实现状干净） |
| 触摸/笔惯性滚动、手势 | spec 非目标；内部工具键鼠为主 |
| 任意绝对定位 / 通用 z-index | spec 非目标，锚定预设够用；引入会破坏 damage/diff 假设 |
| 主题文件格式（json 主题包） | YAGNI：set_palette_override + 代码内 Palette 足够，文件解析是负资产 |
| 自研测试框架 | CLAUDE.md 明令禁止 |

---

## 8. 性能护栏要求

1. **文本输入每键重绘必须留在 damage 矩形内**：`text` 已在 `DamageTracker::paint_props_equal`（src/render/Damage.cpp:112，已核实）；新增 placeholder/masked/focus ring 字段必须同补，且 **caret 闪烁不得走 animating() 全屏脏路径**——需局部动画 rect 机制，否则 60fps 常驻全屏重绘。
2. **IME 组合期重绘仅脏输入框**：组合文本作为 text 的一部分参与 diff，或 paint_id 派生自组合串哈希（照抄 Charts 的 series_id 手法，Charts.cpp:135-142）。
3. **调试 HUD 仅调试构建/显式环境变量启用**，HUD 自身绘制不计入 perf log 的 draw 统计，避免污染门禁。
4. **快捷键注册表**：每次 KEYDOWN O(n) 线性匹配，n≤128 无需哈希；禁止在注册表查询里做字符串解析（chord 解析仅在 bind 时一次）。
5. **持久化**：写盘仅 WM_CLOSE/destroy 或 ≥500ms debounce；禁止每击键写盘。ini 单文件 ≤64KB 预期。
6. **图片**：解码尺寸上限（>4096px 拒绝/下采样）、缓存条目上限（LRU 64）、设备丢失（D2DERR_RECREATE_TARGET）时随现有缓存一并清空；首版同步解码，超限时打 perf log 警告行。
7. **事件原语**：on_drag 回调频率=mousemove 频率，文档要求 handler 只写 State 不做重活；右键命中测试复用每帧已有 hit-test 结果，不得额外遍历。
8. **图表缩放/平移（若做）**：视口变换走纯函数+paint_id 派生自视口参数；每帧几何点数上限（>10k 点降采样）。
9. **flex-wrap（若重开）**：仅静态容器启用，禁止与 virtual_list 组合（行断点对内容高度的依赖会破坏虚拟化假设）。
10. **通用**：任何新 Props 可画字段必须同步 paint_props_equal（CLAUDE.md 不变量）；任何 hover/pressed/focus 语义的新交互路径必须走 InteractionState 局部脏而非 force_full_dirty_（memory 中"altitude trap"教训）。

---

## 9. 被推翻的假设 / 已验证无问题

| 假设/传闻 | 结论 | 证据 |
|---|---|---|
| （记忆）Renderer DPI 未初始化，高 DPI 监视器渲染 67% 尺寸 | **已修复** | Host::create 显式 `renderer_.set_dpi(GetDpiForWindow(hwnd))`，src/core/Host.cpp:315-318（带注释说明动机） |
| （记忆）renderer_ 声明在 presenter_ 前，析构顺序 UB 风险 | **已修复** | Host.hpp:195-197 现为 presenters 先声明、renderer_ 最后声明（:284）并附顺序注释 |
| 命令面板模式缺失，需从零建 | **部分推翻** | `overlay_command_palette` 已存在（Menus.cpp:154-263），含 autofocus/键盘/前 8 条；缺的是 fuzzy/MRU/提示列（属增强非新建） |
| 图表无悬停交互 | **推翻** | line/area 有十字线+最近点+数值气泡（Charts.cpp:106 区域、round7 计划）；bar 有 tooltip_text |
| 多窗口完全不可能 | **半推翻** | 类重注册容错 ERROR_CLASS_ALREADY_EXISTS（Host.cpp:290-293）+ per-HWND GWLP_USERDATA 绑定（:181-190）机制上支持多 Host；但无测试、无文档、无主题同步——Likely 可行，未实测 |
| text 变化可能不触发重绘（paint_props_equal 漏字段） | **无问题** | Damage.cpp:112 显式比较 a.text/b.text；每键已是局部重绘 |
| 无网络/子进程/线程（主审线索） | **独立复核一致** | src 全目录 grep CreateThread/socket/WinHttp/ShellExecute/CreateProcess 零匹配 |
| 立项文档"key/reconcile M2 引入" | **从未实现** | Element/Props 无 key 字段（Element.hpp 全文）；身份=位置路径（CLAUDE.md 明示）——列为文档 vs 代码差异，防止未来功能提案建立在 key 之上 |
| select 下拉可用键盘 | **证伪**（确实不能） | overlay_select 无 on_key/autofocus（Menus.cpp:75-92），纯鼠标操作 |

---

## 10. 未验证 / 局限

1. 未运行构建/测试/样例（只读约束）；所有成熟度判断基于源码阅读与测试文件结构，未验证 ctest 当前全绿。
2. nfui 本体不在本仓库，其能力清单取自立项文档转述，未读 nfui 源码核对（差距表以 WinFlux 侧 grep 为准，nfui 侧为文档依据）。
3. IME 现状的实际行为（中文输入法在当前 WM_CHAR 管线下的表现、组合窗出现在客户区原点）为机制推断（TranslateMessage 存在于 AppHost.cpp:23），未实机验证——标 Likely。
4. 多窗口双 Host 未做原型实验，仅静态读码判断 Likely 可行。
5. 性能数字（每键重绘、全树重建耗时、浮层全脏的实际帧成本）未测量；护栏阈值（4096px/64 条 LRU/500ms debounce）为工程经验值，非本机基准。
6. 未评估两个样例的视觉正确性（不启动 GUI）；Gallery 页清单取自源码常量（9 个分类 tab，Gallery.cpp:1991-1992）。
7. 历史评审记忆文件（MEMORY.md 索引的 8 篇）仅核对了其中 2 条与本评审相关的（DPI、析构顺序），其余未逐条复核。
