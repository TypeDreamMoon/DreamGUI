---
title: Animation
description: The three ways to animate — tweens from the DreamTween module (RenderOffsetTo, RenderScaleTo, RenderAngleTo and RenderTranslationTo for widgets inside panels, EDreamTweenEase, DreamTween.MaxStepSeconds), widget animations (UDreamWidgetAnimation and Sequencer), and .dui timelines.
---

There are three ways to make things move in DreamGUI, each for its own kind of job:

| Way | Good for | Where |
| --- | --- | --- |
| **Tweens** | Motion decided in code: entrances, hovers, a value changing | The `DreamTween` module, `UDreamTweener` |
| **Widget animations** | Designed animations, edited in Sequencer | `UDreamWidgetAnimation`, embedded in the class |
| **`.dui` timelines** | Widget animations the text owns, rebuilt on every compile | A `timeline` block in a `.dui` |

## Tweens

`DreamTween` is an independent, bottom-layer module. Every `*To` function returns a `UDreamTweener` you can chain on:
`SetEase`, `SetDelay`, `SetLoop` (`Once`, `Restart`, `Yoyo`, `Incremental`; a count of `-1` is infinite), `SetTimeScale`,
`SetAutoKill`, and the callbacks `OnStart`, `OnUpdate`, `OnCycleStart`, `OnCycleComplete`, `OnComplete` and `OnKill`; to
control it, `Pause`, `Resume`, `Restart`, `Goto`, `ForceComplete` and `Kill`.

Widgets, visuals and behaviours carry tweens of their own:

| On | Tweens |
| --- | --- |
| `UDreamWidget` | `AnchoredPositionTo`, `SizeDeltaTo`, `WidthTo`, `HeightTo`, `RenderOpacityTo`, `LocalPositionTo`, `LocalScaleTo`, `LocalRotatorTo`, `WorldPositionTo`, … and the render-transform tweens of the next section |
| `UDreamVisual` | `ColorTo`, `AlphaTo` |
| `UDreamTextPaintLibrary` | `PaintPhaseTo`, `PaintAngleTo`, `PlayShimmer` (see [Text](text.md)) |
| `UDreamTweenBPLibrary` | The general `FloatTo`, `Vector3To`, `ColorTo`, `MaterialScalarParameterTo`, `DelayCall`, `SpringFloat`, …, and `UMG_*` tweens for UMG widgets |

An easing is a **name** out of `EDreamTweenEase`, one word list for the whole plugin: `Linear`; `In`, `Out` and `InOut`
with `Quad`, `Cubic`, `Quart`, `Sine`, `Expo`, `Circ`, `Elastic`, `Back` and `Bounce`; and `CurveFloat`, which follows a
curve (`SetCurveFloat`). A widget tween defaults to `OutCubic` over 0.5 seconds.

A few rules that catch people:

- **Finished and killed tweens retire.** The handle reads invalid, and a UPROPERTY holding it is nulled at the next garbage
  collection; `Kill`, `ForceComplete` and `Goto` do nothing on a retired tween, and `Restart` only warns. To play one again
  after it finishes, `SetAutoKill(false)` before it starts, or `Restart` it from its own `OnComplete`, which keeps it running.
- Springs (`SpringFloat`) are held at rest by default; `SetTarget` wakes one.
- A tween whose owner is gone is dropped without callbacks.

### Pause and time dilation

Widget tweens follow the project settings: UI on the screen follows `bScreenSpaceUIAffectByGamePause` and
`bScreenSpaceUIAffectByTimeDilation` (both off by default), everything else `bWorldSpaceUIAffectByGamePause` and
`bWorldSpaceUIAffectByTimeDilation` (both on). Layout animations and the embedded UMG widget follow the same rule.

`DreamTween.MaxStepSeconds` (a console variable; `0`, the default, is off) caps how far one frame moves the tweens: a
frame longer than it — a screen loading its assets, the first draw of new text — moves the tweens the world ticks by the cap
only. An entrance started just before such a hitch then plays on from where it was instead of appearing at its end. Manual
ticks are never capped.

## Widgets inside panels: animate the render transform

A panel writes its children's anchored position and size back on its next pass, so `AnchoredPositionTo` or
`LocalPositionTo` on a widget a panel places only takes turns with the layout. What should move is **what is drawn**, not
the layout:

| Tween | Moves |
| --- | --- |
| `RenderTranslationTo(FVector)` | Translation in local space: X depth, Y right, Z up |
| `RenderOffsetTo(FVector2D)` | The same move on the canvas plane (x right, y up), depth kept |
| `RenderScaleTo(FVector)` | Scale about `RenderTransformPivot` |
| `RenderAngleTo(float)` | The in-plane angle (`SetRenderTransformAngle`) |

