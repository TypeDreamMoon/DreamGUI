---
title: The widget model
description: What a UDreamWidget is — a UObject with a rect, anchors and a pivot, carrying a visual, a layout, a slot and behaviours, arranged in a tree — plus render transforms, opacity and visibility, and the sizing difference from UMG.
---

A widget in DreamGUI is a `UDreamWidget`: a `UObject` with a rect, a pivot and anchors, arranged into a tree and drawn
by a [canvas](canvas.md) that batches the whole tree into as few draw calls as it can. That is Unity's
uGUI shape rather than Slate's. It is not an actor and not an actor component; there is no `SWidget` behind it, so none
of UMG's widgets, styles or bindings apply.

A widget on its own draws nothing and arranges nothing. What it draws, how it arranges its children, where its parent
puts it and what logic it runs are each decided by a sub-object it carries — in the words of `ConstructWidget`'s comment,
"panel" is a sub-object here, not a separate class.

## What a widget is made of

| Part | Property / entry point | Decides |
| --- | --- | --- |
| Rect | `AnchorData` (`FDreamUIAnchorData`) | Where it is and how big, relative to its parent |
| Visual | `Visual` (`UDreamVisual`), at most one per widget | What it draws |
| Layout container | `LayoutContainer` (`UDreamLayoutContainer`) | How it arranges its children |
| Layout self | `LayoutSelf` (`UDreamLayoutSelf`) | A sizing rule of its own: an aspect ratio, a spacer |
| Panel slot | `PanelSlot` (`UDreamPanelSlot`) | How its parent's panel places it |
| Behaviours | The component array: `AddComponent`, `GetComponent`, `GetComponents` | Logic: buttons, canvases, navigation scopes… |
| Children | `GetChildren`, `AddChild`, `SetParent` | The tree |

In a `.dui` the parts are easy to tell apart: the node type is a visual (`Text`, `Image`) or a layout container
(`VerticalBox`), `+ Class { }` attaches a behaviour or a container, and `@slot` lines are what the parent's panel does with
the node.

```dui
Widget Root {
    // All four anchors on the parent's corners, no inset: fills the parent at any resolution
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)

    Widget Card {
        // Anchored to one point, the parent's centre: SizeDelta is its size, AnchoredPosition the pivot's offset from the anchor (y up)
        AnchorData.AnchorMin = (0.5, 0.5)
        AnchorData.AnchorMax = (0.5, 0.5)
        AnchorData.SizeDelta = (420, 220)
        AnchorData.AnchoredPosition = (0, 40)

        // A visual: a backdrop that fills the card
        Image Backdrop {
            Brush.TintColor = #1B1E26FF
            AnchorData.AnchorMin = (0, 0)
            AnchorData.AnchorMax = (1, 1)
            AnchorData.SizeDelta = (0, 0)
        }

        // A behaviour: a plain widget carrying the button behaviour
        Widget Confirm {
            AnchorData.SizeDelta = (160, 44)
            + UIButton { TransitionType = None }
            Text Label { Text = "OK" }
        }
    }
}
```

## The rect: anchors, pivot, size

`FDreamUIAnchorData` has five fields, and its defaults are a 100 × 100 square anchored at the parent's centre:

| Field | Default | Meaning |
| --- | --- | --- |
| `AnchorMin` / `AnchorMax` | `(0.5, 0.5)` | Two normalized points in the parent's rect. Equal on an axis, the widget is pinned to a point on that axis; different, it stretches along it |
| `SizeDelta` | `(100, 100)` | The size, on a pinned axis; on a stretched one, the size **beyond the anchor span** — `0` fits exactly, negative insets |
| `AnchoredPosition` | `(0, 0)` | The pivot's offset from the anchor |
| `Pivot` | `(0.5, 0.5)` | A normalized point in the widget's own rect, `(0, 0)` bottom-left and `(1, 1)` top-right |

Coordinates run y up: `Pivot`, `RenderTransformPivot` and `PerspectiveOrigin` all put their origin at the bottom left,
unlike UMG's top-left origin — deliberately, because consistency inside the plugin beats consistency with the other
engine.

At run time each field has its setter: `SetAnchorMin`, `SetAnchorMax`, `SetAnchoredPosition`, `SetSizeDelta`,
`SetPivot`, and the result-shaped `SetWidth`, `SetHeight` and `SetAnchorOffset` (the four edge distances of a stretched
widget). Each has a tween too — `AnchoredPositionTo`, `SizeDeltaTo`, `WidthTo`, `HeightTo` — returning a `UDreamTweener`
(see [Animation](animation.md)).

`RelativeLocation` can be set as well, but its setter recomputes the anchors and asks the parent to lay out again; a
`.dui` spells `AnchorData` only, so the same position is never written twice.

