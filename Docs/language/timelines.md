---
title: "Timelines"
description: "Animations the file owns — tracks, keys, ease names, the event track, loop modes, and external for an animation edited in Sequencer."
---

## Timelines

A `timeline` block is an animation the file owns:

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

One line per track: a path of node ids (`Row/Title`), the property it drives, and keys `time = value`, each with an
optional `ease` from the tween library's names. A line with no path drives the widget the animation lives on. `@time
-> Name` is a key on the event track. `loop` is `Once`, `Loop` or `PingPong`; `duration` defaults to the last key. A
track may drive what the animation editor offers -- a property marked `Interp`. `external` names an animation that
lives in the asset and is edited in Sequencer. See also [Timelines](https://gui.toolchain.64hz.cn/en/docs/dui/timelines) on the docs site.
