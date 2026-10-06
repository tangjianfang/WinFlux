# Direct 渲染 React 式 UI 基础库方案（WinFlux）

> 状态：已立项。本文档盘点了 NativeFrameUI（nfui）现有全部功能、UI 组件与
> 架构，并给出下一代 UI 基础库 **WinFlux**（`flux` 命名空间）的总体方案：
> 基于 C++20 / Win32 / Direct2D / DirectWrite 的**单 HWND 场景树渲染**框架，
> 采用 React 的组件/状态/声明式心智模型，作为基础库服务所有后续项目。
>
> 新项目位置：`c:\tjf\github\WinFlux`

---

## 1. 现有项目（NativeFrameUI）盘点

### 1.1 架构

- 纯 Win32 C++20 静态库，`nfui` 命名空间，无 MFC/ATL/WTL/BCG。
- 分层静态库，单向依赖，CMake 3.25+ Presets 唯一构建入口：

```text
nfui_core（Application/Window/Dialog/Handle/Diagnostics/HoverState/Dpi/
           Font/Icon/Paint/VectorIcon/Persistence/ResourceContext）
  ↑
nfui_command / nfui_layout / nfui_theme / nfui_window / nfui_dialog / nfui_menu
  ↑
nfui_control_base（subclass_proc + create_native + owner-draw 分发）
  ↑
nfui_button / checkbox / radio / text / listbox / listview / treeview /
iconview / frame / slider
  ↑
nfui_charts → nfui_charts_aa（GDI+ 抗锯齿渲染器）
```

- 关键不变量：每个包装器暴露稳定 `hwnd()`；显式句柄所有权（Owned/Borrowed）；
  异常不穿越 WindowProc；仅 UI 线程触碰窗口；逻辑单位与物理像素分离；
  `LONG_PTR` 存指针数据；无全局单例（服务挂 ApplicationContext）。

### 1.2 渲染策略（现状）

**每个控件仍是一个原生 HWND**，视觉由"子类化 + owner-draw / custom-draw +
`theme_disable_window_theme`"换皮实现；渲染栈为 GDI MemoryDC（图表用 GDI+）。
这带来两类结构性痛点，正是新库要消除的：

1. comctl32 主题与自绘 chrome 互相打架（CP20/CP42 一系列补丁）；
2. 每个控件独立 HWND/重绘路径，无法做统一场景合成、AA、动画与声明式更新。

### 1.3 已实现功能清单

**核心运行时**：Application/ApplicationContext、消息循环与 dispatcher、
窗口类注册、`GWLP_USERDATA` 安全绑定 + WM_NCDESTROY 清理、模态/非模态对话框、
ResourceContext（显式资源模块句柄）、Diagnostics Result/Error、Persistence
（逻辑单位持久化、损坏容错）、Clock/Easing/Animation（200ms 主题交叉渐变）。

**控件（20 个）**：Button、CheckBox、RadioButton、StaticText、Edit、ComboBox、
ListBox、ListView、TreeView、IconView、Panel、Splitter、TabControl、StatusBar、
ProgressBar、Slider、Scrollbar、Tooltip、PropertyGrid、Frame 类型
（Menu/Toolbar 经 nfui_menu / nfui_frame）。

**图表**：Line/Bar/Area/Pie/Scatter/Sparkline/KpiTile、ChartGroup、
高级交互（拖拽编辑数据点、框选、滚轮缩放、中键平移、十字准线、悬停 Tooltip、
选中高亮、编辑/缩放动画、撤销重做）。

**主题**：System/Light/Dark/HighContrast 三套 18 字段调色板（`theme_palette`）、
`state_palette` 六状态解析（rest/hover/pressed/focused/disabled/error）、
8 色图表系列色板、ThemeBroker 跨窗口同步、HC 委托 GetSysColor、WCAG AAA。

**DPI**：Per-Monitor V2、逻辑/物理换算、`WM_DPICHANGED` 字体/图标重缩放。

**命令系统**：命令 ID/处理器/状态/路由器，Menu/Toolbar/Accelerator/上下文
菜单统一路由。

**布局**：Anchor、线性、Panel、Splitter（比例持久化）。