## Box-first: the sizing difference from UMG

The difference is structural rather than cosmetic, and worth knowing before you commit to either:

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| Widget | `SWidget`, retained-mode Slate | A `UObject` in a component-like tree |
| Sizing | Content-sized: a widget's size **is** its desired size | Box-first: you author a rect, content is arranged inside it |
| Text | The box grows to the text | The text is aligned in the box, and may overflow it |
| Placement | Slot-relative | Anchors + pivot, resolution-independent |
| Reuse | Widget Blueprint subclassing | Widget Blueprint subclassing, plus named slots for content |
| In the world | `WidgetComponent`, a rendered quad | A first-class render mode |

The text row is the one that surprises people. A UMG `TextBlock` cannot overflow, because its box is derived from the
text. Here the box is authored, so text can overflow it, and you get controls UMG has no need for — `Margin`,
`LineHeightPercentage`, `WrapTextAt` and Best Fit; see [Text](text.md).

Box-first does not mean there is no content size. Once a widget is inside a panel, the panel asks it how big it wants to
be (`GetDesiredSize`) and writes the arranged rect back into its `AnchorData`. A visual answering that question with a
negative number abstains, and the authored rect is used; `0` is a real answer. The full rules are in
[Layout](layout.md).

## Visuals: what a widget draws

A visual (`UDreamVisual`) is a sub-object of its widget, at most one per widget. A widget without one is a rect — `Widget`
in a `.dui` — which is what a pure container is. Every visual has `Color` (sRGB, keyable in Sequencer), `bRaycastTarget`
and `RaycastType` (`Rect`, `Mesh`, `VisiblePixel`, `Custom`).

| `.dui` tag | Class | Module | Draws |
| --- | --- | --- | --- |
| `Text` | `UDreamText` | `DreamGUI` | Text; see [Text](text.md) |
| `Image` | `UDreamImage` | `DreamGUI` | An image brush |
| `RectBlock` | `UDreamRectBlock` | `DreamGUI` | A procedural block: corner radii, a body, a border, inner and outer shadows, the body and border optionally graded |
| `Sprite` / `Texture` | `UDreamSprite` / `UDreamTexture` | `DreamGUI` | An atlas sprite / a whole texture |
| `Empty` | `UDreamVisualEmpty` | `DreamGUI` | Nothing, but takes raycasts |
| `Ring` / `Polygon` / `PolygonLine` | `UDreamRing` / `UDreamPolygon` / `UDreamPolygonLine` | `DreamGUIExtensions` | A ring, a polygon, a polygon outline |
| `Line2DRaw` / `Line2DChildren` | `UDream2DLineRaw` / `UDream2DLineChildrenAsPoints` | `DreamGUIExtensions` | 2D lines: through given points / through the children |
| `StaticMesh` | `UDreamStaticMesh` | `DreamGUIExtensions` | A static mesh, a draw call of its own |
| `BackgroundBlur` / `BackgroundPixelate` / `PixelSort` | `UDreamBackgroundBlur` / `UDreamBackgroundPixelate` / `UDreamPixelSort` | `DreamGUIExtensions` | Screen effects; see [Rendering](rendering.md) |
| `CanvasRenderTargetPreviewer` | `UDreamCanvasRenderTargetPreviewer` | `DreamGUIExtensions` | What a render-target canvas drew |
| `UMGWidget` | `UDreamUMGWidget` | `DreamGUIExtensions` | Renders a UMG widget into a render target, then draws it as DreamGUI's own geometry; see [UMG interop](../controls/index.md#umg-interop) |
| `PostProcessRenderElement` | `UDreamPostProcessRenderElement` | `DreamGUIExtensions` | Grabs the result image of a post-process visual back and shows it as a piece of geometry here |
| `PostProcessRenderElementText` | `UDreamPostProcessRenderElement_Text` | `DreamGUIExtensions` | The same, with a text visual as the host |

Visuals come in three kinds (`EDreamVisualType`): `BatchMesh`, which the canvas batches and which almost every visual
is; `DirectMesh`, which writes its own mesh section and is a draw call each, for content with many vertices that change
often (the static mesh); and `PostProcess`, an effect that reads and writes the screen. A plugin can add a visual of its
own, registering a `.dui` tag for it with `DECLARE_DREAM_GUI_VISUAL`.

Procedural shapes — the ring, the polygon, the polygon line — have no answer to "how big do you want to be": their
vertices are derived from the rect they are given. They abstain from measurement and follow the authored rect.

## Behaviours: logic on a widget

