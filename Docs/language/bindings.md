---
title: "Bindings and routes"
description: "The three arrows that connect the tree to the class's code — a binding drives a property, a route calls a handler, a two-way binding mirrors a variable — member paths, when a binding updates, and Shown, which every widget has."
---

## Bindings and routes

Three arrows connect the tree to the class's code.

### `<-` drives a property

```
Text <- GetTitle()
RenderOpacity <- Fade() * 0.8
Shown <- Count() > 0 && !IsLocked()
```

The right side is an expression, re-evaluated as the class runs:

- a call to a function of the class, `Func()` or `Func(a, b)`;
- a variable of the class, `Volume` -- including a `props` entry and a `viewmodels` entry;
- a member path through an object variable, `Player.Name`, `Player.Stats.Title`, or a call at its end,
  `Player.FormatGold(Player.Gold)`;
- a literal: a number, a string, `true`, `false`, or `@Resource`;
- operators, loosest first: `||`; `&&`; `==` `!=`; `<` `<=` `>` `>=`; `+` `-`; `*` `%`; and the prefixes `!` and `-`.
  Parentheses group. There is no `/` -- it belongs to paths and comments -- and no `? :`; division and choices go in
  a function.

A bare `Func()` binds that function directly. Anything richer is compiled into a generated function. The property
needs a setter (DUI5005) -- a component's prop excepted, see [`props`](components.md#props) -- and only whole properties of the widget, its visual or a behaviour can be bound (DUI5008):
not a slot property, not a field inside a struct. A function the class lacks is DUI5004; an expression the compiler
cannot lower is DUI5011. A path segment its class does not have, or cannot show to Blueprint, is DUI5023; a path that
goes on past a value that is not an object is DUI5024.

**When a binding updates.** The compiler records every variable and member the expression reads. When each of them is
FieldNotify -- the variable on this class, and every member along a path on an object that implements
`INotifyFieldValueChanged` -- the binding updates when one of them announces a change, and costs nothing in between.
Otherwise it is read every frame. A call with arguments (`Format(Gold)`) may read anything, so a binding holding one is
always read every frame; a view model offering the formatted value as a member is the way out. While an object along a
path is not set, the binding is not evaluated and the property keeps the value it had.

`DreamUI.Binding.Dump` lists every live widget's bindings, how many update on a change and how many every frame, and
why. `DreamUI.Binding.ForcePoll 1` reads them all every frame, which tells a change nobody announced from a wrong
binding.

### `->`, `+=` and `=` route an event

```
OnClicked -> HandleConfirm
OnClick -> emit Picked(ValueIndex)
OnClicked += Settings.Apply()
OnValueChanged += Settings.SetVolume(Value)
OnValueChanged += Settings.SetVolume
OnInit = HandleInit
```

The left side is an event of the widget, its visual or a behaviour: a `BlueprintAssignable` multicast delegate, a
`DreamUIEventDelegate` property, or a single-cast delegate. The operator says what happens to the event's other
listeners:

| Operator | Means | Takes |
|---|---|---|
| `+=` | Adds this listener beside the others. | A multicast delegate or a `DreamUIEventDelegate`. |
| `=` | This is the event's one listener. | A single-cast delegate. |
| `->` | Either, by the event's kind. | Any event. |

`+=` on a single-cast delegate, which cannot hold a second listener, and `=` on a multicast event, which would have to
drop the listeners the control and the Blueprint graph added, are DUI5025.

The right side is one of:

- a function of the class, no parentheses (`HandleConfirm`): it takes what the event sends;
- `emit` of an event the class declares, with its arguments;
- a function of an object the class holds, by a member path (`Settings.Apply`). With parentheses, they are its
  arguments -- expressions over the class, with the event's own parameters in scope by name (`Settings.SetVolume(Value)`);
  without them, it takes nothing or exactly what the event sends. Nothing is called while the object is not set. A
  function the object lacks is DUI6018; arguments that do not fit, DUI6019.

DUI5010 is an event that is not one; DUI6004 and DUI6005 a handler the class lacks or whose parameters do not match.

### `<->` mirrors both ways

```
Value <-> Volume
Value <-> Settings.MasterVolume
```

The property follows the variable, and a change the control makes is written back into it. The variable must be a
FieldNotify variable of the class, or a member of an object it holds. Writing a member back calls the object's
`Set<Member>` when it has a BlueprintCallable one, and otherwise writes the member and announces it on that object. A
member that cannot be written either way is DUI6020.

### `Shown`

Every widget has `Shown`: `true` is Visible, `false` is Collapsed. It keeps nothing of its own -- reading it reads
Visibility, writing it writes Visibility -- and it is what a condition binds:

```
Widget Detail {
    Shown <- HasDetail()
}
```

A widget that is Hidden or hit-test invisible and stays shown keeps that state.

### Reflection-based binding at run time

The arrows above are language constructs: the compiler resolves them and records their dependencies, including
FieldNotify members and view models. `UDreamDataBinding` provides a separate, reflection-based one-way binding at
run time. It is a `UDreamUIBehaviour` component in the core `DreamGUI` module, connecting compatible `UPROPERTY`
values on two `UObject`s by property name. It does not require their types to be known to the `.dui` compiler.

| Field | Default | Meaning |
|---|---|---|
| `SourceObject` / `SourceProperty` | Unset | The object and property to read. |
| `TargetObject` / `TargetProperty` | Unset | The object and property to write. An unset `TargetObject` means the widget that owns this component. |
| `UpdateMode` | `OnAwake` | An `EDreamBindingUpdateMode`: `OnAwake`, `EveryFrame` or `Manual`. |

`ApplyBinding()` is BlueprintCallable. It resolves the named properties through reflection and returns whether the
value could be applied. This path has the cost and constraints of string property names and compatible reflected
types; it is not the compiler's FieldNotify subscription mechanism.

#### Responsive appearance with `UDreamResponsiveBehaviour`

`UDreamResponsiveBehaviour` applies the **first matching breakpoint rule**, using the parent's size, or the widget's
own size when it is a root. Each `FDreamResponsiveRule` contains:

| Field | Default | Meaning |
|---|---|---|
| `Name` | Unset | The rule's name, reported as `ActiveRule` while it is active. |
| `MinWidth` / `MinHeight` | `0` | Inclusive minimum dimensions. |
| `MaxWidth` / `MaxHeight` | `0` | Inclusive maximum dimensions; zero means no upper bound. |
| `Visibility` | `Visible` | Visibility to apply while the rule matches. |
| `RenderOpacity` | `1` | Opacity to apply while the rule matches. |

Rules override the widget's authored appearance. If no rule matches, the component **restores the appearance it
found before the first override**, rather than leaving the previous rule's visibility and opacity in place.
`EvaluateResponsiveRules()` is BlueprintCallable; `GetActiveRule()` reports the active rule.

`ActiveRule` is `Transient`. The private `bHasBaselineAppearance`, `BaselineVisibility` and `BaselineRenderOpacity`
record the appearance temporarily overridden by a rule. They are deliberately not `UPROPERTY` fields: this record
has no meaning across a load, and there is nothing to restore until the component applies another override.

Both classes live in
[`Source/DreamGUI/Public/Interaction/DreamResponsiveBinding.h`](../../Source/DreamGUI/Public/Interaction/DreamResponsiveBinding.h).
The filename covers both runtime binding and responsive rules; there is no separate `DreamDataBinding.h` to include.
