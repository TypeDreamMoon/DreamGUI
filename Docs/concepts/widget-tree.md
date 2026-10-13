---
title: The widget tree, parking and hosts
description: Widgets live on a tree, and the tree decides who looks after them — plus the state on it that is easiest to misread, "parked". Also the host contract and the presenter base class.
---

Widgets live on a tree, and the tree decides who looks after them. [The widget model](widgets.md) covers
what a widget is made of; this page covers **the tree itself**, and the state on it that is easiest to misread.

- The four tree verbs, visuals and behaviours → [The widget model](widgets.md)
- The `WidgetTree` in a user widget → [Widget Blueprints](widget-blueprints.md)
- **What** "parked" is → [Scripting](../tools/scripting.md)
- **What is under the tree, and what carries parking** → this page

## What the widget tree is, and why there has to be one

`UDreamWidgetTree` is a `UObject`, and one comment gives the only reason it exists: **it owns a widget hierarchy, and is
the object every widget in that hierarchy is outered to**.

To understand it, look at what it replaced: early DreamGUI outered widgets **flat** to the `UWorld`, and the hierarchy
lived only in a prefab's binary blob, rebuilt on load. That way the engine **had no object graph** to serialize,
duplicate or instance — **which is exactly why a tree could not live on a class template**. The tree is the anchor that
was missing.

Inside the tree, widgets **stay flat** too rather than nesting under their parents — as in UMG.
`FObjectInstancingGraph` follows **`Instanced` properties, not the outer chain**, so flat outers **cost nothing**; and
**reparenting never has to move an outer**, which **keeps `UObject::Rename`** (unsafe once an object has begun to be
destroyed) **out of the attach path**.

Two things it deliberately does not do:

- **Held as a class template, a tree returns no world, never registers and never ticks.** A tree a class holds as its
  template **deliberately** never reaches a level, so `GetWorld` returns **a null world**, and registration only happens
  through an explicit `OnRegister` call (**never** through `PostInitProperties`).
- **Parent pointers are not serialized**; `RebuildParentLinks` restores them from the persistent `Children` arrays — it
  **has to run** after any path that **produces a tree without going through the attach functions** (a package load, a
  class-template instance, a subtree copy) and before registration.

## Hosts: who owns a tree

What really owns a tree is a **host** (`IDreamWidgetTreeHost`): the host is the `Outer` of each of its trees, which is
how a widget still finds its world.

Every tree is held by its host in a **`Transient` / `DuplicateTransient` / `TextExportTransient`** property — so
**nothing that saves, duplicates or exports the host takes a tree along with it**.

The manager only **observes**: a tree that is **registered but has lost its host** is **a bug to report**, not something
left to GC to clean up.

The host interface has only three methods:

| Method | Does |
| --- | --- |
| `GetTreeOuter()` | The `Outer` of each of its trees; its outer chain **must reach** the `UWorld` the host is in |
| `ReleaseTree(Reason)` | Tears down **immediately**, idempotently; `Reason` is `Recompile` / `Undo` / `LevelRemoved` / `HostDestroyed` — **the host rebuilds after the first two, not after the last two** |
| `RebuildTree()` | Rebuilds from the class after a release that **was not a destruction** |

## Queries and names on the tree

The four verbs (`AddChild` / `RemoveChild` / `DestroyChild` / `FindChildByDisplayName`) are on
[The widget model](widgets.md). What the tree adds is a set of **query and naming** APIs:

| Member | Does |
| --- | --- |
| `RootWidget` (`Instanced`) | The root of the hierarchy. `FObjectInstancingGraph` enters the tree **here** |
| `ForEachWidget(Predicate)` | Visits **every** widget in the tree, **root first, parents before children**, skipping nothing |
| `GetAllWidgets()` | The Blueprint face of the same walk — the counterpart of UMG's `UWidgetTree::GetAllWidgets` |
| `CountWidgets()` | The total, **root included**; ⚠️ **walks, does not cache** |
| `FindWidgetByVariableName(Name)` | Finds by **variable name** — UMG's `GetWidgetFromName` |
| `MakeWidgetVariableName(Widget)` | The member variable name the widget **binds to**: its **display name** sanitized into an identifier |
| `SanitizeIdentifier(Raw)` | Keeps only letters, digits and underscores, **keeps non-ASCII** (CJK display names are common), and **never starts with a digit** |

⚠️ **Why naming is a run-time thing**: widgets are named by **display name** (**not object name** — object names are
generated). The compiler **declares variables by it** and the run time **resolves bindings by it** — **the two agreeing is
the whole contract**. A second, **subtly different** copy on the editor side is exactly where "**the binding reports
success but comes back empty**" comes from.

## What is under the parked state

**What "parked" is** (not drawn, behaviours not running, held by the manager, the `IsWidgetParked` query) is on
[Scripting](../tools/scripting.md). The implementation combines a widget state bit with the manager's timestamped
registration entries:

| Part | Where | Role |
| --- | --- | --- |
| `bParked` (a bitfield, `false` by default) | `DreamWidget.h` | The widget's own "**waiting for a parent**" bit |
| `FDreamParkedWidgetEntry` | `DreamUIManager.h` | One entry: the widget pointer + `ParkedAtSeconds` |
| `ParkedWidgets` (`TArray`) | `DreamUIManager.h` | The list the manager holds |

