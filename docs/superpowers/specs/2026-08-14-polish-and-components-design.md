# WinFlux 精细化打磨 + 高级组件 — 设计文档

日期：2026-08-14 · 状态：已批准（用户授权按推荐方案自主执行，免除逐节评审）

## 背景与目标

WinFlux 库现有 8 种原语元素（view / label / button / checkbox / progress / text_input /
slider / toggle_switch），Gallery 示例 2930 行单文件里用原语拼了 40+ 种组件（Phase A–F）。
2026-08-11 评审的 P0/P1 已在 Round 1–3 提交中修复（device-lost 重建、overlap-merge damage、
text layout 缓存、渲染裁剪、全 token 断言、force_full_dirty_ 收敛到 3 处）。

**剩余缺口（本设计的输入）：**

1. **无动画系统** —— 只有主题渐变一条 `WM_TIMER` 通道。
2. **渲染表现力**：无渐变、无投影、无图标（头像用首字母圆）、无 per-element 光标。
3. **无滚动**：全库 grep 不到 scroll；长列表/表格/树被硬阻挡。
4. **无浮层**：Modal 是流内卡片（无遮罩不阻断）、Toast 挤开布局、Tooltip 点击触发且挤开
   布局（Gallery 代码注释自认是简化假件）。
5. **组件不可复用**：全部 40+ 组件锁在 Gallery.cpp，sample 层无法复用。

**用户决策**（2026-08-14 对话确认）：打磨 = 视觉 + 引擎都要；新组件提升为库层
（`flux/Components.hpp`）；四类组件全做（选择菜单 / 数据展示 / 布局容器 / 图表）；
Demo = Gallery 瘦身 + 新独立 Dashboard 程序；执行方案 B（地基先行，视觉打磨合并进存量提升）。

## 总体结构：五轮

```
Round 1  视觉地基   动画系统 · 渐变/阴影 · 矢量图标 · 光标          ← 打磨阶段
Round 2  布局地基   ScrollArea（滚动+裁剪）+ 浮层通道 overlay        ← 打磨阶段
Round 3  组件库成立 flux/Components.hpp · 存量提升+动画一次到位      ← 打磨阶段
                     · Gallery 瘦身
Round 4  新组件     菜单选择 → 布局容器 → 数据展示 → 图表扩展        ← 组件阶段
Round 5  Dashboard 新示例程序 · 综合演示 · 质感收尾                  ← 组件阶段
```

依赖链：1、2 互相独立 → 3 依赖 1 → 4 依赖 2+3 → 5 依赖 4。每轮交付 = 零警告构建 +
双 preset ctest 通过 + 一次提交（沿用现有 Round/Phase 提交风格）。

## Round 1 — 视觉地基

### 1.1 动画系统（时钟驱动的声明式补间）

树每帧从零重建 ⇒ 动画状态**不进元素树**，分两层：

- **`flux::Animated<T>`**（app 侧，与 `State<T>` 并排）：`{from, to, start_ms, duration,
  curve}`，`current(now)` 返回插值；builder 构建时读它，若未播完则调
  `Host::animate_for(remaining_ms)` 挂住时钟。值域需 lerp：数值直插，Color 按通道插。
- **渲染期插值**（hover/press 高亮这类不经过树的）：Host 记录 hover/press 路径变更
  时间戳，`render_element` 按 `motion_fast_ms` 插值高亮强度。
- **`Host::animate_for(ms)`**：泛化现有 fade 定时器为统一动画时钟——延长截止时刻，每
  tick `request_render()`（受 `painting_` 重入保护），到期自动 KillTimer。主题渐变迁移
  到此时钟。
- **守恒规则**：动画时钟运行期间视同全脏（把 `fading_` 语义泛化为 `animating_`）——
  局部重绘只在静止时生效。滚动与 DPI 等既有 force-full 语义不变。
- **新 token**（Theme.hpp + DESIGN.md + smoke 断言三处同步）：
  `motion_fast_ms=120`、`motion_medium_ms=200`、`motion_slow_ms=280`；曲线复用
  `ease_in_out`，新增 `ease_out`（浮层入场用）。
- **`Props.opacity`**（0–1，新画笔字段 → 必须加进 `DamageTracker::paint_props_equal`）：
  自身背景/文字/边框按 alpha 合成；子树整体透明用 D2D `PushLayer(opacity)`，仅浮层入场
  使用（layer 有成本，路径受限）。

### 1.2 渲染表现力

- **线性渐变**：`Props.background_gradient`（可选 {angle, from, to}），画刷与纯色同样走
  渲染器画刷缓存。token：`gradient_subtle`（surface 提亮幅度）。
- **投影**：手绘衰减阴影——多层同心圆角矩形低 alpha 叠加（不依赖 ID2D1DeviceContext
  effects，任意 RT 可用，damage 友好）。`Props.shadow_elevation`（0–3 档，token 定
  每档半径/alpha/偏移）。
