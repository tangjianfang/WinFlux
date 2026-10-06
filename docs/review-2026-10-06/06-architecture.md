# WinFlux 软件工程与高可扩展性评审（06-architecture）

- 评审对象：`C:\tjf\github\WinFlux` @ master `8eb46e4`（2026-10-06 13:44 +0800），首提交 2026-08-05，共 61 commits，无 tag。
- 方法：全量通读 src/include（16+12 文件，6753 行）、tests（1138 行）、CMake/CI、四份根文档与 docs/superpowers；正则启发式量化脚本见 `exp/metrics.py`。**未构建、未运行任何目标**——所有"运行时"结论均为静态推断，已在文末标注。
- 每条发现标注：**Confirmed**（读到代码、机制完整）/ **Likely**（机制确凿、触发条件推算）/ **Speculative**。

---

## 1. 摘要

**架构健康度总评：6.5 / 10（B−）。** 核心算法层（FlexLayout / DamageTracker / Theme / 纯函数组件）质量高、无环单向依赖、演进纪律（小步 commit + 计划文档 + 零警告）显著优于同规模单人项目；主要债务集中在三处：**Host.cpp 上帝对象**（12 个职责簇、1615 行）、**新增能力的霰弹式修改且无编译期保护**（元素类型 / paint_props_equal / token 三副本）、**交互与渲染核心约 2600 行在 CI 中零断言覆盖**。这三者互相放大：God file 不可测 → CI 跳过 → 扩展点无回归网 → 每次扩展靠人肉记忆 5 步清单。

### 分维度评分

| 维度 | 分 | 关键证据 |
|---|---|---|
| 分层与依赖 | 7.5 | include 图 Tarjan **无环**；Theme→Element→Layout/Damage/Render→Host→Components 严格单向；但 Host.hpp 值成员 presenter 使 d3d/dcomp 头泄漏到全部 flux::ui TU |
| 核心算法质量 | 8 | DamageTracker 指针快路径 + 交互态 diff + max_rects 收敛；flex 两遍测量 + 按地址缓存；边界处理细致 |
| 可测试性 | 5.5 | 纯 CPU 层优秀；Host/渲染/呈现层除 7 个 CI-跳过的 check 外零覆盖；D2DRenderer 无接口不可 mock |
| 扩展性 | 6 | 组件层 🟢（2 文件 100 行级）；基础元素 🟡（4-6 文件无保护）；nfui 契约 🔴（无机制） |
| 横切一致性 | 6.5 | HRESULT 纪律好（67 处 void 型语句调用中真正丢弃 HRESULT 的仅 2 处）；异常策略自相矛盾；线程假设无机器检查 |
| 构建与质量基建 | 6 | 双 preset + /W4 零警告成立；无 tag/版本号、无 /analyze、无 sanitizer、25/48 文件 LF 混行尾 |
| 测试体系 | 5.5 | 231 个断言（224+7），CI 实际执行 224；手写框架无过滤/无隔离 |
| 文档 | 6.5 | 内容密度高，但存在 1 条**误导性**漂移（CLAUDE.md force_full_dirty_ 条目已过时）+ 11 条一般漂移 |

### Top 10 结构性问题

