---
title: The VS Code extension
description: Installing DreamGUI Language Support, what it does, where its completion data DUI/.dui-symbols.json comes from, the bridge to the running editor, and its commands and settings.
---

[DreamGUI Language Support](https://github.com/TypeDreamMoon/dreamgui-language-support) is the VS Code extension for
`.dui`: highlighting, completion, hover, navigation, rename, formatting, diagnostics, and a bridge to the running Unreal
editor. It never guesses at the language — its completion data comes from the symbol file the plugin exports, and its
semantic verdicts come from the compiler's own diagnostics.

## Installing

The extension's id is `typedreammoon.dreamgui-language-support`, and it needs VS Code 1.85 or later. It is on the
[VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=typedreammoon.dreamgui-language-support):
search the Extensions view for *DreamGUI Language Support*, or:

```powershell
code --install-extension typedreammoon.dreamgui-language-support
```

The `.vsix` is also on the repository's GitHub releases, for `code --install-extension
dreamgui-language-support-<version>.vsix` or *Extensions: Install from VSIX...* inside VS Code.

Up to 0.9.1 the id was `typedreammoon.dreamui-language-support` (the Marketplace had that name taken, so 0.9.2 renamed
it). VS Code treats it as another extension, and with both enabled every diagnostic and completion comes twice:
uninstall the old one after installing the new; the extension also says so when it finds it.

### Opening the workspace from the editor

*Tools ▸ Open DreamUI Workspace (VSCode)* (or *DreamUI Workspace* in the level toolbar's *Dream* dropdown, which is the
same thing):

1. refreshes the symbol export, so the completion data is fresh the moment VS Code opens;
2. rewrites `DUI/DreamUI.code-workspace` — the project's `DUI/` and every enabled plugin's `DUI/` root, the `*.dui`
   association pinned, and the extension recommended; when the project has no `DUI/` directory yet, this button is
   allowed to be the first thing that makes one;
3. opens it in VS Code, falling back to the system's default editor and then to Notepad.

Opening through this workspace rather than a bare folder is what lights up the extension's workspace features (the
cross-file index, resolving `use`). *Rebuild DUI* in the same menu recompiles every text-backed widget Blueprint in the
project, loading the ones that are not loaded, after asking first.

## What it does

| Feature | What you get |
| --- | --- |
| Highlighting | The whole grammar, binding expressions, member paths, `<->`, `+=` / `=` routes, `use`, the component syntax, `viewmodels` blocks, `rows` tables and `timeline` blocks included; semantic highlighting by identity: a node id as the member variable it becomes, styles as classes, resources as read-only constants |
| Completion | Built-in tags, layout containers, component classes, per-class properties, enum values, slot properties, resource types, `@` resource references, `->` event names; a component instance's props, events and slots; `use … as` aliases and namespaces; the types in a `viewmodels` block and the four sources after `=`; after `Player.`, the view model's members (properties with their type, whether they are FieldNotify and whether `<->` can write them back; functions with their parameters), filtered by what the path is for — a route offers functions, `<->` writable properties, a loop source arrays — and followed on through object members (`Player.Stats.`) and into a loop over a view model's list (`Item.` in `for Item in Player.Items`) (since 0.10.0) |
| Hover | Property types and enum values, UPROPERTY tooltips and pasteable class defaults, what an alias resolves to, an `emit`'s signature, the id an unnamed node compiles to |
| Outline and navigation | An outline of the widget tree (with `if` / `else` branches, loops, slots); go to definition for `@Name`, styles, aliases, `use` targets, `emit`, props, namespaced names; Ctrl+T over every id, style and resource in every `.dui` |
| Rename and refactors | F2 renames a style or resource with every use; renaming a node id writes the `(was: OldId)` clause so the next compile migrates graph references, bindings and animations; extract to a resource, extract to a style, inline a style |
| Formatting | Indentation from block depth, one space around `=` and the arrows, `}` on its own line — the same spelling the designer's write-back prints, so the two never fight; the result is re-lexed, and a format that would change a single token is refused |
| Diagnostics | The compiler's own codes and wording at the compiler's own severities: the lexical codes, the structural codes one file settles, and symbol-driven checks. The compiler stays the authority: anything it might accept is a warning here at most |
| Explanations | A diagnostic's code, in Problems and in the hover, links to its entry under [Diagnostics](../diagnostics/index.md) (since 0.9.1); offline, every diagnostic offers an "解释 DUInnnn" (explain) action that opens the bundled code-table entry: what it means, why it fired, how to fix it |
| Quickfixes | Declare an unknown resource (DUI4007), create an unknown style (DUI3004), add the `use` for a style or resource that lives in exactly one other file |
| Also | Colour chips and a picker on every `#hex` literal; snippets for the component syntax; *DreamUI: New .dui File*, which writes the same starter as the editor's *Create Source File...* |

