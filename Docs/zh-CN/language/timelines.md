---
title: "时间线"
description: "文件拥有的动画——轨道、关键帧、缓动名、事件轨道、循环方式，以及交给 Sequencer 编辑的动画的 external。"
---

<a id="timelines"></a>

## 时间线

`timeline` 块是文件拥有的一个动画：

```
timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Icon.RenderScale            : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
    Row/Title.RenderTranslation : 0.0 = (0, -40, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}

timeline Celebrate external
```

每条轨道一行：一条由节点 id 组成的路径（`Row/Title`）、它驱动的属性，以及关键帧 `time = value`，每个关键帧都可带一个可选的 `ease`，其名称取自补间库。上面的 `Row/Title` 从左侧 40 px 处滑入：在 `RenderTranslation` 中，Y 轴向右，X 轴表示深度，因此改用 `(-40, 0, 0)` 反而会让它向近处移动 40（[位置、尺寸和轴](nodes-and-values.md#positions-sizes-and-axes)）。没有路径的行驱动的是动画所在的那个控件。`@time -> Name` 是事件轨道上的一个关键帧。`loop` 取 `Once`、`Loop` 或 `PingPong`；`duration` 默认取最后一个关键帧。轨道可以驱动动画编辑器提供的任何东西——一个标了 `Interp` 的属性。`external` 命名一个位于资产中、由 Sequencer 编辑的动画。另见文档站点上的[时间线](https://gui.toolchain.64hz.cn/en/docs/dui/timelines)。

文件拥有的块也可以在动画编辑器里编辑：每次编辑都会写回块里——关键帧、ease、事件关键帧、duration，加一条轨道就写一行、删一条就删一行——自定义切线的关键帧会被拒绝。见[设计器写回](write-back.md)。