Behaviours are subclasses of `UDreamUIBehaviour`, held in order in the widget's component array. They have a Unity-shaped
life cycle — `Awake`, `OnEnable`, `Start`, `Tick`, `OnDisable`, `OnDestroy` — plus callbacks such as
`OnDimensionsChanged`, `OnAttachmentChanged` and `OnInteractableChanged`; a Blueprint subclass gets events of the same
names at the same moments. `bStartWithTickEnabled` and `bTickEvenWhenPaused` govern its tick.

`Awake` runs **higher in the hierarchy first** — a script on the root widget runs before any other.

Besides the six moments there are **seven change callbacks**: `OnTransformChanged`, `OnDimensionsChanged`,
`OnChildDimensionsChanged`, `OnAttachmentChanged`, `OnSiblingIndexChanged`, `OnInteractableChanged`,
`OnRaycastableChanged`. `OnDimensionsChanged` tells you **item by item** what changed (`PivotChanged` / `WidthChanged` /
`HeightChanged`); `OnChildDimensionsChanged` is **a separate one** — use it when a parent has to grow with its
**children**, not its own. Some callbacks would fire before `Awake`; those are cached (`CallbacksBeforeAwake`) and run
at `Awake`.

On the Blueprint side there are **13 `Receive*` events** (6 moments + 7 callbacks), all `BlueprintImplementableEvent`,
and **the names shown in Blueprint drop the `Receive`** — `ReceiveAwake` is just `Awake`.

### Declaring "I don't use it"

`bStartWithTickEnabled` / `bTickEvenWhenPaused` are **run-time** switches; two more are **declared in the constructor**:

| Declaration | Afterwards |
| --- | --- |
| `DeclareTickUnused(InClass)` | Behaviours of exactly this class (and Blueprints derived from it) **stay out of the UI manager's tick list** instead of being called for nothing every frame |
| `DeclareTransformChangedUnused(InClass)` | **Does not listen to the widget's move notifications** — and as a knock-on effect, a widget no one listens to for moves is not notified of moves at all |

Two limits to remember: it **only covers the class that is exactly `InClass`** (a C++ subclass can override `Tick`, and
**nothing can declare on its behalf** — the subclass ticks as before until its own constructor declares too); and it
**does not change whether the behaviour may tick** — that is `SetCanExecuteTick`'s job, and a Blueprint that implements
the `Tick` event still counts.

### Two sub-object base chains

There are **two** chains of sub-objects hung on a widget, of different weight. Everything above is the heavy one; the
light one is `UDreamWidgetSubObjectBehaviour` (`BlueprintType`, `Abstract`, `DefaultToInstanced`, `EditInlineNew`, with
**no** `ClassGroup` and **no** `Blueprintable`), which only offers `OnRegister` / `OnUnregister`, `BeginPlay` / `EndPlay`,
`IsRegistered`, `GetWidget` and `GetPathDisplayName` — **panel slots** use it. Something that runs every frame or
listens to the widget's changes takes the heavy chain; plain "data and logic hung on a widget" takes the light one.

Most of the plugin is behaviours: the canvas itself (`UDreamCanvas`), the `UI*` family the `Dream*` controls are built
from (`UIButton` and its siblings), navigation scopes, named slots (`UDreamNamedSlot`), drag sources and drop targets, the
mesh modifiers (shadow, outline, gradient colour, text animation). In code, `AddComponent`, `GetComponent`,
`GetComponents`, `GetComponentByInterface`, `RemoveComponent` and `MoveComponentToIndex`; in a `.dui`,
`+ UIButton { … }`.

## Render transform: drawing moves, layout does not

A render transform moves where a widget is **drawn** without telling layout anything. Layout still measures and arranges
it exactly as before, siblings do not shift, and nothing is ever written back to `AnchorData`. That is the whole
difference from `RelativeLocation`, whose setter recomputes the anchors and asks the parent layout to rebuild — which is
why animating that inside a panel never works: the animation and the layout just take turns.

| Property | Type | Notes |
| --- | --- | --- |
| `RenderTranslation` | `FVector` | Local space. X is depth and points **away** from the viewer, so a negative X brings the widget toward the screen |
| `RenderRotation` | `FRotator` | About `RenderTransformPivot`, in degrees; `SetRenderTransformAngle` writes only the in-plane channel |
| `RenderScale` | `FVector` | About `RenderTransformPivot` |
| `RenderTransformPivot` | `FVector2D` | Normalized in the widget's own rect, `(0, 0)` bottom-left; independent of `AnchorData.Pivot`, which belongs to layout |
| `RenderShear` | `FVector2D` | A slant in degrees, applied to the widget and its whole subtree |

