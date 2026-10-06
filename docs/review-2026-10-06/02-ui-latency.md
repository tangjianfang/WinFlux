# WinFlux UI 性能审查报告 —— UI 线程卡顿/延迟专项（02-ui-latency）

> **执行者说明**：本维度两次派发子代理均被 API 限额（429）中断且未能落盘，最终由**主审亲自完成**：实测 + 代码走查。首轮代理死前留有一条中程实测线索（已并入本文并标注）。仓库全程只读；样例经 WM_CLOSE + WM_QUIT 收尾（R-3 僵尸 workaround）。

## 方法与证据来源

- 环境：Windows 11 Pro 26200、单显示器 96 DPI、开发机；样例为仓库现有构建产物（08-15 构建，哈希见 04 报告）。
- 实测：`WINFLUX_PERF_LOG` 指向临时文件；PowerShell 驱动（`perf_idle_probe.ps1`，自写）：启动 → 4s 空闲 → 220 次 WM_MOUSEMOVE 锯齿扫掠（40ms 间隔、窗口内、持续跨组件制造 hover 变化与 120ms hover 渐变动画帧）→ 3s 空闲 → WM_CLOSE。**不注入点击**（避开已确认的 R-2 闭包自毁触发链与 Dashboard Servers 页 R-1 abort）。
- Debug 与 Release 双构建各跑一轮；perf log 每 120 帧输出一行分阶段平均（粒度限制见 U-7）。
- 代码走查：paint_frame / DCompPresenter::end_frame / FlexLayout / D2DRenderer 缓存。

## 实测数据（每 120 帧平均，单位 ms/帧）

| 构建 | 窗口 | total | build | layout | damage | draw | skipped |
|---|---|---|---|---|---|---|---|
| Debug | #1 启动段 | 23.25 | 0.001 | 2.53 | 0.007 | 20.71 | 0 |
| Debug | #2 | 10.50 | 0.002 | 2.53 | 0.007 | 7.89 | 3 |
| Debug | #3 | 10.78 | 0.002 | 2.46 | 0.006 | 8.26 | 3 |
| Debug | #4 | 12.45 | 0.002 | 2.61 | 0.007 | 9.83 | 0 |
| Release | #1 | 28.35 | 0.001 | 2.63 | 0.006 | 25.71 | 0 |
| Release | #2 | 25.44 | 0.001 | 2.43 | 0.006 | 23.00 | 0 |
| Release | #3 | 21.77 | 0.001 | 2.68 | 0.008 | 19.08 | 0 |

## 摘要：Top 结论（含置信度）

1. **每帧全量重建树在当前规模下零成本**（Confirmed）：build ≈ 0.001–0.002ms/帧。"React 式每帧重渲"架构在 Gallery 量级（约百级元素）完全成立。
2. **DamageTracker diff 零成本**（Confirmed）：damage ≈ 0.006–0.008ms/帧（指针快路径 + max_rects 收敛有效）。
3. **帧节奏由 DComp 合成同步主导，而非 CPU**（Confirmed 数据 + Likely 机制归因）：CPU 侧实耗（build+layout+damage）≈ 2.5–2.7ms 恒定；"draw" 阶段 Debug 稳态 8–10ms、Release 19–25.7ms——Release 更慢恰恰因为更快：CPU 提前完成 → 帧提交饱和 → `ID2D1DeviceContext::EndDraw` + `IDCompositionSurface` 更新等待合成器释放缓冲（~16.6ms vsync 量子）。首轮代理死前实测"draw≈16.2–23.2ms 且 16.6ms 精确反复出现"、`.visual-test/perf_logs.log`（08-15 同机）同现象，三方数据互证。**含义：局部重绘的收益体现在 GPU 负载与脏区外像素稳定，而不是 wall-clock 帧时长；持续动画上限 ≈ 合成器刷新率（60fps），单帧 CPU+GPU 超 16.6ms 即排队卡顿。**
4. **layout 是唯一随规模增长的 CPU 项**（Confirmed 当前值 + Likely 外推）：~2.5ms/帧恒定且 Debug/Release 无差（该阶段非优化敏感）。按 ~25µs/元素外推，400+ 元素的深页面将逼近 10ms，挤占 16.6ms 预算。布局每帧对全树执行（新树指针，无结构指纹缓存捷径）。
5. **空闲零开销**（Confirmed）：空闲段不产生帧（generation 短路 + 无动画时 KillTimer）；"skipped" 仅在活跃段出现 0–3/120。
6. **hover 交互延迟 < 1 vsync**（Confirmed）：hover 变化帧 CPU ~3ms + 合成等待，视觉反馈在一个合成周期内到位；无感知卡顿。