They move, scale and turn what is drawn and never the layout, so siblings do not make room. A widget whose render transform
keeps changing is made a render layer by its canvas and moved on the GPU (see [Rendering](rendering.md)).

```cpp
// Card sits in a VerticalBox: move what is drawn, not its slot
Card->SetRenderOpacity(0.0f);
Card->SetRenderTranslation(FVector(0.0, 0.0, -40.0));   // 40 below where layout put it
Card->RenderOpacityTo(1.0f, 0.25f);
Card->RenderOffsetTo(FVector2D::ZeroVector, 0.35f, 0.0f, EDreamTweenEase::OutCubic)
    ->OnComplete([]() { /* landed */ });

// An icon that keeps breathing
Icon->RenderScaleTo(FVector(1.15, 1.15, 1.0), 0.6f, 0.0f, EDreamTweenEase::InOutSine)
    ->SetLoop(EDreamTweenLoop::Yoyo, -1);
```

The same goes for components written in `.dui`: inside a panel, animate an instance through its render transform.

## Reusable tweens: PlayTween

Use `*To` for a tween made up on the spot in code. For one that is **saved in an asset, tuned in the details panel and
driven by a component**, use `UDreamUIPlayTween`. It is a **player**, not the tween itself: how to play is written down
as serializable fields, and the `Tweener` is only made on `Start()`.

| Field | Default | Notes |
| --- | --- | --- |
| `LoopType` | `Once` | |
| `LoopCount` | **1** | Changed from `-1`; see below |
| `EaseType` / `EaseCurve` | `Linear` | The curve only applies with `CurveFloat` |
| `Duration` | 0.5 | |
| `StartDelay` | — | |
| Four events | — | `OnStart` / `OnUpdateProgress` / `OnComplete` / `OnCycleComplete` |
| `bAffectByGamePause` / `bAffectByTimeDilation` | **false** | **The opposite of the widget's own tweens** (UI is usually not held back by a pause) |

The entry points are `Start` / `Stop` / `GetTweener`. There are **10** typed subclasses: `Float`, `Double`, `Color`, `Int`,
`LinearColor`, `Quaternion`, `Rotator`, `Vector2`, `Vector3`, `Vector4` — each adds a `From` / `To` pair and an
`OnUpdateValue(interpolated value)`.

The `LoopCount` default moved from `-1` to **1** on purpose: with an infinite default, the moment an author picks a loop
type the tween **never ends** — it never leaves the manager, it keeps the player and the widget behind it alive, and a
sequence component waiting on it never reaches its next step.

Two components hang it on the tree (both `BlueprintSpawnableComponent`):

| Component | Does |
| --- | --- |
| `UDreamUIPlayTweenComponent` | `Play()`s one play tween on `Awake` if asked (`bPlayOnStart` defaults to true) |
| `UDreamUIPlayTweenSequenceComponent` | Plays the play tweens in its array **one after another** (`bPlayNextWhenCycleComplete`, `OnComplete`) |

### Twenty-two tweeners, nineteen of them not public API

The public face is the **base class `UDreamTweener`** in `Public/DreamTweener.h`. There are **22** in all: **1 base + 19
concrete + 1 `Spring` + 1 `Sequence`**. By value, the concrete ones are `Float`, `Double`, `Integer`, `Color`,
`LinearColor`, `Vector`, `Vector2D`, `Vector4`, `Quaternion`, `Rotator`, `RotationEuler`, `RotationQuat`, `Position`,
`MaterialScalar`, `MaterialVector`, plus the valueless `Frame`, `Callback`, `Update` and `Virtual`.

⚠️ **The headers of those 19 concrete tweeners are under `Private/Tweener/`** — they are **not public API**: do not treat
them as classes to inherit from or `#include` directly. The only public faces are the base `UDreamTweener` and
`UDreamTweenBPLibrary`.

`UDreamTweenerSequence` strings several tweens onto one timeline: `Append` / `Insert` / `Prepend` / `Join`, plus
`AppendCallback` / `InsertCallback`. It **does not take `Delay` / `DelayFrame` / `Virtual` tweens**, and **cannot be
changed once it has started**.

### Springs: no duration, and a retarget keeps the velocity

`UDreamTweenerSpring` does not advance on a clock but **as a spring**: no duration, no easing; the value is pulled
towards the target and **it is done when it comes to rest**. Its parameters are the mass / stiffness / damping of
`FDreamSpringParams` plus two rest thresholds (`RestDistance` / `RestVelocity`).