**设计契约**：`DESIGN.md`（已过 design-md-review 与官方 lint）、
design_tokens（8px 间距体系、控件高度 24/32/40、字号 12/14/16/20/28pt）。

**示例（14 个）**：Minimal、Workbench、Showcase、ComponentGallery、Controls、
ControlsPlayground、Charts、ChartsInteractive、DarkStudio、DialogTour、
IconGallery、ResourceGallery、SettingsDemo、ThemeDemo。

---

## 2. 新库目标与定位

| 维度 | NativeFrameUI（现状） | WinFlux（新库） |
|---|---|---|
| 控件模型 | 每控件一个原生 HWND + 换皮 | **单 HWND 场景树**，控件是树节点 |
| 渲染 | GDI MemoryDC + GDI+ | **Direct2D 1.1 + DirectWrite**（GPU 合成、全 AA） |
| 编程模型 | 命令式：create/SendMessage | **声明式组件 + 状态 → 视图**（React 心智） |
| 布局 | Anchor/线性/Splitter | **Flexbox 盒模型**（row/column/gap/padding/margin） |
| 更新 | InvalidateRect 手动失效 | setState → 标记脏 → 下帧重建/重布局 |
| 定位 | 原生互操作优先的 V1 稳定库 | 面向所有新项目的基础 UI 库 |

新库**不是** NativeFrameUI 的替代品；两者并存。需要深度原生互操作
（OLE 拖放、系统菜单、UIA 原生语义）的场景继续用 nfui。

### 非目标（首版不做）

IME/TSF 富文本编辑、完整 DataGrid、Ribbon、Docking、插件系统、
跨平台（仅 Windows 10 1809+ x64）。

---

## 3. 架构设计

### 3.1 模块分层（沿用 nfui 的单向依赖纪律）

```text
flux_platform   HWND 宿主、消息循环、Per-Monitor DPI V2、时钟、dispatcher
  ↑
flux_render     Direct2D 设备/上下文、图元（圆角矩形/文本/线条/椭圆）、
                裁剪与变换栈、调色板缓存（COLORREF→ID2D1Brush）
  ↑
flux_layout     Flex 引擎：measure + layout（main/cross 轴、grow、gap、
                padding/margin、min/max 约束）
  ↑
flux_theme      DESIGN.md 同源 token：light/dark/HC 调色板、状态解析、
                8px 间距、圆角、字号（pt→px 按 DPI）
  ↑
flux_core       Element 树、Component（props→Element）、Host（帧循环：
                rebuild→layout→paint）、hit-test、焦点、事件冒泡、动画调度
  ↑
flux_components View/Label/Button/Checkbox/Radio/Panel/Progress/Card 等
```

### 3.2 React 概念映射（C++20 实现）

| React | WinFlux |
|---|---|
| `function Component(props)` | `ElementPtr render(const Props&)` 纯函数 |
| JSX 嵌套 | 元素构建器链 + `children({...})` |
| `useState` | `shared_ptr<State<T>>` + 闭包捕获，`set()` 触发重渲染 |
| 虚拟 DOM / reconcile | M0 全树重建（帧率预算内）；M2 增量 diff |
| 合成事件（bubbling） | 指针事件自叶向根冒泡，`stop_propagation()` |
| className / 样式 | Props 结构体（样式即数据）+ 主题 token |
| key | M2 reconcile 引入 |

状态模型：`flux::use_state<T>(host, init)` 返回句柄；组件闭包调用
`set(value)` → Host 标记脏 → 下一帧重建场景树并只重绘脏区域。

### 3.3 帧管线

```text
WM_PAINT / 脏标记
  → rebuild（root 组件 → Element 树）
  → layout（Flex：measure pass + layout pass，逻辑单位）
  → paint（遍历树 → D2D 图元，1x 逻辑→物理由 DPI 缩放矩阵统一处理）
  → Present（D2D HWND render target，双缓冲，撕裂保护）
```

DPI：逻辑单位贯穿树；渲染前对 render target 施加
`D2D1::Matrix3x2F::Scale(dpi/96)`，文本用 DWrite 按 pt→DIP 换算。

### 3.4 输入与焦点

