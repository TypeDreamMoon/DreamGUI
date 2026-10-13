---
title: "Writing a component"
description: "A widget Blueprint used inside other trees, written in .dui — props, events and emit, default and named slots, and how a host sets, hears and fills them."
---

## Writing a component

A component is a widget Blueprint used inside other trees. Written in `.dui` it declares, beside its tree, what its
hosts may set and hear.

### `props`

```
props {
    Text   Label
    Text   Value
    Number ValueIndex = 0
    Enum   /Script/MyGame.ERowKind Kind = Cycle
}
```

One property per line: a type, a name, and optionally `= default`. The types are `Text`, `String`, `Number`, `Integer`,
`Bool`, `Color`, `Vector2`, `Asset`, `Class`, and `Enum` followed by the enum's path. Each becomes a Blueprint variable
of the class, editable on instances; a C++ parent's property of the same name and type is used instead of declaring one.

A host sets them like any property, and the component binds them:

```
// in the component
Text LabelText { Text <- Label }

// in a host
Row Audio { Label = "Audio" }
Row Track { Label <- GetTrackName() }
```

A host may bind a prop as well as set it. A prop has no setter -- it is a Blueprint variable -- so the binding writes it
straight into the instance when the value changes and announces it (props are FieldNotify), and the component's own
bindings on it show the change. This is the one setter-less property a binding accepts: a user widget's own variable,
one way (`<-`, not `<->`).

A line that is not `Type Name` or `Type Name = value` is DUI2016; one name twice is DUI3019; at compile, a type with no
Blueprint pin is DUI6008, a name the class already uses DUI6009, a default its type cannot hold DUI6013.

### `events` and `emit`

```
events {
    Picked(Number Index)
    Closed
}
```

Entries end at a line break or a `;`, so a short list fits on one line: `events { Picked(Number Index); Closed }`. Each
becomes an event dispatcher of the class. The component raises one from any route with `emit`, its arguments written as
binding expressions:

```
+ UIButton { OnClick -> emit Picked(ValueIndex) }
```

and a host routes it like any event:

```
Row Audio { Picked -> HandleAudioPicked }
```

DUI2017 is a malformed entry and DUI3020 a duplicate. At compile, `emit` of an event not declared here is DUI6010,
arguments that do not match its parameters DUI6011, an `emit` that cannot be compiled where it stands (inside a loop body,
or on an event whose parameters Blueprints cannot take) DUI6012, and an event named like another member of the class
DUI6014.

### Slots

A component opens a hole its host fills with `slot`:

```
slot Footer
slot Rows default
slot Detail : DetailPanel {
    + VerticalBox { Spacing = 15 }
    @slot SizeRule = Fill
}
```

- `default` marks the slot that content goes to when the host names none. One per file (DUI3022).
- A block, or a style, lays the hole out: components, properties and slot lines -- never children, since what goes in
  it is the host's (DUI2019). A slot with a layout container takes several widgets; one without takes one.
- `(was: OldName)` renames a slot, as it does a node.

A host fills the **default** slot by nesting, and a **named** one with a `slot` block of its own inside the instance:

```
ListPage Page_2 {
    Row Item_0 { Label = "Warped Wire" }        // into the default slot
    slot Detail {                               // into the slot named Detail
        Text Note { Text = "A length of wire bent out of shape." }
    }
}
```

A fill holds widgets and nothing else. A named slot holds one widget -- put several in a container. A fill outside a
component instance is DUI5019; one naming a slot the component does not declare is DUI5020.