- **矢量图标**：新元素类型 `icon(IconKind, Props)`，24px 网格 D2D path geometry 描边图
  标，几何体进程级缓存。首批 ~20 枚：chevron×4、close、check、search、plus、minus、
  hamburger、user、trash、edit、arrow-right、calendar、inbox、play、pause、prev、next、
  info/success/warning/danger 圆形标。`IconKind` 进 Props（画笔字段）。
- **光标**：`Props.cursor`（arrow/hand/ibeam/size_ns/size_we/cross），Host 在 hover 命中
  变化时设 `WM_SETCURSOR` 光标。按钮默认 hand、text_input 默认 ibeam。

## Round 2 — 布局地基：scroll_view + overlay

### 2.1 `scroll_view`（库级元素）

- **状态归属**：滚动偏移存 Host 侧按路径索引的注册表
  `std::map<std::vector<int>, ScrollState>`——与 hover/press 同模式、同限制（位置身份，
  树形变即失效），写入文档。
- **滚轮**：`WM_MOUSEWHEEL` → 命中测试找最近可滚祖先 → 偏移 ± wheel delta（三行
  `control_height_md` 粒度）→ 视口全脏。
- **布局语义**：flex 把 scroll_view 当**固定视口尺寸的叶子**（期望尺寸来自自身
  Props/父级分配，与内容无关）；Host 在视口内以自然尺寸布局内容（内容期望宽度 =
  视口宽 − 滚动条预留，竖向滚动 v1）。`measure_leaf` 不穿透。
- **渲染**：`PushAxisAlignedClip(视口)`（现有能力）→ 内容平移 −offset 绘制 → 叠画悬
  浮滚动条（不占布局）。
- **damage**：滚动时树本身不变（平移在渲染期）⇒ 给 `DamageTracker` 增加
  `extra_dirty` 矩形入口，滚轮 handler 直接投视口矩形。
- **滚动条交互**：拖 thumb（pressed 路径 + mousemove）、点轨道翻页、方向键 / PgUp /
  PgDn / Home / End（走 focus）。auto-hide：滚动静止 `motion_medium_ms` 后淡出。
- **虚拟化预留**：`Host::scroll_viewport(path)` / `Host::scroll_offset(path)` 查询 API，
  Round 4 的虚拟列表据此只发射可见窗口行。

### 2.2 浮层通道 `Host::show_overlay(...)`

- overlay 栈独立于主树：**后绘制（盖顶）、先命中（顶层优先）**；空栈零成本。
- **锚定 = 纯 flex 组合**，不发明绝对定位：overlay 根是全窗口透明 view（`pass_through`，
  仅子树命中），三种锚定预设——
  - 居中（Modal）：根 column `align=center justify=center`；
  - 贴角（Toast）：根 column `justify=end align=end` + padding；
  - 锚定矩形（Dropdown/Menu/Tooltip）：spacer（高=锚.bottom）+ spacer（宽=锚.left）+
    内容自然尺寸；越界翻转到上方/左侧由组件层判断。
- **scrim**：overlay 根 Props `{scrim_color, scrim_opacity}`，命中即回调
  `on_scrim_click`（Modal 关闭语义）；scrim 存在时下层主树不参与命中。
- **damage**：overlay 变化 v1 视同全脏（overlay 树单独 diff 属后续优化，明确记为非目标）。
- **Tooltip 延迟**：hover 停留 `motion_slow_ms` 后显示、即离即藏——组件层用动画时钟 +
  `State` 组合实现，Host 只提供时钟与浮层。

## Round 3 — 组件库成立 + Gallery 瘦身

- 新文件：`include/flux/Components.hpp` + `src/components/`（Navigation.cpp /
  Overlays.cpp / Data.cpp / Charts.cpp，按类别分文件，防止单文件膨胀成第二个
  Host.cpp）。
- **API 契约：纯 builder 函数，无 `App&` 依赖**——规格+状态进、`ElementPtr` + 回调出；
  内部状态由调用方 `State<T>` 持有（沿 `text_input(on_change)` 惯例）：

  ```cpp
  namespace flux::ui {
    ElementPtr tabs(std::vector<TabSpec>, int active, Fn<void(int)>, Props = {});
    ElementPtr accordion(std::vector<SectionSpec>, int open, Fn<void(int)>, Props = {});
    ElementPtr segmented(std::vector<std::wstring>, int active, Fn<void(int)>, Props = {});
    ElementPtr pagination(int current, int total, Fn<void(int)>, Props = {});
    ElementPtr alert(AlertKind, std::wstring, Props = {});
    ElementPtr badge(BadgeKind, std::wstring, Props = {});
    void  toast_show(ToastHost&, ToastKind, std::wstring);      // 入队+自动过期
    ElementPtr toast_layer(const ToastHost&, Fn<void(int)>);    // 贴角 overlay 内容
    void  modal_show(Host&, std::wstring title, ElementPtr body, Fn<void()> on_close);
    ElementPtr tooltip(ElementPtr trigger, std::wstring text, Props = {});
  }
  ```

