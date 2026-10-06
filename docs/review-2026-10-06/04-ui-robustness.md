# WinFlux UI 稳定性审查报告 —— 崩溃风险专项（04-ui-robustness）

- 审查对象：`WinFluxGallery` / `WinFluxDashboard` 及其下的框架 UI 层（Host 消息分发、overlay 系统、drag_handle/scroll_view、图表交互、flux::ui 组件）
- 仓库：`C:\tjf\github\WinFlux`（master @ 8eb46e4，工作区干净）
- 样例二进制：`out/build/x64-debug/Debug/`（2026-08-15 21:47 构建）、`out/build/x64-release/Release/`（21:49 构建），对应 round 8 完成后的 master
- 审查方式：全量代码通读（Host.cpp 1615 行、两个 Presenter、D2DRenderer、Damage、FlexLayout、全部组件、两个样例全文）+ 近 8 提交 diff 逐条复核 + 临时目录最小复现（debug/release 双构建，链接仓库预编译 `flux.lib`，未改动仓库任何文件）+ 样例活体验证（均以 `PostMessage(WM_CLOSE)` / `PostThreadMessage(WM_QUIT)` 正常收尾）
- 测试基线：`WinFluxSmokeTest`（全部 PASS）、`WinFluxHostLifecycleTest`（全部 PASS，真实 HWND 会话）

> **关于审查期间用户桌面出现的 "Debug Assertion Failed (vector subscript out of range)" 弹框**：来源是本审查启动的实验（repro 探针的两次运行，以及 `WinFluxDashboard.exe 1` 即 Servers 页——该页每帧触发同一断言）。这恰好构成发现 R-1 的活体证据；探针随后已改为 `_CrtSetReportMode→stderr` + `SEM_NOGPFAULTERRORBOX`，不再弹桌面框，所有由我启动的进程已清零。

---

## 0. 结论：2026-08-11 历史问题清单逐条核实

| 历史项（当时定级） | 现状 | 证据（file:line，均已亲自读取） |
|---|---|---|
| P1 `handle_lbutton_up` 悬垂指针 + `resolve(fired)` | **部分修复**：`resolve(fired)` 已移到重绘之后（Host.cpp:927-935），重绘本身不再使指针失效；**残余风险 R-2**：`on_click` 执行期间嵌套重绘自毁闭包（见下） | `src/core/Host.cpp:902-943` |
| P1 `handle_lbutton_down` 背景点击不清 `focus_path_` / 键盘激活无重绘 | **已修复**：背景点击清 `focused_input_path_`（:856-859）；无交互祖先时 `focus_path_` 收空（:887-899）；Space/Enter 走 handler 驱动重绘 | `src/core/Host.cpp:832-899, 1088-1093` |
| P1 Host 成员析构顺序（renderer 先于 presenter 死） | **已修复**：`renderer_` 声明在最后、最先析构，与 `D2DRenderer.hpp` 不变量一致且有注释 | `include/flux/Host.hpp:191-284` |
| P1 DComp 设备丢失恢复半成品（`rebuild_d2d` 不重建 IDCompositionDevice） | **已修复**：`rebuild_d2d` 完整拆除 surface/visual/target/dcomp 设备/D2D/DXGI/D3D 后全量重建；`end_frame` 同时检查 `context_->EndDraw` 与 `surface_->EndDraw` 的 HRESULT | `src/render/DCompPresenter.cpp:172-189, 243-266` |
| P2 EndDraw HRESULT 丢弃 | **已修复**（见上；HwndPresenter 也检查 `D2DERR_RECREATE_TARGET`） | `src/render/HwndPresenter.cpp:73-79` |
| P2 悬垂 visual / double Commit / `to_physical_rect` 截断 | **已修复**：resize 失败 `SetContent(nullptr)`；`rebuilt_this_frame_` 抑制二次 Commit；物理矩形四边夹取到表面内 | `DCompPresenter.cpp:10-24, 153-166, 265` |
| P2 `registered_class_` 死状态 | **部分改善**：容忍 `ERROR_CLASS_ALREADY_EXISTS`；类从不注销（每进程一次注册，良性泄漏） | `Host.cpp:284-293` |
| P2 `prev_tree_` 在无绘制时也写（最小化状态失步） | **已修复**：`dirty_rects.empty()` 提前返回只推进 `last_painted_generation_`，不动 diff 基线 | `Host.cpp:1268-1283`（对照 :1302-1308） |
| 主审抽查：启动 `set_dpi` / 变换重写（原 P0） | **均已修复**（`Host::create` 即 `set_dpi(GetDpiForWindow)`；世界变换带脏标记缓存） | `Host.cpp:318`；`D2DRenderer.cpp:129-158` |
| 主审疑问：`force_full_dirty_` 仅 2 处（resize/DPI），交互态失效路径 | **不构成缺陷**：设计已重构——hover/pressed/focus 以 `prev/next InteractionState` 喂给 `DamageTracker::compute`，overlay 交互路径单独 resolve 出矩形，不可解析时回退全量；两处 `force_full_dirty_` 是有意保留 | `Damage.cpp:159-224`；`Host.cpp:1211-1263` |
| 已证伪：HwndPresenter::resize "自赋值" | 维持证伪（实为参数→成员赋值） | `HwndPresenter.cpp:48-57` |
| 已证伪：`paint_props_equal` 字段完备性 | 维持证伪（现 30+ 字段，round 7 补了 `input_error`） | `Damage.cpp:99-129` |

