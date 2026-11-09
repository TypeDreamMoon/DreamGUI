---
title: "Designer write-back"
description: "The designer is a front end for the .dui — which line an edit is written into, how a hand-written file is left alone, and which edits it refuses (DUI7xxx)."
---

## What the designer writes back

The designer is a front end for the file: a change made there is a change to the text, written into the line that
holds the value -- or a new line after the node's others -- with everything else byte for byte as it was.

**Values.** Numbers, strings, tuples, colours, enums and asset references are written as the literals the language
reads. A list or a set is written as `[a, b]` and a map as `{ key: value }`, the whole container replacing its line;
a set or a map that only came back in another order is no change, and a changed one keeps the file's order with new
entries after it. An element that is a struct with no short form -- a layer style's drop shadow, an
adjustment -- is the quoted text of its fields that differ from the struct's defaults, as the builder reads it:
`Style.DropShadows = ["(Angle=45.000000,Distance=8.000000)"]`. A reference to another widget of the hierarchy (an
explicit navigation target, a toggle's group) is written as that node's bare id; one into another file's component has
no id here and is refused.

A turn -- made with the 2D view's rotate handle, the 3D view's gizmo or the details panel -- is written as the euler,
`RelativeRotationEuler = (0, 0, 30)`: pitch, yaw and roll in degrees, a turn in the canvas plane being the roll,
clockwise for a positive one. The quaternion the asset keeps has no spelling and is never written.

A value a node takes from its **style's** `+ Component` line is written as an override on the node: a
`+ Component { … }` line of its own holding the changed value. The style is shared and stays as it was.

`@fill N` given a new weight has the number replaced; a bare `@fill` given another `SizeRule` becomes
`@slot SizeRule = …`; and `@fill 2` given another rule becomes its two lines, `@slot SizeRule = …` and
`@slot FillWeight = 2`.

**Structure.** A widget added in the designer is written with the type this file would use: its `use … as` alias when
the class has one here, a container type for a plain widget that lays out children (`VerticalBox Column`). One dropped
into a named slot of a component instance is written into that slot's fill, `slot Detail { … }`, which is written first
when the instance has none. Removing a widget takes its whole block; moving one lifts its text and puts it back in front
of the sibling it now stands before; a rename is written as the new id with `(was: OldId)`, so the next compile carries
the variable, the bindings, the animation paths and the localization keys the old name owned. An unnamed node renamed in
the designer gets its first id, written after its type. Behaviours are `+` lines added, removed and moved; a new panel
or visual class rewrites the node's type or its `+` line, and lines the new class has no property for are taken out.

A row of a `rows` table is one line: removing it takes the line, moving it lifts the line to before another row of
the same table, and duplicating it copies the line (the copy's key is bumped until the author gives it its own).

**Statements.** A binding from the Bind control is written as `Property <- Function()`, replacing the property's own line
when it has one; *Remove Binding* takes the line out, and *Unbind and Set to This Value* writes `Property = value` in its
place. A two-way binding is `Value <-> Variable` (or a viewmodel member, `Value <-> Settings.Volume`), offered only on a
control that announces its value through `OnValueChangedBP`. A route from the Events rows is `Event -> Handler`: on the
node's line, or in the node's own `+ Component` block for a behaviour the node writes as one. These are written
straight into the document as one line each, in the gesture's transaction.

**Animations.** An edit in the animation editor to a `timeline` block's animation is written into the block: keys' times
and values, the ease from one key to the next, event keys, the duration, a track added or removed as a line. A track
whose keys did not change keeps its line byte for byte; comments and the `loop` line stay as they are.

**What is refused**, with the reason, before anything changes -- a few things have no line to write into, and the
designer says so (DUI7004) instead of inventing one:

- the visibility of a widget an `if` shows or hides -- change the condition, or move the widget out of the branch -- and
  a binding or route written over a line the front end made rather than read;
- the template of a `for` or an `each` removed or moved out of its loop: a loop with nothing to repeat does not build;
- on a `rows` table: renaming a row, giving it a `+` block, moving it out of its table, or placing a node beside the
  table (the place would be inside it);
- a value whose type or content has no spelling (DUI7003, today only non-finite floats among values that have one);
- in the animation editor, a key with tangents of its own or stepped interpolation -- write `timeline Name external` to
  hand the animation to Sequencer for good.

If a source file is read-only or a write fails, the document keeps the edited text in memory. A later flush retries
that write even when no value changed. Compile retries it too, and reports DUI6023 while it still cannot write; the
current hierarchy and edits are retained. Check the file out or resolve the write failure, then compile again.

### From designer values back to text

`FDreamUITextWriteBack` turns designer edits into `.dui` changes. Its central rule is to **compare values, not source
spellings**. `FDreamUITextPatcher` edits text slices; giving it every property the designer might have touched would
rewrite unchanged values whenever the printer's formatting differs from the author's. For example, `(400,240)` and
`(400, 240)` describe the same value.

A flush therefore asks whether rebuilding the current text would produce the value held by the live tree. It builds
a second tree from that text with the compiler's own `FDreamUITextBuilder`, then compares the values. It does not
maintain a separate interpretation of the language for write-back.

One flush is one undo step, or no step when nothing changed. A no-op does not call `SetContent` or create an empty
transaction. Undo marks the tree for regeneration **on the next tick**: the notification arrives inside
`FTransaction::Apply` while it is still restoring objects, so other widgets in the transaction may not yet have
their restored values.

### Where the text lives: `UDreamUIDocument`

For text-backed widgets, the text is the source of truth and the designer is its graphical front end.
`UDreamUIDocument`, a `UCLASS(Transient)` `UObject` in `DreamGUIEditor`, holds the full text of one `.dui` file so
Unreal's transaction system can undo it.

The `UObject` matters because undo restores objects. If the text were outside the transaction, Ctrl+Z could restore
the widget while leaving the new value in the file. A recompile, a reopen or the next unrelated edit would then
regenerate the tree from that file and bring the supposedly undone change back.

Callers must preserve three contracts:

1. **The caller owns the transaction.** `SetContent` calls `Modify()` but never opens a transaction itself. It reports
   a missing transaction instead of silently producing a non-undoable edit.
2. **Memory is authoritative; disk may lag.** A failed write, such as a read-only file that has not been checked out,
   keeps the edit in memory, reports the failure and retries on a later flush.
3. **`FlushToDisk` never clears the read-only bit.** Check out the file through source control rather than making it
   writable behind the source-control system's back.

There is **one document per file, not per editor window**. Two toolkits holding separate documents for the same file
would have independent text and disk hashes: an undo in one could look like an external edit to the other, causing
the windows to keep reloading and overwriting each other's state.

`FDreamUIDocumentRegistry` prevents that by using the normalized absolute path as its key. The registry holds weak
references; callers retain documents through `FDreamUIDocumentHandle`, a move-only, non-copyable scoped handle.
The document API and this ownership contract are declared in
[`Source/DreamGUIEditor/Public/Text/DreamUIDocument.h`](../../Source/DreamGUIEditor/Public/Text/DreamUIDocument.h).