- **提升清单（9 件）**：Tabs、Accordion、SegmentedControl、Pagination、Alert、Badge、
  Modal、Toast、Tooltip。后三件基于浮层**重写成真件**（Gallery 版废弃，不搬运）；前六件
  参数化提升 + 用 Round 1 动画加过渡（SegmentedControl 滑块位移动画、Accordion 高度
  展开、Tabs 下划线滑动、Pagination 淡入）。
- 样式 token/palette 驱动，Props 可覆盖；新画笔字段（opacity、icon、cursor、
  gradient、shadow、scrim 等）全部同步 `paint_props_equal`。
- Gallery 瘦身：composite builder 删除、sections 改调 `flux::ui`，目标 ~1200 行纯展示。
  提升后 Gallery 视觉不回退（逐 tab 核对）。

## Round 4 — 新组件（四批，全部库层）

| 批次 | 组件 | 要点 |
|---|---|---|
| 选择菜单 | Dropdown/Select、Menu、ContextMenu、CommandPalette | 全走 overlay 锚定；键盘上下/回车/Esc；CommandPalette=居中 overlay + 模糊过滤输入 |
| 布局容器 | Splitter、Drawer、SideNav | Splitter：拖拽分栏（pressed+mousemove，光标 size_we/ns）；Drawer：贴边滑入 overlay；SideNav：可折叠 + 内嵌菜单 |
| 数据展示 | DataTable、TreeView、虚拟化 ListView | 全在 scroll_view 上；DataTable=列排序+行选择+斑马纹；TreeView=缩进+展开；ListView=固定行高虚拟化（只发射可见窗口） |
| 图表 | 折线/面积、饼图、Sparkline、Gauge | D2D geometry 直绘；坐标轴 token 化；悬停十字线 + 数值气泡（overlay 锚定） |

顺带提升 Gallery 既有的同族组件入库：RadioGroup/ColorPicker/DatePicker（选择菜单批）、
Calendar/BarChart/AvatarGroup/Breadcrumb（图表/数据批），Gallery 改调库版。

## Round 5 — Dashboard 示例

- 新 target `Dashboard.exe`（`samples/Dashboard/`）：SideNav 外壳 + CommandPalette
  （Ctrl+K）+ DataTable（排序/选择）+ 折线/面积/饼图 + Drawer 详情 + Toast 反馈，
  像真实分析应用的工作台。
- 全局质感收尾：间距/层级/暗色主题逐屏核对；顺手清理仓库根目录的像素 diff 脚本
  （`.gitignore` 或删除，向用户汇报后处理）。

## 测试策略

- 每轮：`cmake --build`（x64-debug + x64-release）零警告 + `ctest` 双 preset 全过。
- smoke（纯 CPU）新增：动画插值曲线/时钟边界（不用真定时器，注入 now）、
  DamageTracker `extra_dirty` 合并语义、新 token 断言、scroll 偏移 clamp 逻辑（抽出
  纯函数）、overlay 锚定 spacer 布局（走 flex_layout 纯函数路径）。
- host_lifecycle 新增：scroll_view paint round-trip、overlay 显示/关闭 round-trip、
  光标设置不崩。GUI 断言保持最小（有窗口会话才跑）。
- Gallery/Dashboard 人工验收：逐 tab 截图核对视觉不回退（沿用现有 pixel-diff 手法但
  不入库）。

## 风险与对策

| 风险 | 对策 |
|---|---|
| PushLayer 子树透明在 DComp surface 上语义偏差 | 仅浮层入场使用；fallback 为无动画直接显示 |
| 路径索引的滚动注册表在树形变时错位 | 与 hover 同批失效重置；文档明示；虚拟列表固定行高降低形变概率 |
| 手绘阴影层数多导致脏区放大 | elevation ≤3 档、层数 ≤4、半径进 token；damage 由 props 变化正常触发 |
| Host.cpp 持续膨胀（现 905 行） | 新能力尽量落在独立文件（Animation 独立小类、组件在 src/components/）；Host 只做接线 |
| 零警告 /W4 下 COM 层新代码告警 | 沿用现有 `com_ptr`/`winrt::check` 风格；每轮构建即验证 |

## 非目标（明确不做）

- 任意坐标绝对定位 / z-index 通用系统（三种锚定预设够用）。
- flex-wrap、双向同时滚动、触摸惯性滚动。
- 位图/WIC 图片加载、字体图标、RTL。
- overlay 树独立 damage diff（v1 全脏）。
- 无障碍（UIA）——记录为未来方向。