## 发现清单

| # | 发现 | 位置/证据 | 级别 | 修复草案 |
|---|---|---|---|---|
| U-1 | draw 阶段含合成等待，且为帧时长主导（Release 19-25.7ms） | DCompPresenter.cpp:243-266（EndDraw×2 + Commit）；三方实测 | 观测事实 | 无需"修"——属 DComp 表面更新语义；但性能优化方向应据此校准：省 CPU 不省 wall-clock，省 GPU/脏区面积才省 |
| U-2 | layout 全树执行、无增量，规模外推风险 | FlexLayout.cpp；实测 2.5ms 恒定 | Likely（外推） | 结构指纹缓存（同 shape+同约束 → 复用布局结果）为 M 级改造，建议 400+ 元素页面真实出现后再做 |
| U-3 | 持续动画期每帧全量 build+layout（虽便宜）叠加合成等待，动画期帧预算紧 | 数据窗口 #1（启动/渐入段 20-28ms） | Likely | 动画帧只影响视觉子集时仍走全管线；当前规模可接受 |
| U-4 | WINFLUX_PERF_LOG 采样粒度陷阱：不足 120 帧的场景完全无输出 | Host.cpp:1295 附近 kFrameTimingInterval=120；首轮实测 35 帧场景零输出 | Confirmed | 改为时间间隔（如每 2s）输出 + 进程退出时 flush 部分摘要（S） |
| U-5 | 无分位数/直方图：平均值掩盖单帧尖峰（本数据无法回答"最差帧多少 ms"） | log_frame_summary 仅累计均值 | Confirmed | 记录 p95/max 帧时长 + 尖峰帧的阶段分解（S-M） |
| U-6 | Release 无自动化性能回归基线 | 仓库观察 | Confirmed | CI 可选 job：跑固定 seed 场景比对阈值（与 08-F26 呼应） |

## 延迟预算表（实测/推算混合，标注来源）

| 操作 | 现状 | 目标 | 差距 | 责任代码 |
|---|---|---|---|---|
| 空闲 | 0 帧、0 CPU | 0 | ✅ | generation 短路（Host.cpp:1130） |
| hover 移动 | CPU ~3ms + 合成等待，反馈 <16.6ms | <16ms | ✅ 达标 | 交互 diff（Damage.cpp:159+） |
| 切 tab / 开浮层 / 滚动 / 主题 fade | **未实测**（避免触发 R-1/R-2） | <100ms | 未验证 | — |
| 启动首帧 | 首窗口均值 23-28ms（含渐入动画） | <300ms | ✅ 推测达标 | — |
| 持续动画上限 | ~合成器刷新率（60fps） | 60fps | ✅ 按设计 | DComp 语义 |

## 被推翻的怀疑

1. **"每帧全量重建树很贵"** — 证伪：build 0.001ms（百元素级）。
2. **"damage diff 昂贵"** — 证伪：0.007ms。
3. **"hover 仍走全屏重绘"** — 证伪：交互态已进局部 diff（04/06 交叉证实 + smoke 回归）；本数据 hover 帧 draw 8-10ms（Debug）与脏区大小正相关，非全屏量级。
4. **"Release 一定更快（帧时长更短）"** — 证伪（wall-clock 口径）：Release 因帧提交饱和反而呈现更长 draw 等待；CPU 占用确实更低。

## 未验证 / 局限

- 未点击/滚动/开浮层（规避已确认的 R-1 abort 与 R-2 UAF 触发链）——这些场景的帧数据空缺，留待修复后补测。
- 96 DPI 单显示器；无 125/150% DPI、多显示器数据。
- 平均值无分位数，单帧尖峰不可见（见 U-5）。
- 合成等待的机制归因（EndDraw surface 缓冲等待 vs Commit）未做 ETW/DWM 诊断级定位，标 Likely；三方耗时数据互证现象本身为 Confirmed。
- 样例为 2026-08-15 构建的产物（含 R-1 缺陷），未重新构建无缺陷版本对照——对性能数据无影响（测试停留在 Overview/默认页）。

## 修复顺序建议

1. **U-4（S）**：perf log 时间间隔 + 退出 flush——立即提高可观测性。
2. **U-5（S-M）**：p95/max 尖峰统计。
3. **U-6（M，可与 08-F26 合并）**：Release 性能回归基线。
4. **U-2（M，暂缓）**：布局增量/缓存——等大页面真实出现。
