---
title: "if and for"
description: "if / else if / else switches which branch is shown, for and each repeat one template from the class's data, and the rules both keep."
---

## `if` and `else`

```
if HasSave() {
    Row Continue { Label = "Continue" }
} else if IsLoading() {
    Native.Throbber Spinner { }
} else {
    Text NoSave { Text = "No save data" }
}
```

The condition is a binding expression. Each branch holds widgets. Every widget of every branch becomes a child of the
enclosing node, in order, with a `Shown` bound to "this branch is the one taken": `HasSave()` for the first,
`!HasSave() && IsLoading()` for the second, `!HasSave() && !IsLoading()` for the last. Switching is a change of
visibility: a hidden branch is collapsed, not destroyed, and keeps its state.

- A branch holds widgets only: a property, a slot line or a `+` block in one is DUI2018, and so is a slot or a loop.
- A widget in a branch that writes a `Shown` of its own is shown when its branch is taken and its own condition holds.
- `if` belongs inside a node; at the top of a file, or an `else` with no `if` before it, is DUI2018.
- `else` may stand on the line the `}` closed or on the next one.

`if` and `else` are keywords only where they lead a branch: a property named `if` is still a property.


## `for` and `each`

Both repeat one template widget per item of a source. The source is a function, `GetOptions()`, or a variable,
`Options` -- a FieldNotify array refreshes the copies when it changes -- or either through a member path,
`Inventory.Items`, `Inventory.Filtered()`, refreshed when anything along the path changes. Inside the body,
`Prop <- Item.Member` binds a property of each copy to a member of its item, and `Event -> Item.Func()` routes an event
of each copy to a function of its item:

```
for Item in Inventory.Items {
    HorizontalBox {
        Text { Text <- Item.Name }
        Native.Button { OnClicked += Item.Use() }
    }
}
```

When an item announces a change of a member a copy shows (the item implements `INotifyFieldValueChanged` and the member
is FieldNotify), that copy is updated alone; the rest of the list is not touched. A member nothing announces is read
again when the list refreshes.

### `for`: copies in the panel

```
VerticalBox Options {
    Text Header { Text = "Options" }
    for Option in GetOptions() {
        Row { Label <- Option.Label  Kind <- Option.Kind }
    }
    Text Footer { Text = "…" }
}
```

At run time the template stays in the tree, collapsed, and one copy per item takes its place among the host's
children, in item order -- between whatever siblings surround the `for`. Nothing is virtualized and nothing else is
made: the host's own container arranges the copies like any children. Use it for short lists: a settings page's
options, a tab bar.

### `each`: a list view's cells

```
Widget Inventory {
    + UIListView { }
    each Item in GetItems() {
        Row Cell { Label <- Item.Name }
    }
}
```

`each` fills the list view of the widget it is written in: the cells are made and recycled as the list scrolls. Use it
for long lists.

### The rules both keep

- The body is exactly one widget, the template (DUI5021, DUI5012). Wrap several in a container.
- A loop is not the root, and does not nest in another loop (DUI5021, DUI5012). The way to nest is a component whose own
  file has the inner loop, given its list through a `props` entry.
- A `for` needs a host that takes any number of children; an `each` needs a `+ UIListView` (or a recyclable scroll
  view) on its host.
- In the body, the only binding is the single hop `Item.Member`, and the only route to an object is `Item.Func` /
  `Item.Func()`; an expression, a `<->`, or a route to another object there is DUI5014.
- The source must exist on the class (DUI6006) and be an array of objects (DUI6007).
- When the array's element class is known (`TArray<UItemVM*>`, not `TArray<UObject*>`), `Item.Member` must be a member
  of it (DUI6021), and `Item.Func` a function of it that takes nothing or exactly what the event sends (DUI6022). With an
  array of `UObject` the item's class is only known at run time, where a line that does not fit is skipped.