1. **Host.cpp 上帝对象**：builders、wndproc、交互状态机、滚动引擎、tooltip、overlay 通道、主题渐变、动画时钟、帧管线、paint 分发、叶子测量策略、perf 埋点 ≈ 12 个职责簇；`render_element` 214 行、启发式圈复杂度 ~80（Host.cpp:1365）。**Confirmed**。
2. **新增基础元素 = 霰弹式修改且零保护**：Element.hpp / Host.cpp(builder) / render_element / measure_leaf / paint_props_equal 4-6 处；type 是 `std::wstring`（27 处运行期字符串比较），漏写 paint_props_equal 或拼错 type 均无编译期/测试期信号，症状是"静默不重绘/透明元素"。**Confirmed**。
3. **交互与渲染核心零 CI 覆盖**：CI 设 `WINFLUX_SKIP_GUI_TESTS=1`（ci.yml:33），host_lifecycle 的 7 个 check 在 CI 全部短路为 return 0；Host.cpp（除 builders ~140 行）、D2DRenderer 绘制路径（除 measure_text）、DCompPresenter/HwndPresenter 合计 ~2500 行只有"编译覆盖"。**Confirmed**。
4. **异常策略名不副实**：wndproc 的 `catch(...)`（Host.cpp:263）不可达——其 try 块内调用的全部 Host 方法均声明 noexcept，用户回调（root_builder_/on_click/on_text_change/props.paint）抛异常将直接 `std::terminate`；PaintScope 注释（Host.cpp:151-155"…or a throw"）与 Host.hpp 头注释均与此矛盾。**Confirmed**（语言规则级静态结论，未运行复现）。
5. **paint_id 约定脆弱且有实际缺陷**：`radar_chart` 的 `paint_id = 0x5ADA9 ^ (unsigned)(max_v*100)`（Charts.cpp:347）只混合了 series 最大值——数据在 max 不变的情况下变化（如 1,3,2 → 2,3,2）时 rect 不变、props 相等、id 相等 ⇒ 脏区为空 ⇒ 图表永远不更新，直到其他元素弄脏该区域。**Confirmed**（代码级；未活体复现）。radar_chart 内还有死代码（Charts.cpp:363-364 `const auto axis = …; (void)axis;`）。
6. **设计 token 三副本手工同步 + nfui 第四副本**：DESIGN.md ↔ Theme.hpp ↔ smoke.cpp 各存 30 个数值（当前 30/30 一致），与 nfui 的共享纯靠纪律；新增 token 忘记任一副本无任何报警。**Confirmed**。
7. **presenter 半开缝**：IPresenter 接口干净（6 方法），但 Host 按值持有 `DCompPresenter dcomp_presenter_; HwndPresenter hwnd_presenter_;`（Host.hpp:195-196），选择逻辑硬编码在 `Host::create`，无法注入第三方 presenter（如离屏/截图/测试用）；且 Host.hpp 因此必须 include d3d11/dcomp/dxgi 头，连带 Components.hpp 的全部 11 个下游 TU。**Confirmed**。
8. **单线程假设无机器检查**：`State<T>::set → request_render → paint_frame` 同步重入模型完全依赖调用线程 == UI 线程，但无 `GetCurrentThreadId` 断言/校验（全仓库无任何线程检查）。**Confirmed**（缺失型）。
9. **重复代码残留（round 8 收敛后仍存）**：slider/drag_handle 拖拽映射逻辑在 handle_mouse_move（Host.cpp:738-760）与 handle_lbutton_down（Host.cpp:863-885）整段重复 ~18 行；`thumb_h = max(24, vh*(vh/content_h))` 公式 3 处（721/816/1601）；`toast_stripe`（Overlays.cpp:156-165）与 `kind_color(ToastKind)`（Overlays.cpp:23-31）同映射两份；spacer+row 锚定模式两份（Host::build_tooltip 470-505 内联 vs Menus.cpp `anchored_popup` 19-39，后者还是匿名命名空间私有）；`dpi>0?dpi:96` clamp 4 处。**Confirmed**。
10. **无版本化基建**：无 git tag、VERSION 只有 CMake `project(... VERSION 0.1.0)`、无 changelog；姊妹项目消费时只能靠 commit hash 引用；Release preset 未显式开 PDB（VS 生成器默认 Release 有 .pdb，但未验证产物——静态标注）。**Confirmed**（仓库状态）。

---

## 2. 度量数据

### 2.1 编译单元行数（wc -l，物理行）

| 文件 | 行数 | 角色 |
|---|---|---|
| samples/Gallery/Gallery.cpp | 2488 | 样例（30+ State 字段、~30 section 函数、209 个函数） |
| src/core/Host.cpp | 1615 | **God file**：builders+wndproc+交互+帧管线+paint 分发 |
| tests/smoke.cpp | 1035 | 17 个 test_* / 224 断言 |
| src/render/D2DRenderer.cpp | 731 | D2D/DWrite 图元 + 6 类缓存 |
| src/components/Charts.cpp | 426 | 7 种图表 |
| samples/Dashboard/Dashboard.cpp | 398 | 样例（较健康的组装方式） |
| src/components/Overlays.cpp | 367 | alert/badge/toast/modal + 共享 chrome |
| include/flux/Components.hpp | 357 | flux::ui 全部声明 |
| include/flux/Host.hpp | 308 | Host + State<T> |
| 其余（DComp 271 / Menus 263 / Flex 231 / Damage 225 / Data 224 / DataViz 208 / Nav 188 / Element.hpp 185 / Pickers 177 / Layout.cpp 158 …） | | |
| **合计** | **10777**（src 5265 + include 1488 + samples 2886 + tests 1138） | |

### 2.2 函数长度 / 圈复杂度 Top（正则启发式；纯数据 switch 会虚高，如 build_icon 的 cx~19 实为坐标表）

| 函数 | 行数 | cx~ | 位置 |
|---|---|---|---|
| render_element | 214 | **80** | Host.cpp:1365 |
| test_damage | 201 | 15 | smoke.cpp:805 |
| paint_frame | 200 | **36** | Host.cpp:1119 |
| handle_key_down | 115 | **40** | Host.cpp:980 |
| handle_lbutton_down | 106 | **44** | Host.cpp:795 |
| build_icon | 136 | 19（数据表假阳性） | D2DRenderer.cpp:456 |
| handle_mouse_move | 86 | 25 | Host.cpp:700 |
| paint_props_equal | 31 | 29（字段清单本质） | Damage.cpp:99 |
| begin | 84 | 15 | D2DRenderer.cpp:103 |
| update_cursor / resolve / hit_test_all | 29-42 | 10-13 | Host.cpp |

全仓 225 个函数：>100 行 17 个、>50 行 42 个、cx>10 18 个；集中在 Host.cpp（7/17）与 Gallery 样例（9/17）。

### 2.3 include 图（fan-in / fan-out，Tarjan SCC）