It is three-dimensional on purpose: the transform it mirrors is already an `FVector` / `FQuat` / `FVector`, world-space
canvases exist, and a card flipping about its vertical axis is an ordinary thing to want. The batching rule is the one
the authored transform already has: rolling, scaling and sliding in the canvas plane stay batched; a widget that gains
depth, yaw or pitch leaves the batched 2D path, so a flip costs a draw call — exactly as it would if you had authored the
rotation.

`RenderShear` differs in two ways: layout never sees it, and **hit testing does not follow it** — a sheared widget is
clicked on its unslanted rect (the raycaster switches to the matrix path only inside a perspective scope). It is also
not keyable, while the other three channels are. `ClearRenderTransform` puts them all back in one invalidation;
`HasRenderTransform` answers whether drawing currently differs from layout.

A widget whose render transform keeps changing is made a **render layer** by its canvas and moved on the GPU; see
[Rendering](rendering.md). To animate a widget inside a panel use `RenderOffsetTo`, `RenderScaleTo`,
`RenderAngleTo` and `RenderTranslationTo`; see [Animation](animation.md).

### Perspective, per widget

`bPerspective` makes a widget establish a perspective for its **descendants**, in the shape CSS `perspective` has:
children with depth are foreshortened toward an eye standing in front of this widget's plane. `PerspectiveFieldOfView`
(60 degrees by default) sets how far away the eye stands and `PerspectiveOrigin` is the vanishing point (CSS
`perspective-origin`). A nested perspective nests rather than overrides. It costs nothing when off, and nothing for a
subtree with no depth in it — a flat widget comes back untouched. It needs a screen-space canvas with a perspective
projection and is inert otherwise; see [Canvas](canvas.md).

## Visible, transparent, interactive

Each switch decides one thing; do not use one for another:

| Switch | Decides |
| --- | --- |
| `Visibility` (`EDreamWidgetVisibility`) | One for one with UMG: `Visible`, `Hidden` (takes space, not drawn), `Collapsed` (takes no space), `HitTestInvisible`, `SelfHitTestInvisible` |
| `Shown` | `Visibility` as a yes or no: `true` is `Visible`, `false` is `Collapsed`. It stores nothing of its own; it is what `Shown <- HasSave()` binds in a `.dui` |
| `bWidgetActive` | The behaviour life-cycle switch: inactive is invisible, takes no layout space, is not interactive and not hit-testable, and its behaviours hear `OnDisable` |
| `bIsEnabled` / `SetIsEnabled` | UMG's enabled switch: the subtree stops taking input and its controls take on their disabled look; `GetIsEnabledInHierarchy` is the cascaded answer |
| `Interactable`, `Raycastable` | Three-valued (`Inherit`, `Enabled`, `Disabled`) interactivity and hit-testability |
| `RenderOpacity` | Opacity, multiplied down the tree; `GetFinalRenderOpacity` is the product |
| `SetContentTint` | Tints the visuals of the **descendants** (not its own), as UMG's content colour does |

`Shown` never undoes hidden or hit-test-invisible: a `Hidden` widget set to shown stays `Hidden`. Clipping is
`Clipping` (`Inherit`, `ClipToBounds`, `ClipToBoundsWithoutIntersecting`, `Disabled`), with `ClippingCornerRadius` and
`ClippingMargin`. `OnVisibilityChanged`, `OnFocusReceived` and `OnFocusLost` are the three events a widget offers for
binding.

## Making widgets in code

`UDreamUIBPLibrary::ConstructWidget(WorldContext, DisplayName, VisualClass)` is the counterpart of UMG's
`WidgetTree::ConstructWidget<T>()`: the widget exists but is not on screen. It draws nothing and its behaviours do not
run until you hand it to `AddChild` or `AddToViewport`; until then the DreamUI manager holds it, so it is not collected
while you configure it. Leave `VisualClass` empty for a pure container, then give it a layout container to make it a
panel.

| Call | Result |
| --- | --- |
| `AddChild(Child, SiblingIndex)` | Attaches it and returns the child's `UDreamPanelSlot` (one slot class for every panel, so nothing to cast) |
| `RemoveChild` / `RemoveFromParent` | Detaches it and keeps it alive — still registered, subtree intact, back in the not-yet-added state; the verb to pool with |
| `DestroyChild` / `DestroyWidget` | Tears the subtree down; `IsValid` answers false afterwards |
| `FindChildByDisplayName` | Finds a child by display name, paths included: `"Content/ListItem/NameLabel"` |

An instance of a widget Blueprint is made with the **Create Dream Widget** node in a graph, and with
`UDreamUIBPLibrary::CreateDreamWidgetOfClass` or `AddWidgetOfClassToViewport` in C++; see
[Widget Blueprints](widget-blueprints.md). Every property and function is
listed in the [`UDreamWidget` reference](../Reference/DreamWidget.md), which ends with a member-by-member comparison
with UMG's `UWidget`.