---

## 1. 摘要：按崩溃可能性排序的 Top 风险

| # | 风险 | 触发难度 | 置信度 |
|---|---|---|---|
| R-1 | `ui::data_table` 负排序列 → `vector` 下标越界读（Dashboard **Servers 页每帧执行**；debug 构建 CRT 断言弹框/abort，release 构建 OOB 读可致 SIGSEGV——已复现） | 打开 Dashboard 左侧 Servers 页即触发 | **Confirmed**（代码 + debug 断言实录 + release 段错误实录） |
| R-2 | 树持有执行中的回调闭包：handler 内 `State<T>::set` → 嵌套 `request_render` → `prev_tree_` 释放旧树 → 正在执行的 `std::function` 自毁 → 后续捕获读取 UAF（debug 构建在精确断点段错误——已复现；release 被寄存器缓存掩盖但 UB 仍在） | 样例内有多条真实触发链（表头换列排序、日历翻月、date_picker） | **Confirmed**（机制 + 最小复现；样例触发链为代码证实） |
| R-3 | 退出路径断裂：全仓无 `PostQuitMessage`，`WM_CLOSE` 毁窗后 `GetMessageW` 永久阻塞 → 进程僵尸。两个样例均实测复现 | 用户关窗即触发（必现） | **Confirmed**（grep 全仓 + 两次活体复现） |
| R-4 | `overlay_modal` 的 `close_it` 对已 reset 的 `mh.current` 解引用（无 `has_value()` 检查）→ 空 `optional` 上读已析构 `std::function` → UB/野调用 | 模态 Esc 关一次（无重绘滞留）后再按 Esc；或点 scrim 关闭后再 Esc | **Confirmed**（代码无歧义；Gallery 现场 on_close 为空 lambda、字节良性故未崩，库用户传入真实 on_close 即 heap UAF） |
| R-5 | Esc / 点外关闭浮层后**不请求重绘**：`handle_key_down` Esc 分支调 `on_click` 后直接 `return`，且 `handle_lbutton_up` 的重绘发生在关闭突变之前 → 浮层逻辑已关、画面滞留；陈旧 `frame_overlays_` 继续参与命中 | 每次用 Esc 关 select/菜单/命令面板/模态 | **Confirmed**（代码路径无歧义；hover 动画 120ms 定时器常掩盖，故视觉测试未暴露） |
| R-6 | 全部 Host 消息入口 `noexcept` + `/EHsc`：任何异常（`bad_alloc`、app 回调抛出）在进入 wndproc 的 `catch(...)` 前即 `std::terminate`——`catch(...)` 是死代码 | 低（当前 src/samples 无 throw 点） | **Confirmed**（语义推演，未注入异常） |
| R-7 | `begin_frame` 失败 / 设备丢失恢复帧：`paint_frame` 仍推进 `last_painted_generation_` 与 `prev_tree_` → 恢复后该帧内容不再补画（陈旧像素直至下次失效） | 设备丢失 / DComp 表面 BeginDraw 失败 | **Likely**（代码路径明确，未做 TDR 注入） |

---

## 2. 发现清单

### R-1（P0）`table_sorted` 负排序列：`vector` 下标越界 —— 用户所见断言框的本体

- **位置**：`src/components/Data.cpp:29-36`（`sort_rows` 比较器），触发表达式在 :33-34：
  ```cpp
  if (col >= static_cast<int>(a.size()) || col >= static_cast<int>(b.size()))
      return false;                      // 只挡了上界，负值直接穿透
  return asc ? a[static_cast<size_t>(col)] < b[static_cast<size_t>(col)] ...
  ```
  调用链：`ui::data_table`（Data.cpp:51 无条件排序）← `samples/Dashboard/Dashboard.cpp:139-141` **硬编码 `sort_col = -1`**（page_servers，"默认不排序"的写法）。
