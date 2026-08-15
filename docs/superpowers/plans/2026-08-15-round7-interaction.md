# Round 7 — 图表交互 + 表单校验 + 新图表 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Checkbox tracking.

**Goal:** 图表悬停交互（十字线 + 数值气泡 + 柱 hover tooltip）、表单校验模式（field 包装 + 错误态）、radar / scatter 图表；Gallery 组件页演示。

**Architecture:** `Props::paint` 回调扩展悬停上下文（hovered + 元素内局部鼠标坐标）——渲染期由 Host 注入，零新状态。图表几何继续走纯函数。表单校验 = `ui::field` 组合组件（caption/input/error 文案列）+ `Props.input_error` 输入框红边态；校验逻辑留在 app（on_change → State<error>）。

## Tasks

- [x] T1 paint 悬停上下文：`paint(D2DRenderer&, x, y, w, h, hovered, mx, my)`（mx/my 相对元素左上，未 hover 时为 -1）；render_element 注入；现有图表签名跟进。line/area_chart 十字线 + 最近点圆点 + 数值气泡；纯函数 `nearest_index(values, t)` + smoke
- [x] T2 bar_chart 柱 tooltip（元素级 hover 已有，柱列加 tooltip_text="Mon: 30"）；`radar_chart`（蛛网 + 多序列多边形）+ 纯函数 `radar_points` + smoke；`scatter_chart`（点映射）+ 纯函数 `scatter_map` + smoke
- [x] T3 `Props.input_error`（画笔字段→diff）输入框 danger 边框；`ui::field(pal, caption, input, error_text)` 列组件；`ui::validate_not_empty/email` 两个小校验器 + smoke
- [x] T4 Gallery 组件页扩：交互图表墙 + 表单 field 演示（实时校验）+ 视觉验证 + 双 preset 收口提交

## 验收
- [x] 折线图悬停出现十字线与数值
- [x] 空/非法输入显示红边 + 错误文案，输入后消失
- [x] radar/scatter 正确渲染且几何有测试
- [x] 双 preset 零警告全绿
