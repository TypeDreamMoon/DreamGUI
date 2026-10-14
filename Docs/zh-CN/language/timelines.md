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
    Row/Title.RenderTranslation : 0.0 = (-40, 0, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}

timeline Celebrate external
```

每条轨道一行：一条节点 id 组成的路径（`Row/Title`），它驱动的属性，以及关键帧 `time = value`，每个关键帧后面可以跟一个取自补间库名字的 `ease`。没有路径的行驱动的是动画所在的那个控件。`@时间 -> 名称` is a key on the event track. `loop` is `Once`, `Loop` or `PingPong`; `duration` 默认取最后一个关键帧。轨道可以驱动动画编辑器提供的任何东西——一个标了 `Interp` 的属性。`external` 命名一个住在资产里、由 Sequencer 编辑的动画。另见文档站点上的[时间线](https://gui.toolchain.64hz.cn/en/docs/dui/timelines)。