- **触发**：任何以负 `sort_col` 调用 `ui::data_table` 的应用。Dashboard 打开 Servers 页后**每帧**执行（root builder 每帧重建）。
- **后果（分层实测）**：
  - Debug 构建：MSVC STL `vector::operator[]` 边界检查触发 `Debug Assertion Failed: vector subscript out of range`（`vector(1941)`，见 `repro_stderr.log`）。弹框模态阻塞消息泵；点 Abort → `abort()`（退出码 3）。**用户桌面看到的断言框即此**（我的探针与 Dashboard Servers 页共同产生）。CI 不跑样例所以从未暴露。
  - Release 构建：无边界检查，`a[size_t(-1)]` 折叠为"行缓冲区前 32 字节"的伪 `wstring`，其 `_Myres/_Mysize/_Bx` 皆为堆上无关字节；若伪串被判为堆模式则解引用野指针。**实测**：`repro_sort.exe`（release，链接 release `flux.lib`）在 20 万次带堆扰动迭代内 **SIGSEGV（exit 139）**。当前 release Dashboard Servers 页之所以"能跑"，是稳态分配模式下缓冲区前字节恰好良性——任何堆布局变化（先逛别的页、改窗口尺寸、行内容变长）都可能翻转。
- **修复草案**：
  ```cpp
  if (col < 0 || col >= (int)a.size() || col >= (int)b.size()) return false;
  ```
  并在 `data_table` 入口夹取/断言 `sort_col`；Dashboard page_servers 改传 `0` 或给 `data_table` 增加"列 < 0 表示不排序则跳过 sort_rows"的显式分支。补 smoke 回归：`table_sorted(rows, -1, …)` 不越界（可断言排序后行集合不变）。
- **版本归属**：守卫缺口随 Round 4 `Data.cpp` 引入；`-1` 调用随 Round 5 `187d2d6`（2026-08-15）Dashboard 立样起存在——即 **Round 5 以来所有 Dashboard 构建均含**（本次测试的 debug/release exe 分别构建于 08-15 21:47 / 21:49，哈希前缀 `7d7c7dcd…` / `822827d8…`）。
- **置信度**：Confirmed（debug 断言 + release 段错误双实录）。

### R-2（P1）执行中的回调闭包被嵌套重绘自毁 —— 系统性 UAF 模式

- **位置（根因两处）**：
  - `src/core/Host.cpp:1154` `tree_ = root_builder_();` 与 `:1304` `prev_tree_ = tree_;`——嵌套 `paint_frame` 结束时释放旧树（含所有 `Props` 回调闭包）。
  - 分发点（在树上 `std::function` **属主仍为可能被重建的树**的情况下直接调用）：`Host.cpp:940`（`handle_lbutton_up` → `e->props.on_click()`，注释 :924-926 自称"路径保持点击目标稳定"，但稳定的是路径、不是闭包存储）、`:992`（Esc → overlay 根 `on_click`）、`:1005`（VK_BACK → `on_text_change`）、`:1036`/`:1052`（`on_key`）、`:1091`（Space/Enter）、`:1107`（WM_CHAR）、`:407`（`on_scroll`）、`:744`/`:757`（拖拽中 `on_value_change`）、`:869`/`:882`（按下时 `on_value_change`）。
- **机制（已在 debug 构建逐步复现）**：`on_click` lambda 执行 → `app.xxx.set()` → `State::set` 的 notify → `Host::request_render`（`painting_` 已复位，直入）→ `paint_frame` 全管线 → :1304 释放旧树 → **正在执行的闭包被析构、堆块被填 0xDD（debug CRT）** → lambda 后续语句从已释放块重读捕获 → 野指针解引用。
- **实测**：`repro_uaf.exe`（debug）输出
  ```
  [h] handler entered
  [h] after a.set — nested paint_frame done, owning closure freed
  <SIGSEGV (exit 139)>          ← ++app.guard_reads，a.set 后第一次捕获读取
  ```
  release 构建（`repro_uaf_rel.exe`）同序列**存活**（`&app` 被缓存在寄存器），exit 0——UB 仍在，只是被优化掩盖；嵌套管线的数百次分配若复用该块即可致崩。
