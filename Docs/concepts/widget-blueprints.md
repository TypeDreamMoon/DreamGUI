---
title: Widget Blueprints
description: A UI tree is a class — UDreamWidgetBlueprint compiles into a UDreamUserWidget — with subclassing and nesting, named slots, binding to children by name, a designer that edits the class with no apply step, the prefab model it replaced, and .dui as a Blueprint's source file.
---

A UI tree in DreamGUI is a **class**, not an asset you instance. Authoring one gives you a `UDreamWidgetBlueprint` whose
generated class is a `UDreamUserWidget`; you subclass it, drop it inside another tree, and bind to its named children by
name — the same shape as UMG's widget Blueprints, compiled by DreamGUI's own `FDreamWidgetBlueprintCompilerContext`.

## Asset, class, instance

| | What it is |
| --- | --- |
| `UDreamWidgetBlueprint` | The authoring asset (its type reads **DreamUI Widget Blueprint** in the Content Browser, and the *Add* menu offers it as **DreamGUI Widget**), holding the tree the designer edits (`WidgetTree`) and the graphs |
| `UDreamWidgetGeneratedClass` | What it compiles into. The compile copies the tree onto the class as the template (archetype) instances are built from |
| `UDreamUserWidget` | An instance. It is itself a `UDreamWidget`, exactly as a `UUserWidget` is a `UWidget` |

The two copies are deliberate and match UMG: editing the template in place would change every live instance's template
mid-session.

Because a `UDreamUserWidget` is a widget, nesting falls out for free: an instance of one class placed inside another tree
is just a widget in that tree, while its own contents come from its own class. Those contents live behind `WidgetTree`
(`UDreamWidgetTree`) rather than directly among its children, and the indirection is not decoration: without it "the class
of one widget" and "the class of a tree" would be the same thing. `WidgetTree` is transient — a class template must never
carry an expanded subtree, or a nested class would be baked into its parent's template and stop tracking its own class —
and is built at Initialize, from the class, every time.

Like `UWidgetBlueprint`, the asset is never cooked: a cooked build has the generated class, bytecode included, and needs
nothing else. Uncooked content is another matter: an editor build drops a Blueprint's saved bytecode on load and rebuilds it
from the Blueprint, so any process running uncooked content — a game started with `-game` or as Standalone Game included
— needs this class and its compiler. That is why `DreamGUIEditor` is an `UncookedOnly` module rather than an `Editor` one;
otherwise a widget Blueprint's graphs would do nothing in `-game`.

## An instance's life cycle

The moments a `UDreamUserWidget` can override, as UMG has them:

| Event | When |
| --- | --- |
| `PreConstruct(bIsDesignTime)` | At the top of Initialize, in the designer preview and at run time alike |
| `OnInitialized` | Once the contents are built (`NativeOnInitialized` in C++); in the preview too |
| `OnConstruct` | Begin play: the tree is registered and on screen; game worlds only |
| `OnDestruct` | On tear-down |
| `OnTick` | Every frame only with `bWantsTick` on, which it is not by default — a tick never used still costs a manager list entry, a virtual call and a ProcessEvent |

The pointer, drag, key, focus and navigation events (`OnPointerClick`, `OnDrop`, `OnKeyDown`, `OnFocusReceived`,
`OnNavigate`, …) are here as well. `bAllowEventBubbleUp` is on by default: a user widget is a container, and a container
that silently swallowed every click inside it would break each screen it was placed on; turn it off to consume events at
this boundary. `bCanNavigateHere` is off by default, so the navigation search walks past the widget as it always has;
turn it on when you want `OnNavigate`.

## Subclassing and nesting

- **Subclassing.** A subclass that only adds logic declares no tree of its own and instances its parent's
  (`FindWidgetTreeArchetype` finds the nearest class that has one) — otherwise subclassing a screen to change one function
  would silently produce an empty screen. Property bindings and event routes **accumulate** down the chain: a subclass
  that adds none still honours its parent's, and one that adds some does not replace them.
- **Nesting.** Drag a class into another tree (or write it as a node type in a `.dui`) and the designer folds that
  instance into one row — right for a button or a slider, which are finished things. For the host to put something
  inside, the class has to open a hole on purpose: a named slot.

## Named slots

`UDreamNamedSlot` is a behaviour that marks its widget as a **hole** that whoever places the class fills in: the class
says where, the host says what.

- The slot's name is the widget's display name — what the author already types and sees in the hierarchy, the same
  choice UMG makes for `UNamedSlot`.
- It takes one widget by default (like `UDreamContentWidget`: a hole that took several would be a panel). When the hole
  already is a panel, turn on `bAcceptsSeveral`.
- `bIsDefaultSlot` marks the default slot, where content nested with no slot name goes. One per class (DUI3022).

What a host puts in a slot are **widgets of the host's own tree**, not differences recorded against another asset; at the
end of Initialize they are hung under the matching slot. The calls are on `UDreamUserWidget`: `GetContentForNamedSlot`,
`SetContentForNamedSlot`, `FindSlotWidget`, `GetDefaultSlotName` (a C++ class may override it) and `GetNativeSlotNames`
(how a class whose contents are built by code rather than a template declares its holes).

There is also `UDreamNamedSlotHost`, with a similar name and a different job: it is the counterpart of UMG's
`INamedSlotInterface`, a run-time name-to-child map **inside one tree** (`SetContentForSlot`, `GetContentForSlot`,
`ClearSlot`, `GetSlotNames`), with no other asset involved. Do not confuse it with the named slots between classes.

