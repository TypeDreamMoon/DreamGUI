---
title: Gradients and viewport handles
description: Multi-stop gradients on a rect block's body and border — linear, radial, angle, diamond, reflected and four corners, the same gradients a text paints with — edited in the details panel or by dragging their ends and stops in the designer's viewport; how two-colour gradients from before carry over; and the handle framework other features drag their points with.
---

## A rect block's gradients

A rect block (`UDreamRectBlock`) fills its body, and draws its border, with a gradient instead of a plain colour when
`bEnableBodyGradient` (or `bEnableBorderGradient`) is on. The gradients are `BodyGradient` and `BorderGradient`, each an
`FDreamGradient` — the struct a text paints with ([Gradient text](text.md#gradient-text)), drawn from the same gradient
rows on the GPU: a rect block and a text that use the same gradient share its row.

| Type | Image editors call it | What it is |
| --- | --- | --- |
| `Linear` | Linear | Along a direction (`Angle`), CSS's `linear-gradient` |
| `Radial` | Radial | Out from `Center` to an ellipse (`Shape`, `Size`, `Radius`) turned by `Angle` |
| `Conic` | Angle | Round `Center`, clockwise from `Angle` |
| `Diamond` | Diamond | Out from `Center` in a diamond turned by `Angle` |
| `Reflected` | Reflected | Out from a line through `Center` across `Angle`, both ways alike |
| `Corners` | — | The first four stops at the four corners, mixed across |

Up to 16 stops, each a colour (sRGB, straight alpha) at a position along the gradient; `Spread` says what happens past the
ends (pad, repeat, reflect), `Scale` and `Offset` stretch and move it, and `Interpolation` mixes the stops in sRGB, linear
light or Oklab. The body's gradient takes the place of `BodyColor` and is multiplied by the body's texture, as the colour
is; the border's takes the place of `BorderColor`.

In a `.dui` a gradient is its CSS, the spelling the details panel shows and copies:

```
RectBlock Card {
    bEnableBodyGradient = true
    BodyGradient = "linear-gradient(135deg, #FFF3B0, #E8B64A 55%, #9C6A12)"
    bEnableBorder = true
    bEnableBorderGradient = true
    BorderGradient = "reflected-gradient(90deg at 50% 50%, #FFFFFF, #30B0C7)"
}
```

`diamond-gradient`, `reflected-gradient` and `corners-gradient` are DreamGUI's words for what CSS has none for; an angle in
a `radial-gradient` turns its ellipse. In C++ and Blueprint: `SetBodyGradient`, `SetBorderGradient`, and
`GetEffectiveBodyGradient` / `GetEffectiveBorderGradient` for the gradient a rect block draws.

### In the details panel

Under Body › Gradient (and Border › Gradient) the row shows the gradient as a strip, with its type beside it and the
**Presets** menu: the project's gradient assets (`UDreamGradientAsset`) and its named presets (Project Settings › Plugins ›
Dream GUI › Gradient Presets), or the gradient saved as either. Open it for the gradient as CSS (type or paste one), the
stops in the engine's gradient editor (drag a mark, double-click to pick its colour, click the strip to add one), the stops
as a list, and the fields the type uses.

## Viewport handles

With one rect block selected in the designer, its gradient is drawn over it the way an image editor's gradient tool draws
one:

- **Linear, Reflected**: a start and an end. Drag either; the other stays.
- **Radial, Angle (Conic), Diamond**: a centre and an end — the end sets the radius and the turn (a Radial ellipse has a
  second end for its other radius; an Angle's end sets only where it starts). Drag the centre to move the whole gradient.
- **Stops**: a square of each stop's colour beside the line, joined to it. Drag one along the line to move it; drag it
  well away from the line to take it out (two always stay). Double-click the line to add a stop of the colour already
  there.
- **Shift** keeps a turn to steps of 15°. **Esc** while dragging puts everything back.

A drag is one undo step, and the `.dui` is written when the mouse button comes up. When a rect block has both gradients on,
the handles are the body's; the border's row in the details panel has a button that shows the border's instead.

## Two-colour gradients from before

Rect blocks used to have a two-colour radial gradient: `BodyGradientColor` at `BodyGradientCenter` fading to `BodyColor`
at `BodyGradientRadius`, turned by `BodyGradientRotation`, with unit modes for the centre and the radius (and the same
`BorderGradient…` properties for the border). They carry over without anything to do:

- A rect block whose `BodyGradient` has no stops draws its old two-colour gradient, now as two stops: a radial ellipse
  with the old centre, radius and turn, mixed in linear light. It looks as it did, to a level per channel at most.
- A `.dui` written the old way still builds, and the designer leaves its lines as they are until the gradient is edited.
  The first edit — in the details panel or with a handle — writes the two stops into `BodyGradient` and the new line
  replaces the old ones in the file.
- The old setters (`SetBodyGradientColor`, `…Center`, `…Radius`, `…Rotation` and their border twins, and the unit modes')
  still work and are deprecated. On a gradient that has stops they set its first stop's colour, its centre, its radius
  and its angle. The tweens (`BodyGradientColorTo` and the rest) go through them.

Two things are not the same as before. A centre or a radius in units (rather than a percentage) stays where it is in
units while the gradient is two-colour; once it has stops, everything is a fraction of the rect and grows with it. And a
body texture that is itself partly see-through used to have the gradient colour laid over its alpha a second time; now the
gradient tints it the way the body colour does.

## Handles for other features

The viewport's handles are a framework (`Designer/DreamDesignerHandles.h` in the DreamGUIEditor module) that anything
selected can use. A provider — an `IDreamDesignerHandleProvider` registered with `FDreamDesignerHandles::Register` —
says, for the selected widget, which points it has and which lines to draw, which properties a drag of one writes, and how
a step of the drag changes the preview's objects. The viewport does the rest the same way for all of them: hit testing,
drawing, one transaction per drag, putting the preview back on Esc, writing the properties to the asset and the asset to
the `.dui` when the button comes up, and double-clicks. A warp's lattice points, a path's points and the rect block
gradients are three providers.

```cpp
#include "Designer/DreamDesignerHandles.h"

class FMyHandles : public IDreamDesignerHandleProvider
{
public:
	virtual FName GetName() const override { return TEXT("MyHandles"); }
	virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
	{
		if (const UMyVisual* Visual = Cast<UMyVisual>(InWidget->GetVisual()))
		{
			OutSet.AddHandle(0, Visual->GetPoint());
		}
	}
	virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& Out) const override
	{
		Out.Emplace(InWidget->GetVisual(), TEXT("Point"));
	}
	virtual FText GetTransactionName(int32 InHandleId) const override { return INVTEXT("Move Point"); }
	virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
	{
		Cast<UMyVisual>(InWidget->GetVisual())->SetPoint(InDrag.GetTarget());
		return true;
	}
};

// In your editor module's StartupModule:
FDreamDesignerHandles::Register(MakeShared<FMyHandles>());
```

Points are in the widget's local space — units from its pivot, up is +Y — and `RectFractionToLocal` /
`LocalToRectFraction` convert from and to fractions of its rect.