- Hit-test：从根开始**反向深度优先**（最上层优先），逐层应用裁剪矩形；
  返回叶节点 → 建立捕获（pointer capture）直到抬起。
- 键盘：焦点序 = 树的前序遍历中 `focusable` 节点；Tab/Shift-Tab 移动；
  Space/Enter 激活按钮语义。
- 悬停：WM_MOUSEMOVE 驱动 hover 集合，进出产生 hover-in/out 事件。

### 3.5 主题

直接继承 nfui 已验证的 token 契约（`DESIGN.md`）：light/dark/HC 三调色板、
六状态（rest/hover/pressed/focused/disabled/error）、圆角 control 6 / card 10、
8px 间距体系、Segoe UI。HC 模式检测系统高对比开关并切换 AAA 色板。
主题切换沿用 200ms 颜色插值交叉渐变。

---

## 4. 组件路线（对照 nfui 功能迁移）

| 里程碑 | 组件 | 对应 nfui 功能 |
|---|---|---|
| M0（本次交付） | Host、View、Label、Button、Checkbox、Panel/Card、Progress | core、Button、StaticText、CheckBox、Panel、ProgressBar |
| M1 | TextInput（基础，无 IME）、Radio 组、Scroll 容器、Slider、Tooltip、Tab、StatusBar | Edit/Radio/Scrollbar/Slider/Tooltip/TabControl/StatusBar |
| M2 | 虚拟化 List、Tree、Splitter、Menu、Dialog、reconcile diff | ListBox/ListView/TreeView/Splitter/Menu/Dialog |
| M3 | 图表全家桶（D2D 重写，含拖拽编辑/缩放/十字准线） | nfui_charts(+aa) 全部 |
| M4 | PropertyGrid、IconView、命令路由、持久化集成 | PropertyGrid/IconView/Command/Persistence |

---

## 5. 技术决策

1. **渲染 API**：Direct2D 1.1 HWND Render Target（DWrite 1.0）。不引 D3D11
   互操作层（M0 不需要自定义合成）；Windows 10 1809+ 全部自带，零额外依赖。
2. **构建**：CMake 3.25+ Presets（x64-debug/release），`/W4 /permissive- /EHsc
   /utf-8 /MP`，静态库 `WinFlux::flux` 伞目标 + 模块目标，与 nfui 同款纪律。
3. **异常策略**：同 nfui——WndProc/渲染回调 noexcept，内部用 Result 风格返回码。
4. **无全局单例**：D2D 设备、主题、时钟全部由 Host 拥有。
5. **句柄策略**：整个应用一个顶层 HWND（Host 拥有），组件树不持有 HWND——
   这是与 nfui 的根本差异，换取统一合成与声明式更新。

---

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| 文本编辑/IME 复杂度 | M1 只做基础输入（键盘字符+退格+选区占位）；IME 留 M4 |
| 全树重建性能 | 树规模受控（<2k 节点时 <1ms）；M2 引 diff + 脏矩形 |
| D2D 设备丢失 | 检测 `D2DERR_RECREATE_TARGET` → 重建资源（标准流程） |
| 无障碍 | M4 提供 UIA Provider（单 HWND + 虚拟树映射） |
| 与 nfui 功能重复 | 明确分工：flux=新项目默认；nfui=原生互操作场景 |

---

## 7. 本次交付范围（M0）

新项目 `c:\tjf\github\WinFlux` 包含：

- CMake 骨架（presets、编译选项、模块库）；
- platform：`AppHost`（窗口创建、DPI V2、消息循环、定时器）；
- render：`D2DRenderer`（圆角矩形、文本、线条、椭圆、裁剪、调色板缓存）；
- layout：`FlexLayout`（row/column、grow、gap、padding、min/max）;
- theme：三主题 token（与 DESIGN.md 同源）；
- components：`use_state`、View/Label/Button/Checkbox/Card/Progress、
  hit-test、hover/pressed/focused/disabled 状态、冒泡事件、200ms 主题渐变动画；
- 示例 `Gallery`：标题区、主题切换、按钮行、复选框、计数器卡片、进度条——
  全部声明式构建，一个 HWND 渲染。