- **样例内真实触发链**（均亲自读到）：
  - `Gallery.cpp:1878-1885` 表头换列排序：`else` 分支 `app.table_sort_col.set(col); app.table_sort_asc.set(true);` —— 第二次 `set` 前捕获已被自毁（Components 页表头 / Dashboard Overview 表头同理）；
  - `Gallery.cpp:551-555` date_picker：三连 `set`；
  - `Gallery.cpp:2035-2042` 日历翻月：`cal_month.set` 后再读 `app.cal_year`。
  - 单 `set` 且结尾无捕获读取的 handler（大多数按钮）恰好空尾巴，故日常使用存活——这是"偶发、换机器/换编译选项即翻脸"的典型埋雷。
- **修复草案**（统一封装，改动小）：
  ```cpp
  // Host 私有 helper：先把回调拷出到栈上再调用，树重建不再影响执行体
  template <class F> static void invoke_owned(const std::function<F>& fn) { auto c = fn; if (c) c(); }
  ```
  将上述 11 个分发点改为 `auto fn = e->props.on_click; if (fn) fn();` 式调用；`slider/drag_handle` 的 `on_value_change`、`on_text_change`、`on_key`、`on_scroll` 同理。更彻底：`Props` 回调改存 `std::shared_ptr<std::function<…>>`。
- **置信度**：Confirmed（最小复现段错误 + 机制逐步打印定位；样例触发链为代码证实，未在样例上注入点击）。

### R-3（P0）退出路径断裂：关窗后进程僵尸

- **位置**：`src/platform/AppHost.cpp:21-28` 消息循环；**全仓 grep 无 `PostQuitMessage`、无 `WM_DESTROY` 处理**（src/include/samples/tests 均无）。
- **触发**：用户点窗口 × 或 `WM_CLOSE` → `DefWindowProc` → `DestroyWindow`（窗口确实销毁）→ 无 `WM_QUIT` → `GetMessageW` 永久阻塞 → `Host::run()` 永不返回 → `wWinMain` 永不返回 → `App`/`Host` 析构从不执行（COM 资源靠进程 teardown）。
- **实测**：Dashboard（Overview 页，无断言干扰）与 Gallery 各一次：`PostMessage(WM_CLOSE)` 后 `WaitForExit(8000)==false`、`IsWindow==false`、进程存活——**僵尸确认**。需 `PostThreadMessage(WM_QUIT)` 才能收尸（我已如此清理我启动的实例）。
- **附带影响**：任务管理器残留进程、二次启动同名类注册冲突路径（被 `ERROR_CLASS_ALREADY_EXISTS` 容忍）、CI/自动化挂起。审查初期一次"进程已退出"的假象实为 R-1 断言 abort，不是干净退出。
- **修复草案**：`host_wndproc` 增加
  ```cpp
  case WM_DESTROY: PostQuitMessage(0); return 0;
  ```
  并在 `host_lifecycle.cpp` 加"create → PostMessage(WM_CLOSE) → 等待进程/线程退出（带超时）"探针（现测试手动析构 Host，恰好绕过了该缺陷——这也是它没被发现的原因）。
- **置信度**：Confirmed（双样例活体复现）。

### R-4（P1）`overlay_modal`：空 `optional` 解引用（双 Esc）

- **位置**：`src/components/Overlays.cpp:268-271`
  ```cpp
  auto close_it = [&mh] {
      if (mh.current->on_close) mh.current->on_close();   // 无 has_value() 检查
      mh.current.reset();
  };
  ```
  对照 `modal_close`（:256-258）有 `has_value()` 检查——同文件内自相矛盾。
- **触发**：Esc 关闭模态（此时无重绘，R-5）→ `frame_overlays_` 仍持有模态根（上一帧快照）→ 再按 Esc → `handle_key_down` 遍历陈旧 `frame_overlays_`（Host.cpp:989-995）→ 再次调用 `close_it` → `mh.current` 已 disengaged → 读已析构的 `std::function` 存储。scrim 点击关闭后再 Esc 同理。
- **后果**：Gallery 现场 on_close 为空 lambda（inline 存储、平凡析构），读到的字节未被破坏，实测未崩（未注入复现，避免再打扰桌面）；**库用户传入带堆捕获的真实 on_close 时即 heap UAF / 野调用**。
- **修复草案**：`if (mh.current && mh.current->on_close) mh.current->on_close(); mh.current.reset();` + 关闭后 `host.request_render()`（与 R-5 一并修）。
- **置信度**：Confirmed（代码）；崩溃可达性对 Gallery 为 Likely-benign、对一般库用户为 Likely。

### R-5（P2）浮层 Esc / 点外关闭后不重绘（陈旧浮层滞留 + R-4 的使能条件）