`UDreamSpringLibrary` also has two **pure functions**, `StepSpring` and `IsSpringAtRest`, so a spring can be computed
without a widget.

Three properties are its whole value: **a retarget keeps the velocity** (`SetTarget` can be called at any time in flight
and the velocity carries on — that is how a column of lyric lines slides to a new place instead of starting over);
**it settles by its damping** (`DampingRatio()` < 1 bounces, = 1 is critical, > 1 creeps); and **the trajectory does not
depend on the frame length** (`FDreamSpring::Step` uses the **closed-form solution** of the damped harmonic oscillator,
exact and stable at any step, with none of the blow-ups explicit integration has on long frames).

### Who ticks them

`DreamTween` is a separate low-level module that the engine does not tick on its own. It is driven by
`UDreamTweenManager`, with `UDreamTweenTickHelperComponent` (when there is a widget to hang it on) or
`UDreamTweenTickHelperWorldSubsystem` (when there is none). This is also what "manual ticks are never capped" above
means — "manual" is advancing a tween outside this chain.

## Widget animations

A `UDreamWidgetAnimation` is a `UMovieSceneSequence` embedded in the class, held in the root's animation component
(`UDreamWidgetAnimationComponent`), with a class member variable like any other animation, and edited in Sequencer. It plays
through `UDreamUserWidget`'s entry points, which carry UMG's names: `PlayAnimation`, `PlayAnimationByName`,
`PlayAnimationForward`, `PlayAnimationReverse`, `PlayAnimationTimeRange`, `QueuePlayAnimation`, `PauseAnimation`,
`GetAnimationByName`, `BindToAnimationStarted` / `BindToAnimationFinished`, `FlushAnimations`. Playing returns an
`FDreamUIAnimationHandle`, safe to keep: once the instance ends it stops being valid
(`UDreamUIAnimationLibrary::IsAnimationHandleValid`).

- **What a track can drive**: a property marked `Interp`. On a widget that is `RenderTranslation`, `RenderRotation`,
  `RenderScale`, `RelativeLocation`, `RelativeRotationEuler` (Sequencer has no property track for a quaternion, so rotation
  is animated through this Euler mirror), `RelativeScale`, `PerspectiveFieldOfView` and `PerspectiveOrigin`; on a visual,
  `Color`; on a text, its paint phases and `PaintAngleOffset`. The anchor block is a struct, and a struct has no property
  track, so a widget has animatable mirrors of it, shown on the track as Width, Height, Anchor Left and so on
  (`AnimatableWidth`, `AnimatableHeight`, `AnimatableAnchorLeft`, …).
- **Event tracks**: keys on a DreamUI Event track are broadcast through `OnAnimationEvent` as playback crosses them.
- **C++ bindings**: `meta = (BindDreamWidgetAnim)` says the class's code plays an animation of that name, so a hierarchy
  without one is a compile error; `BindDreamWidgetAnimOptional` makes the same claim without the error.
- **The player**: an animation made only of plain property tracks is evaluated straight from its channels by DreamGUI's
  own player rather than through Sequencer's entity system (`DreamUI.Animation.DirectEvaluation`,
  `DreamUI.Animation.LitePlayer`, both on by default).

An `ADreamWorldWidgetActor` placed in a level can also be possessed by a Level Sequence directly, and `WidgetOpacity`,
`WidgetOffset` and `bWidgetVisible` on its component are keyable (see [World-space UI](world-space.md)).

## `.dui` timelines

A `timeline` block in a `.dui` is a widget animation the file owns. One line per track: a path of node ids, the property it
drives, and the keys (`time = value`, each with an optional `ease` and an easing name). `@time -> Name` is a key on the event
track.

```dui
timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Icon.RenderScale            : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
    Row/Title.RenderTranslation : 0.0 = (-40, 0, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}

timeline Celebrate external

Widget Root {
    Image Icon { }
    Widget Row {
        Text Title { Text = "Ready" }
    }
}
```

The block compiles into a `UDreamWidgetAnimation` in the root's animation component, gets its member variable like any
other animation, and plays through the same entry points. Because the file owns it, every compile rebuilds it and **the
animation editor opens it read-only** — edit the `.dui`, or write `timeline Name external` to hand the animation to
Sequencer for good. What a track may drive is exactly what the animation editor offers, and an easing is a name out of
`EDreamTweenEase`, never a tangent. The full rules are on [`.dui` timelines](../language/timelines.md).