What the language **deliberately does not have** (compile-time conditions, macros, variables, pseudo-states, flag enums
written as `A | B`, a ternary, array literals, …) the extension will not complete either; the reasons are in the
extension's README and in [the .dui overview](../language/overview.md).

## The symbol file: DUI/.dui-symbols.json

Completion offers exactly what the compiler accepts because the two read **one list**. The editor module dumps what the
compiler knows into the project's `DUI/.dui-symbols.json`:

- **tags** from the builder's own table (registry tags such as `Native.Button` included);
- **properties** from the same reflection rules the write-back sweeps with;
- **enum values** from their `UEnum`;
- and the layout containers with their properties, events, property tooltips and class defaults;
- the classes a `viewmodels` line can name (subclasses of `UDreamViewModel`, and any class that is not a widget and
  implements `INotifyFieldValueChanged`) with their members — what completion after `Player.` reads (version 2 of the
  file; an older export completes nothing there, rather than guessing).

It is written at two moments: **on editor startup** (once classes exist), and **on demand**, by a console command:

```text
DreamUI.ExportSymbols
```

It writes only into a project `DUI/` directory that **already exists**: a project that has never used a `.dui` should
not wake up owning one. With no `DUI/`, the command logs that nothing was written.

No symbols file yet? Open the project in the Unreal editor once, with a `DUI/` directory present. Everything
grammar-driven works without it, and the status bar says which mode you are in — a missing symbols file is a visible
warning with instructions, not a silent downgrade. After the file changes, *DreamUI: Reload Symbols* reads it again.

## The bridge to the editor

While the editor is running, the extension reaches what one file's characters cannot see:

- **Compiler diagnostics, live.** Every compile of a text-backed class writes its verdicts to
  `DUI/.dui-diagnostics.json`, and they appear in the Problems panel as `dui-compiler` — the semantic layer: unknown
  properties, bad values, missing binding functions. A clean compile writes an empty array, which is what clears that
  file's squiggles.
- **Requests and responses.** A drop-folder protocol in `Saved/DreamGUI/Bridge/`: the extension drops a request, the
  editor polls, handles it and writes a response. `<-` and `->` completion ask the editor what the class really
  declares; `<->` completion lists the class's variables (FieldNotify ones first — the rest are polled every frame);
  typing `/` offers every nestable widget class; `/Game/...` paths are links that open the asset in the editor's Content
  Browser.
- **Reveal in Unreal Designer** (Ctrl+Alt+R) opens the designer and selects the node under the cursor; **Compile This
  File** (Ctrl+Alt+B) compiles now, verdicts in Problems. The designer is this language's preview surface — the bridge
  just takes you there.
- **The other way round**: *Reveal in VS Code* on the designer's toolbar or in the hierarchy's context menu makes the
  editor write `Bridge/reveal-to-editor.json`, and VS Code jumps to the node's line.

**Note:**

  One project, one editor. Two editors would steal each other's requests — a case that is recorded, not defended against.

## Commands and settings

| Command | Key | What it does |
| --- | --- | --- |
| DreamUI: Reload Symbols (.dui-symbols.json) | | Read the symbols file again |
| DreamUI: New .dui File | | Write a starter `.dui` (also on a folder's context menu) |
| DreamUI: Reveal in Unreal Designer | Ctrl+Alt+R | Open the designer and select the node under the cursor |
| DreamUI: Compile This File | Ctrl+Alt+B | Have the editor compile this file now |
| DreamUI: Clear Bridge Caches | | Drop what was cached from the editor's answers |

| Setting | Default | Meaning |
| --- | --- | --- |
| `dreamui.symbolsPath` | empty | An explicit path to `.dui-symbols.json`. Empty searches each workspace folder's `DUI/` directory and the folders above the open file |