⚠️ **`bParked` is deliberately a bit of its own, not a state of `bWidgetActive`** — borrowing `bWidgetActive` would make
`SetWidgetActive` **silently do nothing** during the window in which the caller is setting the widget up. So the bit means
"**waiting for a parent**", **not** "which stage of life it has reached": **a parked widget is registered, and has already
had `BeginPlay` once the world is running**.

**Parking happens in two places**: when `ConstructWidget` finishes making a widget, and when `RemoveChild` takes a child
off. ⚠️ A source comment explains **why not in the general detach path**: **a move between two parents also detaches
half-way**, and parking there would switch behaviours **off for an instant in an operation that is not a removal**.

What takes a widget out of the lot is `UnparkWidget`, which **runs on every attach** and **returns `false`** for a widget
that was never parked — **the normal case**.

**"Behaviours not running" comes down to one test**: in `CalculateWidgetActive`,
`if (!bWidgetActive || bParked) bResultActive = false;` — **while `bParked` is true, the whole subtree counts as
inactive**, so it is not drawn, not laid out, and its behaviours get `OnDisable`.

⚠️ **Do not mix up the two lists**:

| List | Holds | Cleared by |
| --- | --- | --- |
| `ParkedWidgets` | Widgets waiting for a parent after creation **or removal from a parent** | `UnparkWidget` on attach; world teardown; or the sweep below |
| `FreeRoots` | **Registered tree roots that no host holds** | The manager's own pool; **none of them counts as a leak** |

## The sweep and leak detection

The parking lot has an optional **leak probe**, **off by default**: `UDreamUISettings::ParkedWidgetLifetimeSeconds`,
default **`0.0f`** (**`0` turns the check off, and is the default**).

⚠️ It is a **development-time leak detector, not a lifetime policy** — a **legitimate slow path** (one waiting on an async
load, say) **looks exactly like a leak** from here, and destroying such a caller's widgets **is making your own bug**.

The sweep runs **every frame** (it is the first thing in `TickDreamUI`). The key is the comment **"Destroying is the
point"**: the widget is **registered**, and simply letting go would have GC collect a widget **still in the registered
state**, a path that prints a "not destroyed by its owner" error **from a call stack unrelated to the cause**. So the
sweep **actively destroys** and warns, through `DestroyWidget()` — which unregisters and ends play **in the right order**,
so a widget **never** reaches `BeginDestroy` while still registered.

## The presenter base class

`UDreamWidgetPresenterComponentBase` is a **`USceneComponent` + `IDreamWidgetTreeHost`**,
`UCLASS(Abstract, ClassGroup=DreamGUI, Blueprintable, BlueprintSpawnableComponent)`.

⚠️ **Abstract on purpose**: `LoadWidget()` is `PURE_VIRTUAL`, so an instance of this class **asserts the moment it
registers** — without `Abstract`, the Add Component menu would list it, and **picking it there would be a crash rather
than an error**. What you add is the concrete subclass [`UDreamWorldWidgetComponent`](world-space.md).

It carries out the host contract as `OwnedTree` (`Transient, NonTransactional, DuplicateTransient, TextExportTransient`).
**`NonTransactional` has its reason too**: transactions record even `transient` properties, and an undo/redo that brings
the component back would write back **the tree it held at that moment** (a destroyed one, or none where a live tree
exists) — **which tree the component holds is the component's business, not undo's**.

Two tests:

| Test | Answers | Why it is needed |
| --- | --- | --- |
| `IsLoadedWidgetCurrent()` | Whether the tree already loaded **is** the one a rebuild would make now | In the editor, changing **any property** of the host actor triggers a reregister, and **a reregister is not a teardown** |
| `IsInAWorldThatRunsTrees()` | Whether a tree built now would land in **a world that runs trees** | An `Inactive` or untyped world is a package being made / preloaded, and a tree built there **has no manager** |

**Presenter-level animation forwarding**: `WidgetOpacity` / `WidgetOffset` / `bWidgetVisible` (all `Interp`, **keyable
from a Level Sequence**). Each setter **writes through to the root widget** (opacity through `RenderOpacity`, the offset
through a **render-only translation**, **neither touching layout**). `NotifyWidgetLoaded` then tells every Level Sequence
player in the world to **re-resolve its bindings** — otherwise a custom widget binding evaluated before the tree existed
would keep failing.

## When it does not serve

- **Parking detection is off by default; do not use it as a lifetime policy** — left on in a release it **kills callers
  that are legitimately waiting on an async load**.
- **`ParkedWidgets` and `FreeRoots` are not the same thing** — when debugging, first work out which list it is in.
- **`CountWidgets` does not cache and walks every time** — cache the result yourself for frequent calls.
- **The presenter base cannot be added** — `LoadWidget` is `PURE_VIRTUAL`, and an instance asserts as soon as it
  registers.
- **Parent pointers are not serialized** — any path that makes a tree around the attach functions must call
  `RebuildParentLinks` before registering.
