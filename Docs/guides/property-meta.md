---
title: Property meta tags
description: The DreamGUI_ meta tags that place a property in DreamGUI's details panels, hide it, pick its control or make it an effect's switch -- a property without them is never dropped -- and how an extension's classes and customizations use them.
---

DreamGUI's details panels lay themselves out from meta tags on the properties they show. The tags all start with
`DreamGUI_`, and none of them is needed: **a property without tags is shown under its own `Category`, the way the
engine shows it.** Only a property tagged `DreamGUI_Hidden` is left out. No DreamGUI panel hides a category whole, so
a property added to a class cannot disappear from the editor because of the category it was given.

The tags work on an extension's own classes too: a class with tags and no details customization gets one that places
them, and a customization of your own places them by calling `FDreamDetailsLayout::PlaceTagged` (below).

## The tags

| Tag | Value | What it does |
|---|---|---|
| `DreamGUI_Section` | `Layer`, `Layout`, `Visual`, `Appearance`, `Effect`, `Behavior`, `Advanced` | Puts the property in that top-level section of the panel. |
| `DreamGUI_Group` | a path, `Glass/Refraction` | Puts it in that group of its section, or of its own category when it has no section. Groups nest at each `/`. |
| `DreamGUI_Order` | a number | Its place in its section or group, smaller first. Untagged ones count as 0 and keep the order they are declared in. |
| `DreamGUI_Advanced` | -- | Under the advanced expander of where it goes. |
| `DreamGUI_Hidden` | -- | Not shown: internal state, or a value another row of the panel edits (say which in a comment). |
| `DreamGUI_Editor` | `AngleWheel`, `Curve`, `Gradient`, `BlendMode`, `Color`, `Slider` | Which of DreamGUI's controls edits it, when its type alone does not say. An option follows a colon: `AngleWheel:TopCW`. |
| `DreamGUI_Handle` | a handle kind, `Gradient`, with arguments after a colon (`Kind:Args`) | Which handles the designer's viewport shows for it on the selected widget. The kinds are the viewport handle framework's (`FDreamDesignerHandles::RegisterKind`). |
| `DreamGUI_LayerToggle` | -- | The switch that turns the whole effect it belongs to off and on: the eye in the hierarchy and in the Layer section. A switch inside a struct member counts (`Style.bEnabled`). |

Showing a property only while another one has some value stays the engine's `EditCondition` and `EditConditionHides`:
the row is always there, and shows the moment the condition holds.

`AdvancedDisplay` on a property of a sub-category (`Category = "DreamGUI|Pointer Light"`) does not hide it behind an
expander in DreamGUI's panels: the engine draws a sub-category without its advanced rows -- "show all advanced" or not --
so DreamGUI gives such a property an ordinary row of its sub-category instead, after the others. To fold a property away,
use `DreamGUI_Advanced`, or put it in a category with no sub-category.

```cpp
// Shown with the widget's opacity and mask in the Layer section, second.
UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (DreamGUI_Section = "Layer", DreamGUI_Order = 20))
EDreamUIBlendMode BlendMode = EDreamUIBlendMode::Alpha;

// The eye on this effect in the hierarchy turns this off and on.
UPROPERTY(EditAnywhere, Category = "DreamGUI|Holo Foil", meta = (DreamGUI_LayerToggle))
bool bEnabled = true;

// Only for a filled image, and only then shown.
UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (EditCondition = "DrawType == EDreamUISpriteDrawType::Filled", EditConditionHides))
float FillAmount = 1;
```

## Sections

A section is a category of the panel with a fixed name, title and place. They are the ones a widget's details are built
of, top to bottom:

| Section | Category | Order |
|---|---|---|
| `Layer` | `DreamLayer` | -110 |
| `Layout` | `DreamLayout` | -90 |
| `Visual` | `Visual` | -80 |
| `Appearance` | `DreamAppearance` | -60 |
| `Effect` | `DreamEffect` | -55 |
| `Behavior` | `DreamBehavior` | -50 |
| `Advanced` | `DreamAdvanced` | 100, collapsed |

A visual is shown inside its widget's details, so a visual's `Layer` properties are drawn by the widget's Layer section
and left out of the visual's own rows -- that is how a visual's blend mode sits beside the widget's opacity.

## Viewport handles

`DreamGUI_Handle` names a kind of handles the designer's viewport draws over the selected widget for the property: the
widget's own properties, its visual's and its behaviours', and a plain struct property's members one level down. Each
tagged property gets a provider of its own, named `Kind.Property` ("Gradient.BodyGradient"); what follows a colon is handed
to the kind as it is.

| Kind | Tagged on | Handles |
|---|---|---|
| `Gradient` | a rect block's `BodyGradient`, `BorderGradient` | The gradient's ends, centre and stops ([Gradients](../concepts/gradients.md#viewport-handles)) |
| `MaskShape` | a widget's `ClipMask` | Its clip's sides, a rect mask's corners, a polygon's roundness, a star's or a ring's inner size, the feather |
| `GlassShape` | a glass's `CornerRadius` | Its corners, its edge's width, where its light comes from |
| `Shadow` | a layer style's `Style`; a rect block's `OuterShadowAngle` | The style's shadows; the rect block's own two -- each where it falls |
| `Light` | a pointer light's `LightPosition`; a layer style's `Style.GlobalLightAngle` | Where the light stands and how far it reaches; the global light as a disc |

A kind is registered by the module that draws it, with a factory that makes the provider for one tagged property:

```cpp
FDreamDesignerHandles::RegisterKind(TEXT("MyKind"), [](const FDreamDesignerHandleBinding& InBinding) -> TSharedPtr<IDreamDesignerHandleProvider>
{
	// InBinding.Owner is the object, InBinding.Property the tagged property, InBinding.Args what follows the colon.
	return Cast<UMyVisual>(InBinding.Owner.Get()) ? MakeShared<FMyHandles>(InBinding.Property) : nullptr;
});
```

## In a customization of your own

```cpp
void FMyCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FDreamDetailsLayout Layout(DetailBuilder);
	Layout.Section(DreamPropertyMeta::ESection::Behavior).AddCustomRow(...);   // rows of its own
	// What applies only in some state of the object that no property says -- whether a canvas is a root -- keeps its row,
	// shown while it applies:
	Layout.ShowWhen(TEXT("ReferenceResolution"), TAttribute<bool>::CreateLambda([...] { return IsRoot(); }));
	Layout.PlaceTagged();   // last: every tagged property not placed yet, placed
}
```

A property is either placed by tags or by a customization's own rows, not both: a tag does not move a property a
customization has already added or hidden. A row a customization draws for a property itself (a value with a unit
button beside it) hides that property's default row with `HideProperty`, and the properties it edits on that row (the
unit) are tagged `DreamGUI_Hidden`.

`DreamPropertyMeta` (`DetailCustomization/DreamPropertyMeta.h`) reads the tags; `FDreamDetailsLayout`
(`DetailCustomization/DreamDetailsLayout.h`) places them.

## The test that holds it

`DreamGUI.Editor.Details.EveryEditablePropertyHasARowUnlessTaggedHidden` shows every class of the plugin's modules the
way the editor does -- a widget in its own details, its visual, layouts and navigation inside them, a component in its
own, a panel slot under each kind of panel, anything else as its class defaults -- and every struct on its own, and fails
on any editable property without a row that is not tagged `DreamGUI_Hidden`, and on any property with two rows.