- **无循环**（0 个多节点 SCC）。单向链：`Theme(0 依赖) → Element → {FlexLayout, Damage, D2DRenderer/Presenter} → Host → Components → {samples}`。
- fan-out Top：Host.hpp **9**、flux.hpp 6、Components.hpp 3；fan-in Top：Components.hpp **11**、Theme.hpp/Element.hpp 各 6。
- **AGENTS.md 声称的分层单向依赖成立**（core 依赖 render 方向：Host.cpp include D2DRenderer/DComp/Hwnd/Damage——Host 是组合根，render 不知道 Host）。**Confirmed**。
- 防火墙弱点：Components.hpp → Host.hpp → DCompPresenter.hpp → `d3d11.h/dxgi1_2.h/dcomp.h`（Host.hpp:17-20, DCompPresenter.hpp:17-20）。⇒ 每个 flux::ui 组件 TU、每个样例 TU 都编译 D3D/DComp 头。**Confirmed**。

### 2.4 其它计数

| 指标 | 值 |
|---|---|
| expect()/check() 断言 | smoke 224 + lifecycle 7 = **231** |
| `e.type == L"..."` 字符串比较 | 27 处（Host.cpp 21、FlexLayout 2、smoke 4） |
| force_full_dirty_ | **2 个置位点**（resize Host.cpp:540、DPI :551），1 个复位（:1264） |
| 丢弃 HRESULT 的调用 | 2 处：`dcomp_device_->Commit()`（DCompPresenter.cpp:165, resize 路径）、错误清理路径的 `surface_->EndDraw()`（:226，刻意）；EndDraw 主路径、Commit 主路径均已检查 |
| printf 家族 / OutputDebugString（src+include） | 5 / 3（集中在 perf 日志与 HwndPresenter 失败提示，无散落调试遗留） |
| noexcept | 334 处（Host.hpp 52 处方法级全覆盖） |
| TODO/FIXME | 0 |
| 行尾 | 25 个 tracked 文件 LF / 23 个 CRLF，**无 .gitattributes**；无 BOM |
| 根目录 ps1 | 35 个；.visual-test 73 个文件（62 PNG）；.qoder 含 repowiki/diff 等 |
| .gitignore 覆盖 | `/*.ps1` `.qoder/` `.visual-test/` `out/` 全部命中（git check-ignore 实测） |

---

## 3. 分层与可测试性

**分层现状（与 docs/DIRECT_RENDERING_UI_PLAN.md §3.1 的 6 目标库对照）**：实际是单一 `flux` 静态库 + 目录分层。逻辑分层成立且无环，但"平台层"名不副实——AppHost.cpp 仅 30 行（DPI context + 消息循环），真正的平台胶水（wndproc、定时器、光标）都在 Host。计划中的模块目标（flux_render/flux_layout…）从未拆分，AGENTS.md 的模块表如实描述了目录而非库目标（不算漂移，但计划文档与现实分叉，见 §7）。

**可测试性分层**：

| 层 | 可测性 | 现状 |
|---|---|---|
| Theme/FlexLayout/Damage | 纯 CPU，头依赖极小 | smoke 覆盖良好（flex justify/scroll/grow、damage 9 类场景、token 全量） |
| flux::ui 纯 builders | 纯函数 | smoke 覆盖**结构**（children 数、回调、选中态），不覆盖布局后果与绘制 |
| D2DRenderer | 具体类、无接口 | 仅 measure_text（DWrite 可无头）5 断言；绘制路径零覆盖 |
| Host | 具体类 + 真 HWND + DComp | 仅 lifecycle 7 check（CI 跳过） |
| Presenters | IPresenter 是接口，但 Host 按值持有、选择硬编码 | 零覆盖；无法强制走 fallback 路径 |

- **测试目标拼源方式**：CMake 三目标均 `target_link_libraries(... WinFlux::flux)`——共享同一静态库源清单，**无两份手抄**。🟢（CMakeLists.txt:53-64）
- **IPresenter 缝够不够**：对"换呈现后端"够（6 方法、逻辑单位契约清晰、HwndPresenter 证明可行）；对"无头测试 Host"不够——D2DRenderer 仍是具体类且 Host 构造即拉起 dcomp/hwnd 成员。要 headless 跑 paint_frame 需要 NullPresenter + renderer 缝，两者目前都不存在。
- **核心数据结构直接单测**：Element/Props 有（test_builders、paint_props_equal 系列）；State<T> **没有**（bind/notify 路径零测试）；Host::overlay/hit_test/resolve/collect_focusable 等 **全部没有**（private 且无测试缝）。
- "仅编译覆盖"比例：约 6753 行 src+include 中，被任何断言执行过的 ≈ 2800 行（纯函数 + builders + measure_text），**~3900 行（Host 除 builders 外 ~1475 + D2DRenderer ~680 + 双 presenter 351 + AppHost 30 + 组件绘制闭包）为纯编译覆盖**。Likely（按调用图推算）。

---

## 4. 扩展性走查（逐项实走）

评级：🟢 低摩擦 / 🟡 中摩擦或无保护 / 🔴 高摩擦或无路径。