- **位置**：`Host.cpp:988-996`（Esc 分支 `top->props.on_click(); return;` —— 之后无 `request_render`）；`Overlays.cpp` `close_it`、`Menus.cpp:90`（`[&sel]{ sel.open = false; }`）、`:128`（menu）均不触发渲染；`Host.cpp:932` 的重绘又发生在 `on_click` **之前**（关闭突变落在最后一帧之后）。`ui::select` 触发按钮的 `on_open`（Gallery.cpp:1907-1914 只改 `select_host.open`，无渲染请求）同病——**打开**方向也依赖 hover 动画定时器兜底。
- **后果**：Esc/慢速点击（停顿 > `motion_fast_ms`=120ms 后点击）后下拉/菜单/模态在画面上滞留，直到任一无关重绘；期间命中测试仍命中陈旧浮层（点击被已关闭的浮层吃掉、双 Esc 触发 R-4）。快速操作被 hover 动画 120ms 窗口内的 WM_TIMER 帧掩盖——解释了历史视觉验证（快速脚本化点击）为何全过。
- **修复草案**：`handle_key_down` Esc 分支与 `handle_lbutton_up` 的 `on_click` 之后统一补一次 `request_render()`（幂等、有 generation 短路，代价极小）；或约定所有 overlay 关闭回调末尾自请求渲染（不如框架侧兜底可靠）。
- **置信度**：Confirmed（代码路径无歧义；未做像素级活体复现）。

### R-6（P2）`noexcept` 全覆盖使 wndproc 的 `catch(...)` 成为死代码

- **位置**：`host_wndproc` 的 try/catch（Host.cpp:193-265）包围的每个 `host->handle_*` / `paint_frame` 均声明 `noexcept`（Host.hpp:155-172）。`/EHsc` 下 `noexcept` 函数内抛出 C++ 异常直接 `std::terminate`，不会展开到 wndproc 的 catch。
- **后果**：builder / `Props::paint` / app 回调抛出的任何异常（含 `std::bad_alloc`）= 进程必死，且现有"安全网"给人错误的安全感。当前 src/samples/tests 无显式 throw（grep 证实），风险仅在库边界。
- **修复草案**：二选一：(a) 去掉消息入口的 `noexcept`（保留 wndproc catch，允许吞掉并跳帧）；(b) 保留 `noexcept` 但删除误导性 catch 并在文档声明"回调不得抛出"。倾向 (a) + catch 内 `bump_generation()` 保证下帧重试。
- **置信度**：Confirmed（语义；未注入异常实证）。

### R-7（P2）绘制失败帧仍推进基线 → 恢复后不补画

- **位置**：`Host.cpp:1290-1309`：`renderer_.begin` 拿不到 RT（DComp `begin_frame` 失败 / 设备丢失重建失败）时全帧静默空转，但 `:1309` 仍执行 `last_painted_generation_ = dirty_generation_`、`:1304` 仍推进 `prev_tree_`。`DCompPresenter::begin_frame` 失败路径（DCompPresenter.cpp:212-217）返回空 `SurfaceFrame`。
- **后果**：设备恢复后的第一帧 generation 已"消费"，若内容与失败前基线一致则 diff 为空 → 永不补画（陈旧像素直至下次交互）。
- **修复草案**：`renderer_.begin` 返回 bool（或 `frame_rt_==nullptr` 可查询），失败帧不推进 `last_painted_generation_`/`prev_tree_`。
- **置信度**：Likely（代码路径确凿；未做 TDR/设备移除注入）。

### R-8（P3）`splitter(vertical=true)` 失效：`drag_vertical` 被随后的 props 赋值覆盖

- **位置**：`src/components/Layout.cpp:24-27`：先 `handle->props.drag_vertical = vertical;` 再 `handle->props = std::move(p);`（`p` 未设 `drag_vertical`，默认 false 覆盖之）。垂直分隔条永远按水平轴计算（`Host.cpp:753-758` 读到的恒为 false）。功能缺陷非崩溃。修复：移动赋值后再设 `drag_vertical`。
- **置信度**：Confirmed（代码；未视觉复现）。

### R-9（P3）`button(text, ip.on_click, std::move(ip))` 参数求值顺序不定

- **位置**：`Overlays.cpp:57`（`menu_item_button`）、`Data.cpp:79`（表头按钮）等。`std::move(ip)` 与 `ip.on_click` 的拷贝无先后保证：若移动先发生，拷出的是 moved-from（空）`std::function` → 静默死按钮/死菜单项。当前 MSVC 求值顺序下恰好存活。修复：先 `ClickHandler c = std::move(ip.on_click);` 再传（DataViz.cpp:188-191 的 `nav_btn` 已示范正确写法并留有注释）。非崩溃，属埋雷。
- **置信度**：Confirmed（机制；当前产物未表现）。