In a `.dui`, a component opens a hole with `slot` and declares what its hosts may set with `props`; a host nests content
in the instance to fill the default slot:

```dui
class /Game/UI/Components/WBP_Card

props {
    Text Title
}

VerticalBox Root {
    Spacing = 8
    Padding = (16, 16, 16, 16)

    Text Heading {
        Text <- Title
        FontSize = 22
    }
    slot Body default
}
```

```dui
use "UI/Components/Card.dui" as Card

Widget Root {
    + Overlay {}

    Card Audio {
        Title = "Audio"
        Text Hint { Text = "Master volume and effects" }
    }
}
```

The whole syntax is in [`.dui` components](../language/components.md).

## Binding to children by name

Every widget with an id in the tree becomes a member variable of the class, which graphs read directly. A widget-typed
property declared in a C++ parent class is bound to the tree's widget of the **same name**: `InitializeWidgetStatic` binds
every widget-typed property declared below `UDreamUserWidget`, marked or not. The marker decides something else —
`meta = (BindDreamWidget)` makes a missing widget a compile error:

```cpp
UCLASS()
class UMySettingsScreen : public UDreamUserWidget
{
    GENERATED_BODY()

protected:
    // The tree must contain a widget named Confirm, or the compile fails
    UPROPERTY(meta = (BindDreamWidget))
    TObjectPtr<UDreamWidget> Confirm;
};
```

It is deliberately not UMG's `BindWidget`. The two never collide at run time, but a header is read by people, and a bare
`meta=(BindWidget)` on a Dream widget says it belongs to a framework it does not belong to. The animation counterparts are
`BindDreamWidgetAnim` and, without the error, `BindDreamWidgetAnimOptional`. At run time, `GetWidgetFromName` finds a
widget by its variable name.

Events are handled with **routes**: `OnClick -> HandleConfirm` connects an event to a function on the user widget, which
is where UMG puts event handling too. The compiler resolves every route into `UDreamWidgetBlueprint::EventBindings`, and
`UDreamUserWidget::BindEventBindings` attaches them at Initialize. In the designer the same thing is the *Events* section
of the details panel: the `+` creates a custom event with the right signature and the route that names it.

## The designer edits the class

The designer edits a preview instance of the class and writes back to the **class**, so there is no apply step and no
per-instance override list to reconcile. The preview host is transient and non-transactional; the authoring tree is the
only undoable half. The designer itself is described in [Designer](../tools/designer.md).

**Note:**

  The prefab asset model this plugin forked from is gone, along with `SavePrefab`, `Apply`, `ClearLoadedPrefab` and
  *Save on Apply*. The plugin ships no redirects: assets saved against the old class names are resaved once with
  1.0.0, 2.1.0's redirect block borrowed for it; see [Migration](../guides/migration.md).

## `.dui`: a Blueprint's source file

A tree can also be written as text. `UDreamTextUserWidget` is a user widget whose hierarchy comes from a `.dui` file: its
`SourceFile` names the file, and at compile time `FDreamWidgetBlueprintCompilerContext` reads it, builds the tree and
installs it as the Blueprint's authored hierarchy — before the class declares its variables. From then on it is an
ordinary `UDreamUserWidget` in every respect: the same generated class, the same member variable per widget, the same
property bindings, the same designer.

- Text replaces the **authoring** step, not the run time.
- `SourceFile` is editable on the class defaults only (`EditDefaultsOnly`): one class, one tree. The file is read at
  compile time, the only moment that can declare a member variable per widget, resolve bindings, check them, and hand the
  designer a hierarchy to draw.
- It is editor-only data, stripped at cook; relative paths are rooted at a `DUI/` source directory.
- *Set Source File...* in the designer toolbar points a class at a file, and edits made in the designer are written back
  to the line in the file that holds the value.

How to write one: [`.dui` overview](../language/overview.md).

## Showing it

| Call | Does |
| --- | --- |
| The **Create Dream Widget** node | UMG's Create Widget node: picking a class gives it a pin for each Expose on Spawn property (every `props` and `viewmodels` entry of a `.dui` is one) and an Owning Player pin, and types its result as that class. The pins are assigned **before** the widget initializes, so Pre Construct, On Initialized, On Construct and the first value of every binding already see them; UMG assigns after Create, which is too late for On Initialized |
| `UDreamUserWidget::AddToViewport` | UMG's Add to Viewport: on the owning player's screen (the first local player's when none was named), switched on. As in UMG, a widget that already has a parent is refused. `RemoveFromParent` takes it off again |
| `UDreamUserWidget::AddToPlayerScreen` | The same, refused when there is no owning player rather than falling back to screen 0 |
| `UDreamUIBPLibrary::AddWidgetOfClassToViewport` | Create and put on the viewport in one call; the return pin takes the shape of the class |
| `UDreamUIBPLibrary::CreateDreamWidgetOfClass` | Instances it without putting it anywhere (the same not-on-screen state as `ConstructWidget`). Like UMG's `Create`, not in the Blueprint menu — the Create Dream Widget node stands for it; graphs that already call it keep compiling |
| `BeginCreateDreamWidget` / `FinishCreateDreamWidget` | C++: the node's two halves. Write the new widget's own properties in between (`SetViewModel`, say), before it initializes |
| `UDreamScreenUISubsystem::CreateWidgetOnScreen` / `AddToPlayerScreen` | A named player's screen |
| `UDreamWorldWidgetComponent::SetWidgetClass` | Into a level; see [World-space UI](world-space.md) |

The class's members, compared one by one with UMG's `UUserWidget`, are in the
[`UDreamUserWidget` reference](../Reference/DreamUserWidget.md).