| # | 扩展场景 | 需改文件 | 估计行数 | 霰弹式? | 编译期保护 | 评级 |
|---|---|---|---|---|---|---|
| a | 新基础元素类型 | Element.hpp + Host.cpp(builder/render/measure) + Damage.cpp | 45-60 行 / 4-6 处 | 是 | **无** | 🟡 |
| b | 新 flux::ui 复合组件 | Components.hpp + 1 个 src/components/*.cpp（+可选 smoke） | 80-150 行 / 2-3 处 | 否 | 有（漏声明=链接错） | 🟢 |
| c | 新设计 token | DESIGN.md + Theme.hpp + smoke.cpp | 各 1-3 行 / 3 处 | 是（三副本） | 无（smoke 清单手工维护） | 🟡 |
| d | 新 overlay 组件 | Components.hpp + 1 组件 cpp（+样例根 builder） | 60-120 行 | 否 | 无（锚定样板需复制） | 🟡 |
| e | 新图表类型 | Components.hpp + Charts.cpp（+smoke 几何测试） | 60-100 行 | 否 | 无（paint_id 自负其责） | 🟡 |
| f | 新主题/暗色模式 | Theme.hpp(enum) + Theme.cpp(switch) [+DESIGN.md] | ~25 行 | 轻 | 无（default 吞掉新枚举值→静默回落 light） | 🟡 |
| g | 换呈现层（新 presenter） | 新 h/cpp + **Host.cpp/Host.hpp**（create 选择分支 + 成员） | 100 + 修改 Host | 是（要动组合根） | 接口有、注入无 | 🟡 |
| h | nfui 共享 token 契约 | 无机制（手工复制 30 常量到 nfui） | — | — | **无** | 🔴 |

细节与证据：

**(a) 基础元素**。以现存的 `toggle_switch` 为标本反推：Element.hpp:123-131 声明（9 行）→ Host.cpp:123-132 定义（10 行）→ render_element 的 `L"toggle"` 分支（Host.cpp:1538-1552，15 行）→ 复用 `checked` payload 故 measure_leaf/paint_props_equal 未加行；对照 `text_input`（加了 input_error 字段）：Element.hpp:108 + Damage.cpp:120 + smoke.cpp:619-624 三处联动。**摩擦点**：① 5 步清单全部靠人肉（CLAUDE.md 有文档但无机器检查）；② `drag_handle`（Layout.cpp:12-29 手工 `make_shared<Element>` + 裸字符串 type）证明存在**绕过清单的暗路径**——它无公共 builder、无 render 分支（靠通用背景栈渲染）、无 measure 特例，恰好零遗漏，但下一个绕过者未必；③ 拼错 type（如 `L"buton"`）渲染为普通 view，无 assert、无告警。**遗漏风险实测**：paint_props_equal 的 spot-test 只覆盖 ~10/28 个 paint 字段（smoke.cpp:944-965, 404-406, 545-554, 613-624），新增字段漏写 diff 的概率高且症状隐蔽。

**(b) 复合组件**。round 7 实际新增 radar/scatter：Components.hpp + Charts.cpp + smoke test_round7，三文件零核心改动。round 8 收敛出 popup_card/menu_item_button/caption/icon_button 四个共享 chrome 后，新静态组件确为 🟢。

**(c) token**。三副本手工链：DESIGN.md 表、Theme.hpp `inline constexpr`（30 个，当前 30/30 一致——逐项核对通过）、smoke.cpp:56-92（31 条 expect，含 1 条聚合）。**没有生成器/校验脚本**；忘写 smoke 断言时该 token 的契约即失去执行。token 为编译期 constexpr——运行期不可换（见 f）。

**(d) overlay**。六个 host（Toast/Modal/Select/Menu/Command/Drawer）模式统一：app 侧 host struct + show/close + `overlay_*(host,...)` 每帧声明。样板量实测：`overlay_select` 18 行、`overlay_menu` 22 行、`overlay_command_palette` 108 行。**遗留摩擦**：锚定 popup 的 spacer+row 模式有两份互不知晓的实现（Host::build_tooltip 内联 vs Menus.cpp 匿名 `anchored_popup`），新 anchored overlay 要么复制第三份要么跨 TU 暴露 helper——目前没有公共锚定 API。

**(e) 图表**。paint 回调 + paint_id 模式本身优雅（数据即 id），但 id 推导无约定强制：line/area/sparkline/pie 用 `series_id(values)`（逐值哈希，正确），**radar 只用 max_v（缺陷，见 Top10-5）**，gauge 用 clamp 后 t*1000，scatter 逐点。修复模式应是统一 `chart_id(所有输入)`。图表闭包按值捕获 Palette（每帧每图复制 64B，可忽略）。

**(f) 主题**。运行期可换的是 3 个硬编码 `ThemeMode`；`palette()` 是 switch + `default:` 吞新枚举（Theme.cpp:29-30）——加枚举值忘加 case 会**静默回落 light**（/W4 不报，因为有 default）。fade 路径（set_theme_mode → lerp_palette → anim clock）对新模式免费可用。**自定义品牌色无入口**：组件签名收 `const Palette&`（app 可传自制 Palette），但窗口清屏色/渐变基线绑定在 Host 私有 `shown_` 上，Host 不接受外部 Palette 注入。Likely（接口面读全，未写样例验证）。

**(g) presenter**。正向：IPresenter 6 方法 + SurfaceFrame 契约文档极好，HwndPresenter 80 行完整实现 fallback（含 recreate_target 失败日志、minimize 1x1 保护）；DComp 失败自动降级在 Host::create:305-313。反向：注入不可行（见 Top10-7）；fallback 路径**不可测试**（无法强制 DComp 失败，除非拔显卡）；DComp resize 中 create_surface 失败后 `valid()` 恒 false，直到下一次成功 resize 才恢复——降级面可用但无自愈重试。

**(h) nfui**。仓库内零机制：无 submodule、无 vendored 头、无同步脚本、无 CI 比对；DESIGN.md 自述"与 nfui 共享契约"仅是文档承诺。token 值甚至同时存在第 4 份拷贝（nfui 仓库）。这是 8 个维度中唯一 🔴。

---

## 5. 横切一致性

- **HRESULT**：见 §2.4——真正的丢弃仅 resize 路径 Commit 一处（低危：Commit 失败下一帧仍会重试），错误清理路径 EndDraw 一处（刻意）。2026-08-11 记忆中的"EndDraw 丢弃"已修复（DCompPresenter.cpp:247-260 双 EndDraw 均检查并触发 rebuild）。🟢
- **异常**：见 Top10-4。另外 `Host::set_root_builder`/`overlay` 等 noexcept 函数内移动 `std::function` 理论不抛；真正的抛掷源全是用户回调。**修复方向**：要么在 paint_frame/dispatcher 内部对用户回调逐点 try/catch（吞掉并记日志），要么明确 fail-fast 并删掉不可达的 wndproc catch 与误导注释。现状是"看起来有边界，实际一触即溃"。Confirmed。
- **线程**：单线程模型未写进任何机器检查；`State<T>` 可被任意线程 set。建议 debug-only 断言（R6）。Confirmed（缺失型）。
- **命名/注释**：注释密度与质量非常高（英文，讲 why 不讲 what），0 TODO；少数注释已失真：Menús.cpp:180-181 "re-filters on press (captures the query, not the snapshot)" 与实际代码（捕获 `matches_copy` 快照）不符（行为因每帧重建而无恙，注释错误）；radar 死代码；data_table `is_sel ? pal.text : pal.text`（Data.cpp:97）双分支同值。
- **魔数与集中管理**：定时器 ID 单一（`anim_timer_id_ = 1`，Host.hpp:286）集中 ✓；元素 type 字符串散落 27 处 ✗；滚动步长/hold 时长（600ms、0.9 页、24px 最小 thumb）内联在 Host.cpp ✗；组件默认尺寸（28/22/180/460…）内联在各组件 ✗——多数有注释解释，尚可接受。
- **日志**：WINFLUX_PERF_LOG 通道设计良好（零开销开关 + release 可用 + skip 单独计数）；无其它散落 print。🟢

---

## 6. 构建、质量基建与仓库卫生

- **构建结构**：单库 + 2 样例 + 2 测试，编译选项经 `flux_apply_compiler_options` 函数集中施加（cmake/WinFluxCompilerOptions.cmake），/W4 /permissive- /EHsc /utf-8 /FS /MP /Zc:__cplusplus 全目标一致。零警告约定从提交历史看持续维持（round 6-8 均无警告修正提交，CI 双 preset 构建把关）。🟢
- **CI**：双 preset 构建+ctest，windows-2022 固定镜像（因 VS18 兼容），2 个环境修正提交说明 CI 是活的。**缺口**：GUI 测试跳过 = Host 生命周期测试在 CI 永不执行（这是已知取舍，但没有任何替代——连"离屏 smoke 渲染"都没有）；无 /analyze、无 clang-tidy、无 ASan/UBSan job（MSVC `/fsanitize=address` 在 x64 debug 可开，加一个 preset 即可）；无覆盖率。
- **版本戳**：无 tag、无版本头、无 changelog；`project(VERSION 0.1.0)` 是唯一版本声明。nfui/下游引用只能靠 commit hash。Release 符号：VS 生成器默认 Release 产 .pdb（未验证产物目录——静态推断）。
- **仓库卫生**：`.gitignore` 实测覆盖 `/*.ps1` `.qoder/` `.visual-test/` `out/`——CLAUDE.md "untracked scratch" 表述有规则背书，**不会误入库**；但 35 个 ps1 + .visual-test 73 文件长期驻留工作区根，属可归档噪声（建议移 `tools/scratch/` 或删除，blacklist 场景见局限）。tracked 文件无 BOM；**25/48 LF 与 CRLF 混存**（round 3+ 的新文件多为 LF），缺 .gitattributes——跨机器 checkout 会产生假 diff。git ls-files：src 16、include 12、docs 8、samples/tests 各 2、根 7 —— 结构干净。

---

## 7. 测试体系与文档漂移

### 7.1 测试金字塔

| 层 | 有/无 | 证据 |
|---|---|---|
| 纯逻辑（flex/damage/theme/几何/验证器/动画） | **有，224 断言，质量高** | smoke 17 个 test_*，含边界与回归用例 |
| 组件结构（builder 输出形状/回调） | 有（结构级） | test_components/menus/layout/data |
| 布局-渲染集成（render_element 分支、chrome） | **无** | 无任何绘制断言 |
| 交互路径（hit-test/Tab/滚动/overlay Esc） | **无**（生命周期目标只 check hwnd 非空） | host_lifecycle.cpp:59-95 |
| 性能回归 | 无（有手工 perf log 通道） | WINFLUX_PERF_LOG |
| 视觉回归 | 工具链存在但**非测试资产**（.visual-test 62 张 PNG + 35 个 ps1 点击/像素脚本，无断言、不进 CI、不可重复 seed） | 仓库观察 |
| 呈现层（DComp/Hwnd） | 无 | — |

手写框架自身缺陷：expect/check 失败后**继续执行**（无中止语义——后续断言可能建立在坏状态上，好在测试多为独立小场景）；无名字过滤（CLAUDE.md 已声明是有意决策，且给出了注释隔离的替代法——**该决策在当前规模仍合理**，224 断言 17 组函数的粒度不需要框架）；无异常捕获（测试内抛异常=崩溃而非 fail）；无耗时/统计。

### 7.2 文档漂移清单（逐条对代码核对，≥10 条）

| # | 文档声称 | 代码现实 | 判定 |
|---|---|---|---|
| 1 | CLAUDE.md「Damage tracking—biggest footgun」：**每次** hover/pressed/focus 变更必须 `force_full_dirty_ = true` | 已过时：交互态经 `DamageTracker::InteractionState` 进 diff（Host.cpp:1211-1214，Damage.cpp:209-213），hover 变更是**局部重绘**；force 位现存仅 resize/DPI 2 处；smoke.cpp:896-940 有专门回归测试 | **误导性漂移（重要）**——会诱导贡献者加多余的全屏失效，抵消 partial-repaint 优化 |
| 2 | CLAUDE.md 同节：「两棵树逐字节相同 ⇒ diff 空 ⇒ 不重绘」 | InteractionState 参数化后不再成立 | 漂移（同上条） |
| 3 | AGENTS.md 组件清单 charts「line/area/pie/sparkline/gauge」 | 还有 radar/scatter（round 7，Charts.cpp:307-424） | 漂移 |
| 4 | AGENTS.md：lifecycle 测试「exercises presenter selection」 | 实际只验证 create 成功（用哪个 presenter 都过），无法选择/强制 fallback | 轻微夸大 |
| 5 | flux.hpp:6-7「See docs/DIRECT_RENDERING_UI_PLAN.md **in the NativeFrameUI repository**」 | 该文档 2026-10-06 已入本仓库 docs/ | 过时指针 |
| 6 | DIRECT_RENDERING_UI_PLAN §3.1 六个模块库（flux_platform…flux_components） | 单一 `flux` 静态库（CMakeLists.txt:15-33），platform 仅 30 行 | 计划 vs 现实分叉（文档自称"方案"，可接受但应补现状注记） |
| 7 | 同 §3.3「Present（D2D HWND render target）」 | 默认路径是 DComp（DCompPresenter），Hwnd 是 fallback | 演进漂移 |
| 8 | 同 §3.2 `flux::use_state<T>(host, init)` | 实际 API 是 `Host::make_state<T>` | 命名漂移 |
| 9 | 同 §3.4「事件冒泡 / stop_propagation()」 | 不存在事件对象与冒泡 API；是 handler 内 walk-up 解析 | 未实现特性未标注 |
| 10 | 同 §3.5「六状态 state_palette」「HC 检测系统高对比开关」 | 无 state_palette、无系统 HC 检测（手动 set_theme_mode） | 未实现特性未标注 |
| 11 | DIRECT plan §4 路线：M1 StatusBar、M3 高级图表交互（拖拽编辑/框选/缩放/撤销）、M4 PropertyGrid/IconView/命令路由/持久化 | 均未实现（M0-M2 基本达成；charts 只有 hover crosshair） | 路线图状态未回写 |
| 12 | CLAUDE.md/AGENTS.md「builders 全在 Host.cpp」 | 属实（Host.cpp:14-140） | ✅ 无漂移 |
| 13 | DESIGN.md ↔ Theme.hpp 30 token | 逐项核对 30/30 一致 | ✅ 无漂移 |
| 14 | CLAUDE.md 测试表、smoke 覆盖描述 | 与 smoke.cpp 实测一致（含 224 断言分布） | ✅ 无漂移 |
| 15 | docs/superpowers 计划：round1-4、7、8 有 plan 文件；round 5/6 无（5 轮总 spec 覆盖 1-5；6a/6b/6c 有 commit 无 plan） | round 7/8 抽查完成项（overlay 滚动 Host.cpp:1193-1198、chrome helpers Overlays.cpp:33-79、CI）均属实 | ✅ 已标"complete"的属实；组织上 1458 行计划文档与 1 份 spec 混放，检索价值中等 |

---

## 8. 值得保留的工程实践（重构时不要破坏）

1. **零警告 + 选项集中函数**（cmake/WinFluxCompilerOptions.cmake）——所有扩展的硬底线。
2. **paint_props_equal 白名单语义**（layout 字段靠 x/y/w/h 浮出、交互字段显式排除）——方向正确，只需反转默认（见 R1），不要改成"比较一切"。
3. **纯 CPU / GUI 测试双目标拆分**与"不为小规模引框架"的克制。
4. **IPresenter + SurfaceFrame 契约**（逻辑单位、物理偏移注释）——呈现层抽象的正确范本，扩展它而不是绕过它。
5. **damage 快路径三件套**：指针相等短路、interaction 折叠、max_rects 全屏收敛。
6. **逻辑单位 96DPI 基线贯穿**、只有 presenter/换算点碰物理像素的纪律。
7. **request_render 直驱 presenter + painting_ 再入保护 + generation 短路**的帧模型。
8. **小步 commit + 计划文档 + 回归测试随修随加**的演进纪律（git log 质量明显高于均值）。
9. 注释讲 why 的文化（SurfaceFrame/DamageTracker/再入保护等处的注释是新人上手的主要资产）。
10. WINFLUX_PERF_LOG 零开销插桩模式。

---

## 9. 演进路线图（按 ROI 排序，全部可逐 commit 落地）

| ID | 项 | 动机 | 做法（增量步骤） | 工作量 | 风险 | 收益 |
|---|---|---|---|---|---|---|
| **R1** | 反转 paint_props_equal 默认 + 元素类型枚举 | Top10-2/5 的根：新增字段/类型零保护 | ① 给 Props 加 `interaction_equal()`（on_*/cursor/tooltip/autofocus/drag_vertical/width/height/layout 字段），`paint_props_equal` 改为 `operator==全字段 && interaction_equal`——新字段自动进 diff；② `Element::type` 旁加 `enum class ElementType`，wstring 由它派生（保留 wstring 兼容或一次性切换）；③ 删 27 处字符串比较改 switch | M | ①需仔细核对现排除清单（smoke 已有 8 条 spot-test 兜底）；②是破坏性改动，样例+组件同步改 | 扩展安全性质变；diff 遗漏类 bug 绝迹 |
| **R2** | 修 radar paint_id + 图表 id 统一 + 微清理 | Top10-5（已确认的陈旧重绘 bug） | `chart_id(vector<vector<float>>)` 混入全部值+轴数；radar/gauge/scatter 改用；顺手删 Charts.cpp:363 死代码、Data.cpp:97 同值三目、修 Menus.cpp:180 注释 | S | 无 | 消除一个静默渲染 bug |
| **R3** | Host 拆分（纯搬移） | Top10-1 | ① builders → `src/core/Builders.cpp`（~130 行，零语义变化）；② `render_element`+`draw_scrollbar` → `ElementPainter`（收 renderer_+交互快照引用）；③ 输入处理 → `InputController`；④ 滚动引擎 → `ScrollEngine`。每步一 commit，头文件不动公共面 | M-L | 搬移期 conflict 风险；行为必须零变化（以 R5 的测试网兜底） | Host.cpp 1615→~700；后续每块可独立测试 |
| **R4** | 无头渲染测试网（最高杠杆测试投资） | Top10-3：核心 0 覆盖 | ① 加 `TestPresenter : IPresenter`（记录 draw 调用序列，返回 WARP/软件 D2D RT 或空 rt 时只测调度）；② Host 加 presenter 注入 ctor 重载（不动默认路径）；③ 把 host_lifecycle 的场景改写为断言式（hit_test/Tab/Esc/overlay 顺序），CI 不再跳过 | M | D2D 软件栈在 runner 的可用性需实验（DComp 路径需真窗口站，TestPresenter 可绕开） | ~2500 行零覆盖代码获得回归网；重构 R3 的安全带 |
| **R5** | 异常策略落地 | Top10-4 | 二选一并写进 CLAUDE.md：A) paint_frame/dispatcher 内对用户回调逐点 try/catch（吞+OutputDebugString）；B) 明示 fail-fast，删除 wndproc 不可达 catch 与两处误导注释 | S | A 改变失败语义（吞异常可能留半帧状态） | 消除"看起来有边界实际 terminate"的认知陷阱 |
| **R6** | 线程与不变量 debug 断言 | 单线程假设无机器检查 | Host::create 记录 `GetCurrentThreadId`，request_render/overlay/animate_for 前 `assert`；`Host::overlay` 在非 painting_ 调用时目前静默丢弃——加 assert | S | 无（debug-only） | 误用即报，而非偶发重入 |
| **R7** | token 单源化 | Top10-6 | 最小方案：CMake 自定义 target 跑 python 脚本比对 DESIGN.md 表 ↔ Theme.hpp 常量 ↔ smoke 断言三份文本（不一致即 FAIL）；进阶：`.inl` 生成 | S-M | 脚本维护成本 | 契约漂移 CI 可见；为 (h) nfui 同步铺路 |
| **R8** | 头防火墙 | Top10-7 副作用 | Host 的两个 presenter 改 `std::unique_ptr` 前置声明成员（析构在 cpp 定义）；Host.hpp 摘掉 DCompPresenter/HwndPresenter/D2DRenderer 具体头 | S-M | 唯一所有者语义要保持（现注释已写明顺序约束） | 全仓增量编译收益；Host.hpp 变薄 |
| **R9** | 版本化与符号 | 无 tag/版本 | 打 `v0.1.0` tag；CMake 写入 version.h（`FLUX_VERSION` 宏）；Release preset 显式 `/Zi`+pdb 输出确认 | S | 无 | 下游可引用；崩溃栈可读 |
| **R10** | 仓库卫生 | 混行尾 + 根目录脚本 | 加 `.gitattributes`（`* text=auto` 或统一 LF + `*.ps1/.png` 标 binary）；35 个 ps1 + .visual-test 归档到 `tools/visual-scratch/` 或删除 | S | renormalize 会产生一次性大 diff（单独 commit） | 假 diff 消失；根目录可读 |
| **R11** | 静态分析/消毒 CI | 无 /analyze、无 ASan | CI 加一 job：`x64-debug` + `/analyze` 或 clang-tidy（先只开 bugprone/core）；再加 `/fsanitize=address` preset 跑 smoke | M | 误报治理成本 | 捕获 COM/迭代器/UB 类问题 |
| **R12** | 拖拽/滚动去重 + 锚定 API | Top10-9 | `apply_drag(Element*, lx, ly)`、`scrollbar_metrics(st)` 提取；`anchored_popup` 从 Menus.cpp 匿名命名空间提升为 flux::ui 公共 helper（Host::build_tooltip 改用） | S | 无 | 消除三份漂移源 |