### R-10～R-14（P3，加固项）

- **R-10** 递归无深度看门狗：`render_element` / `diff_subtree` / `measure_impl` / `layout_node` / `layout_scroll_views` 深度=树深。样例树深 ~15，安全；库层面无上限（病态深树可栈溢出）。Speculative。
- **R-11** `days_in_month(year, month)`（Pickers.cpp:11-18）不对 month 夹取，`days[month]` 越界读静态数组；当前调用方（date_picker / calendar，经 Gallery 的月份回绕算术）都传 0..11。Speculative（防御性夹取一行）。
- **R-12** `measure_text` 缓存键为 (format, FNV(text), wrap) 64 位哈希（D2DRenderer.cpp:393-407）：碰撞 → 错误测量（视觉）。Speculative。
- **R-13** `RegisterClassExW` 从不 `UnregisterClass`（每进程一次，良性）。信息级。
- **R-14** `handle_key_down` 的 `collect_focusable(*frame_overlays_[oi], …)`（Host.cpp:1064-1066）与 `hit_test_all`（:652-653）不解引用判空——不变量目前由 `Host::overlay()` 的 `root != nullptr` 过滤（:334）维持，但 `update_tooltip` 等新写入点若破坏即崩。建议统一判空。Likely（防御缺口，当前无违反者）。

---

## 3. 近 8 个提交逐条复核

| 提交 | 内容 | 复核结论 |
|---|---|---|
| `8eb46e4` | docs: DIRECT_RENDERING_UI_PLAN | 纯文档，无代码。无缺陷。 |
| `24461dc` | CI: pin windows-2022 | 无产品代码。注：CI 全链路在无窗口站跳过生命周期目标（见下 0dc0d77 行），R-3 类缺陷无法被 CI 发现。 |
| `4b9f501` | CI: MSVC dev env 初始化 | 同上，无产品代码。 |
| `0dc0d77` | Round 8: overlay scroll + chrome helpers + CI | **未发现崩溃级缺陷**。`scrolls_ = std::move(next_scrolls)` 移到 overlay 布局 pass 之后（Host.cpp:1193-1201）是正确修复——若提前执行，sentinel 键条目会落入 moved-from map（计划文档记载的坑确已修对）。`host_lifecycle.cpp` 的 `WINFLUX_SKIP_GUI_TESTS` 早退（:33-38）使 CI 上生命周期目标整体跳过——**掩盖面**而非缺陷本身，但 R-3 正属其盲区。 |
| `f3e3b49` / `8298f76` / `e6d34af` | Round 7/8 计划文档 | 无代码。 |
| `1359aae` | Round 7: chart hover + validation + radar/scatter | **未发现崩溃级缺陷**。`Props::paint` 签名扩为 hover 上下文（render_element :1425-1429 传参安全）；`handle_mouse_move` 的 paint 元素随动重绘（:778-784）有 `resolve` 判空守卫、`break` 限一次；`input_error` 已同步进 `paint_props_equal`（Damage.cpp:120）。 |

---

## 4. 系统性加固方案

1. **回调分发统一封装（治 R-2/R-4/R-9 一类）**：Host 内建
   ```cpp
   template <class Sig> struct ScopedInvoke {  // 拷贝到栈上再调
       template <class F> static void call(const std::function<F>& fn, auto&&... a) {
           auto local = fn; if (local) local(std::forward<decltype(a)>(a)...);
       }
   };
   ```
   11 个分发点全部切换；CI grep 闸：`grep -n "props\.on_click()" src/core` 仅允许出现在封装内。
