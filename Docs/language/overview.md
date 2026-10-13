---
title: "Overview"
description: "What a .dui file is, where it lives, the statements it is made of, and how it becomes a widget Blueprint's hierarchy."
---

A `.dui` file is the source of a DreamGUI widget Blueprint: a tree of widgets, their properties, their bindings and their
animations, as text. The Blueprint names the file as its *Source File*, compiling the Blueprint reads the file, and the
designer writes its edits back into the file. This page is the reference for writing one by hand.

## Where a file lives

Under a `DUI/` directory: the project's (`<Project>/DUI/`) or an enabled plugin's (`<Plugin>/DUI/`). Not under
`Content/` -- a `.dui` is source, not an asset.

A path to another file -- in a Blueprint's Source File, or after `use` -- is written one of three ways:

| Spelling | Means |
|---|---|
| `Panels/Settings.dui` | Relative to a `DUI/` root: the project's first, then each plugin's, the first that has the file. |
| `Plugin.MyPlugin:Panels/Settings.dui` | That plugin's `DUI/` directory. |
| `D:/Work/Proj/DUI/Panels/Settings.dui` | Absolute, used as written. |


## File structure

A file is a list of statements. A statement ends at the end of its line or at a `;`. Comments are `// …` to the end of
the line and `/* … */`.

At the top level a file holds:

| Statement | Purpose |
|---|---|
| `class /Game/UI/WBP_Settings` | The Blueprint this file is the source of. At most one. |
| `use …` | Another file or class made available to this one. See [`use`](use.md#use). |
| `resources { … }` | Named constants. See [Resources](styles-and-resources.md#resources). |
| `props { … }` | Properties this file's class declares. See [Writing a component](components.md#writing-a-component). |
| `events { … }` | Events this file's class raises. |
| `viewmodels { … }` | The view models this file's class holds. See [View models](viewmodels.md#view-models). |
| `style Name { … }` | A named set of lines. See [Styles](styles-and-resources.md#styles). |
| `timeline Name { … }` | An animation. See [Timelines](timelines.md#timelines). |
| one node | The root of the tree. |

The order of these is free: a style may be declared below the node that wears it. A file compiled into a class has
exactly one root node (DUI2006). A file with no root is a *library*: it can be `use`d, and it cannot be compiled.

The `class` line does two jobs. It says which Blueprint the file is for (a compile into a different one warns,
DUI6003), and its path is the namespace of every localized string in the file, which keeps the keys stable when the
file is renamed. Without one the file's own name is the namespace.