**先做什么**：R2（S，修已确认 bug）→ R5+R6（S，语义与断言兜底）→ R7+R9+R10（S，契约与基建）→ R1（M，扩展性根因）→ R4（M，测试网）→ R3（L，最后拆 God file，因为前三步已把风险面缩小）。R4 与 R1 可并行。

---

## 10. 被推翻的假设 / 已验证无问题

1. **「hover/press/focus 变更必须 force_full_dirty_」不成立**（现行代码）：交互态已进 DamageTracker diff，且有 smoke 回归测试（smoke.cpp:896-940"regression for the old behaviour"）。CLAUDE.md 该条目过时（本报告漂移 #1）。
2. **2026-08-11 评审 P0/P1 清单全部复核为已修复**，不重复报：create 时 `set_dpi(GetDpiForWindow)`（Host.cpp:318）✓；世界变换/物理 clip（D2DRenderer.cpp:141-175）✓；设备丢失重建含 dcomp 设备链（DCompPresenter.cpp:172-189）✓；renderer_ 声明顺序 + 注释（Host.hpp:281-284）✓；lbutton_up 先存 path、重绘后 re-resolve（Host.cpp:925-941）✓；EndDraw/双 Commit（DCompPresenter.cpp:243-266）✓；`registered_class_` 死状态已删除（全仓 grep 0 命中）✓；rect_union 重复已删（仅存 Damage.cpp）✓；smoke token 断言 2/21 → **30/30**（smoke.cpp:61-92）✓。
3. **「.gitignore 可能没覆盖 scratch」不成立**：`/*.ps1`、`.qoder/`、`.visual-test/`、`out/` 均实测命中。
4. **「include 图可能有环」不成立**：Tarjan SCC 为空；AGENTS.md 分层单向声称属实。
5. **「smoke 纯 CPU」基本成立但有注脚**：measure_text 依赖 DWrite 共享工厂（无需窗口站；CI 历史上一直跑过）。
6. **「Builders/组件清单文档失真」大部分不成立**：CLAUDE.md builders-in-Host.cpp、AGENTS.md 模块表、DESIGN.md↔Theme.hpp 均逐项核对一致（漂移仅 §7.2 所列 11 条）。
7. **「paint_props_equal 当前有漏字段」未发现**：逐字段比对 Props 与 paint_props_equal/布局派生规则，当前 paint 相关字段全覆盖；仅 `padding` 在"固定尺寸叶子 + 文本偏移"边缘场景理论上漏重绘（text_input 光标/文字 x 随 padding.left 变而 w/h 不变时）——Likely、低危、触发面窄。

## 11. 未验证项与局限

- **未构建、未运行任何目标/测试/样例**（授权限制）：所有"测试通过数""CI 行为""运行时性能"结论为静态推断；build 产物目录未读。
- 圈复杂度/函数长度为正则启发式：纯数据 switch（build_icon cx~19）与链式构造会虚高；builders 的 Props 拷贝链不计复杂度。
- radar paint_id 陈旧重绘、异常 terminate、padding 边缘漏重绘均为**代码级确认、未活体复现**（无运行环境）。
- CI runner 上 DComp/HwndPresenter/WARP 的实际可用性未实验（R4 的可行性需先验证）。
- nfui 仓库不可访问：token 共享现状（第 4 副本是否同步）只能按本仓库文档推断。
- Release .pdb 是否生成未验证（未读 out/ 产物）。
- .visual-test 工具链脚本内容未逐个审读（黑名单约束只读豁免了内容审读的成本收益）；其对"能否进 CI"的评估基于文件名与 CLAUDE.md 描述。
- 行数/断言计数是 2026-10-06 HEAD `8eb46e4` 快照；工作区 clean，无未提交改动。