2. **路径失效降级**：`resolve` 已对越界返回 nullptr（保持），配套纪律：所有 `resolve` 结果判空后才解引用（现有代码已基本做到，补 `collect_focusable`/`hit_test_all` 两处判空）。悬停/焦点路径失配只降级为空高亮，不抛不崩——现状即如此，固化成断言。
3. **重绘触发与消息入口收口**：`Host::dispatch_after_handler()`——任何 handler 分发（click/key/char/scroll/value）结束后统一 `request_render()`；配合 generation 短路零成本（治 R-5，缓解 R-4 时窗）。
4. **退出与生命周期**：`WM_DESTROY → PostQuitMessage(0)`；`host_lifecycle` 增加"关窗后线程退出（超时 5s）"探针；CI 对无窗口站保留该探针的进程级变体（CreateProcess + WM_CLOSE + WaitForExit，不依赖窗口站绘制）。
5. **绘制失败传播**：`IPresenter::begin_frame` 失败 → `paint_frame` 跳过基线推进（R-7）；`end_frame` HRESULT 已检，补 `Host` 侧日志（`OutputDebugString`）。
6. **异常策略**：消息入口去 `noexcept`（或捕获点移入 `paint_frame` 内部的非 noexcept 包装），`catch (...) { bump_generation(); }`（R-6）。
7. **编译器/静态分析**：调试目标加 `/analyze`（warnings-as-errors 白名单起步）、`/GS`（默认开，确认未关）；链接 `/DYNAMICBASE /guard:cf`（CFG）；CI 增加 x64-debug ASan 作业（R-1/R-2 类即刻红）。保持既有 `/W4 /permissive-` 零警告闸。
8. **grep 闸（CI 脚本化）**：新增 `SetTimer` 必须伴随 `KillTimer` 或窗口属主论证；`table_sorted/data_table` 调用点 `sort_col` 必须 `>= 0`（或封装内夹取）；`button(x, p.on_click, std::move(p))` 模式直接禁（改用 DataViz.cpp:188 的先取后移写法）。
9. **回归测试思路（现有 expect()/check() 框架下）**：
   - 路径漂移：smoke 已覆盖 resolve 失配 → 补"树 shape 变化后 hover 路径越界仅清空高亮"的 host 级用例；
   - 设备丢失注入：`IPresenter` 加测试桩（`TestPresenter`，可编程 `begin_frame` 失败 N 次）验证基线不推进（R-7）；
   - 优雅关闭探针：见 4；
   - 排序防御：`table_sorted(rows, -1, true)` 行集合不变且无越界（R-1，debug STL 断言即免费探针）；
   - 闭包自毁：smoke 级模拟——builder 每帧新建 + `State::set` 嵌套调用的 host 用例（无头，用 `TestPresenter`），debug 下当前必崩、修复后必过。

---

## 5. 建议修复顺序

- **P0**：R-1（一行守卫 + Dashboard `-1` 改参 + 回归）；R-3（`WM_DESTROY→PostQuitMessage` + 退出探针）。两者改动极小、必现/高危。
- **P1**：R-2（11 个分发点 copy-invoke 封装）；R-4（`has_value` 检查）；R-5（handler 后统一 `request_render`）。
- **P2**：R-6（异常策略定案）；R-7（失败帧基线）；R-8（drag_vertical 覆盖）；R-9（参数顺序封装）。
- **P3**：R-10～R-14（看门狗/夹取/判空/注销等防御性加固）+ 第 4 节 grep 闸与 ASan/analyze CI。

---

## 6. 已检查且安全（避免重复排查）

- **路径/索引防御**：`Host::resolve`（含 `{-1, idx}` overlay 哨兵）全分支越界返回 nullptr；`DamageTracker::resolve_path` 同；`diff_subtree` 对不等长子数组按加/删处理；`hit_test_all` 的 scrim/透明根语义正确。
- **窗口/消息绑定**：`WM_NCCREATE` 绑 `hwnd_` 与 `GWLP_USERDATA`，`WM_NCDESTROY` 双清（Host.cpp:181-190, 256-259）；`WM_PAINT` 直调 `paint_frame`（窗口存活期内）；`WM_SIZE` 在 presenter 就绪前由 `renderer_.valid()` 拦截。
- **定时器**：唯一 `anim_timer_id_`（窗口属主，`DestroyWindow` 自动回收），`handle_timer` 在无需求时 `KillTimer`；无 `PostMessage`/`WM_APP` 堆负载（grep 全仓为零）。
- **模态泵**：无 `TrackPopupMenu`、无消息路径上的 `MessageBox`（仅 Gallery 创建失败、消息循环之前）；浮层全部为元素树内实现，无原生模态重入。
- **多线程**：无（grep `CreateThread/_beginthread/std::thread/std::async` 为零）——单线程模型成立，无跨线程 COM/设备问题。
- **COM**：未调 `CoInitialize`——D2D/DWrite/D3D/DComp 所用创建函数均不要求；所有创建点 HRESULT 已检查，失败回退链（DComp→Hwnd→失败退出）完整。
- **坐标/单位**：`to_physical_rect` 已夹取；`SurfaceFrame` 偏移以物理像素进入、渲染器组合变换正确（release 样例长时间运行像素正常佐证）。
- **文本/编码**：全 `wstring` 管线、`/utf-8`、无窄宽转换层；WM_CHAR 拒绝 <0x20 控制码，代理对以两码元自然追加；截断代理对交给 DWrite（测量/绘制宽容）。
- **样例成员序**：`App` 中 `host` 首成员（最后析构），builder/hook lambda 捕 `&app` 存活期覆盖 Host 生命期（若 R-3 修复后 `run()` 能正常返回，析构序亦正确）。
- **overlay 滚动（round 8 修复项）**：`scrolls_` 提交位置正确；`{-1, idx}` 路径进同一注册表；拖拽中树重建时 `resolve` 判空、`WM_CAPTURECHANGED` 清 `drag_scroll_`。
- **图表交互（round 7）**：`nearest_index/value_range/pie_slices/scatter_map` 均有界；`swprintf(buf,32,…"%.1f")` 有界截断安全；`draw_hover_crosshair` 的 `idx >= pts.size()` 有守卫；paint 闭包以 `shared_ptr` 持数据，跨帧安全。
- **测试基线**：SmokeTest（226 项）与 HostLifecycleTest（4 项，真实 HWND）全绿。

## 7. 被推翻的假设 / 已验证无问题

1. **假设"Gallery Motion 页 Fill/Drain 按钮双 `set` 会触发 R-2"** —— 证伪：`Animated<T>::set` 无 notify、不触发嵌套重绘（Animation.hpp:65-82）；只有 `State<T>::set` 走 `request_render`。
2. **假设"Dashboard Servers 页启动即崩"** —— 部分证伪：debug 下表现为 CRT 断言弹框（阻塞/abort，用户所见），release 下靠稳态堆字节存活；但堆扰动复现证明 release 同样可 SIGSEGV（R-1 维持 Confirmed）。
3. **假设"样例 WM_CLOSE 后正常退出"** —— 证伪：初次观察到的"退出"是 R-1 断言 abort；干净页实测两样例均僵尸（R-3）。
4. **假设"wndproc 的 `catch(...)` 能兜住管线异常"** —— 证伪：全部入口 `noexcept` + `/EHsc`，异常在到达 catch 前已 terminate（R-6）。
5. **主审线索"force_full_dirty_ 仅 2 处，交互态失效路径可疑"** —— 证伪为缺陷：交互态已并入 `DamageTracker::compute` 的 prev/next InteractionState（设计重构），2 处 force_full_dirty_ 为 resize/DPI 保留；hover-only 变化实测走部分重绘（smoke 亦有覆盖）。
6. **历史 P2 "`prev_tree_` 最小化失步""double Commit""悬垂 visual""EndDraw 丢弃"** —— 均验证已修复（见第 0 节表），不再报。

## 8. 未验证 / 局限

- 未做真实设备丢失注入（TDR / `DXGI_ERROR_DEVICE_REMOVED` 人为触发），R-7 为代码判读（Likely）。
- 未在样例上做真实鼠标点击链验证 R-2（无可靠的注入坐标来源；机制已由最小复现在同库上证实，样例触发链为代码证实）。
- R-4 未活体复现（Gallery 空 on_close 字节良性；为避免打扰用户桌面未注入双 Esc）。
- R-5 的画面滞留未做像素级取证（seed 模式的 60s toast 会持续请求帧、污染观察窗口）；机制为无歧义代码路径。
- 环境局限：单显示器、会话 DPI 100%；未覆盖多显示器 DPI 切换、WARP 软渲染、远程会话。未审查 `.qoder`、docs 计划文本（非代码）。
- 版本归属方法：R-1 引入点由 `git log -S/-L` 定位（守卫缺口随 Round 4 Data.cpp，`-1` 调用随 Round 5 `187d2d6`）；所测二进制哈希前缀与构建时间已记录（debug `7d7c7dcd…` 08-15 21:47 / release `822827d8…` 08-15 21:49），非按文件名推断。
- 复现材料：`%TEMP%\winflux-review-2026-10-06\`（repro_uaf.cpp / repro_sort.cpp、构建脚本、repro_stderr.log 断言实录）；仓库全程只读，未修改任何源码/构建配置/构建产物。

## 9. 审计操作合规记录

- 样例与探针进程全部经 `PostMessage(WM_CLOSE)` 或 `PostThreadMessage(WM_QUIT)`（僵尸态唯一优雅手段）收尾；`tasklist` 核实无 `WinFluxGallery` / `WinFluxDashboard` / `repro_*` 残留。
- 未运行仓库根目录任何 `*.ps1` 与 `.visual-test\` 脚本（仅读源码参考）；所用脚本（close_sample.ps1、wmclose.ps1、probe_exit.ps1、kill_zombie.ps1、build_*.cmd）均为本审查在临时目录自写。
- 仓库未做任何 git 写操作；构建产物未改动（新构建仅产生于临时目录）。
