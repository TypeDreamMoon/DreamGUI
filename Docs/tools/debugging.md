---
title: Debugging and measuring
description: Seeing what DreamGUI drew and what it cost without a debugger — DreamUI.Capture, DreamUI.Stats and the Insights scopes, stat DreamGUI, DreamGUI.Memory, the two verification switches, the diagnostic commands, the console variables for A/B runs, and the log categories.
---

Both questions — "what did it draw" and "what did it cost" — can be answered without a debugger.

## What it drew: DreamUI.Capture

```text
DreamUI.Capture [Directory]
```

Writes this world's UI as PNG files: one of the viewport, and one of every root canvas that renders into a target.
Without a directory it writes into a new, time-stamped directory under `Saved/DreamUI/Captures`. A canvas drawn
straight onto the screen has no picture of its own — it is part of the viewport's; capture the viewport.

The same thing is a Blueprint library, [`UDreamUICaptureLibrary`](../Reference/DreamUICaptureLibrary.md):

| Function | What it does |
| --- | --- |
| `CaptureAll` | What the console command does: `Viewport.png` plus `<widget name>.png` for each root canvas that renders into a target; returns the files written |
| `SaveViewportToPng` | The viewport the world is shown in — the scene with every panel on it, as a player sees it |
| `SaveCanvasToPng` | What a canvas that renders into a target draws |
| `SaveRenderTargetToPng` | A render target's pixels, alpha kept |
| `GetCaptureDirectory` | `<Project>/Saved/DreamUI/Captures`, which a relative path is taken from |

Every call reads the picture back from the GPU, and so waits for the render thread to finish what it has been given:
a tool for looking, not something to do every frame. Without a GPU (`-nullrhi`) there is nothing to read; the call
says so in the log and returns false. In a game the viewport is drawn straight into its window and has no texture
between frames, so `SaveViewportToPng` returns false there and says to use the engine's screenshot request.

## What it cost: DreamUI.Stats and Insights

```text
DreamUI.Stats
```

Prints what the frames **since the last `DreamUI.Stats`** cost, stage by stage, and starts counting again — so type it
once before and once after the stretch you want to measure.

| Stage | What it is |
| --- | --- |
| `ManagerTick` | The UI manager's tick |
| `CanvasUpdate` | Canvas updates |
| `Batching` | Batching |
| `DrawCallSubmit` | Draw-call submission |
| `RenderRecord` | The render thread's recording |

Each stage gets milliseconds per frame and a run count. The counters follow, per frame and in all: `BatchesRecorded`,
`VerticesRecorded`, `SectionUploads`, `UploadedBytes`, `DataTextureUpdates`, `GeometryCopies`, `SectionReuses`,
`WidgetsUpdated`, `SectionPatches`, `DrawCallRebuilds`, `InPlaceRefreshes`, the render layers' `RenderLayerMoves` /
`Promotions` / `Demotions`, and for text `TextPaints`, `TextMoveRepaints`, `SmallTextPlacements`, `SharpenSweepTexts`,
`SharpenRepaints`, `CoverageItemsDrawn`, `CoverageGlyphLookups`, `CoverageRastersSync`, `CoverageJobs`,
`CoverageFlushes` and `FontAtlasUploadBytes`.

**In Unreal Insights**, each stage is also a named scope starting with `DreamUI_` (`DreamUI_LayoutPass`,
`DreamUI_UpdateRootCanvases`, `DreamUI_TextLayout`, `DreamUI_RecordScreenSpace`, `DreamUI_WorldSpaceRaycast`, …) on the
CPU channel. The finer scopes that run once per canvas, section or widget are on a channel of their own,
`DreamUIDetail`, off by default — a wall of thousands of widgets runs them thousands of times, each reading the clock
twice, enough for a traced frame to say little about an untraced one. When you want them:

```text
-trace=default,DreamUIDetail
Trace.Enable DreamUIDetail
```

the first on the command line, the second at the console.

**`stat DreamGUI`** is the plugin's stat group: timings for canvas batching, draw-call updates, geometry updates, vertex
transforms, layout, the property-binding poll and the root-canvas update, and two animation-player counts (players
keeping their own time, players updated by the sequencer).

## What it holds: DreamGUI.Memory

```text
DreamGUI.Memory
DreamGUI.Memory Json
DreamGUI.Memory File=<path>
```

Each font's glyph atlas (slices, GPU bytes and the CPU copy, cells, glyphs, face bytes), the sprite atlas pages, and in
every world with a UI manager the canvas meshes' sections and the paint rows. `Json` prints it as JSON instead;
`File=` writes the JSON to a file (relative to `Saved`). The packaged smoke test's probe records its memory report this
way.

## The two verification switches

For a report of something drawn wrong or not drawn:

| Variable | When on |
| --- | --- |
| `r.DreamUI.VerifyPartialPrepare 1` | Every prepare a canvas makes from its last one (making again only what the widgets that asked, came or moved draw) is checked against a prepare of every widget; a difference is an ensure that names the canvas |
| `r.DreamUI.VerifyKeptPointers 1` | Each object DreamGUI **keeps** instead of looking up every frame — a widget's canvas, a canvas's render layers, the UI manager's canvases, an animated property's bound object — is looked up as well where it is used; a disagreement is an ensure that says which |

Both are off by default, because they cost what the shortcuts save (a full prepare, the look-ups). The test host
switches them on in its `Config/DefaultEngine.ini`, so the whole suite runs under them:

```ini
[ConsoleVariables]
r.DreamUI.VerifyPartialPrepare=1
r.DreamUI.VerifyKeptPointers=1
```

The benchmarks (`Tools/Bench`) switch them off for the sessions they measure. Text's incremental layout has a switch of
the same kind: `DreamGUI.Text.VerifyIncremental 1` lays out again from nothing every layout that built on a kept one,
compares the two, logs the first difference and uses the fresh layout.

## Diagnostic commands

| Command | What it does |
| --- | --- |
| `dreamgui.ListPendingWidgets` | Lists widgets created but not yet added to anything (the parked state between `ConstructWidget` and `AddChild`), with how long they have been waiting. Holding them in a named array is what stops them being collected; listing them is what stops that becoming a place things quietly pile up |
| `DreamGUI.Diag.FindTreeBridges` | Lists every reference from an object the current world's levels keep (an actor, one of its components or sub-objects) into a DreamGUI widget tree, and whose tree it is. A play session's copy of the level follows such a reference into the tree |
| `DreamGUI.Text.ShapeCacheFlush` | Empties the text shape cache; its counters are kept |
| `DreamUI.ExportSymbols` | Rewrites `DUI/.dui-symbols.json` (editor); see [The VS Code extension](vscode.md) |
| `DreamUI.Binding.Dump` | Logs every live user widget with property bindings: how many are subscribed (updated on a change), polled every frame, constant and legacy, and for each polled one, the member that keeps it on the poll. See [When a binding updates](../language/bindings.md#bindings-and-routes) |
| `DreamUI.Binding.ForcePoll 1` | Every binding compiled with recorded dependencies is polled every frame instead, for the widgets initialized after it is set. For a value that does not update: it tells "nothing announces its change" from "it never changes". Default 0 |

⚠️ **Not every DreamUI "command" is a console command.** On the editor side, two entry points from the same name family
go through **the toolbar and the Tools menu**, not the console: `DreamUI.OpenWorkspace` (rewrites and opens
`DUI/DreamUI.code-workspace`) and `DreamUI.RebuildAll` (recompiles every text-backed widget Blueprint in the project,
**after a confirmation dialog**). Each has two ids, one for the toolbar and one for the Tools menu
(`DreamUI.OpenWorkspaceTools` / `DreamUI.RebuildAllTools`); see [Designer](designer.md).

Tracing variables, which log when on: `dreamgui.LayoutTrace 1` (every panel arrange and every rect it commits),
`dreamgui.ScrollBoxTrace 1` (every scroll-box drag delta and physics tick that changes state), and
`dreamgui.DumpMaterialDraws 1` (which branch each screen-space material draw attempt took, until set back to 0).

## Switches for A/B runs

These exist to measure with: side A, switch, side B, in one session. The defaults are the shipped behaviour.

**Render layers and canvases**

| Variable | Default | Meaning |
| --- | --- | --- |
| `r.DreamUI.RenderLayers` | 1 | A widget whose render transform keeps changing becomes a render layer; its canvas moves the geometry under it on the GPU |
| `r.DreamUI.RenderLayerPromoteFrames` | 2 | A widget whose `RenderLayer` is Auto becomes a layer once its render transform changed on this many frames in a row |
| `r.DreamUI.RenderLayerDemoteFrames` | 60 | It stops being one after holding still this many frames, and batches with the rest of its canvas again |
| `r.DreamUI.RenderLayerMaxPerCanvas` | 8192 | How many layers a canvas makes for widgets set to Auto |
| `r.DreamUI.ParallelLayerRowsMin` | 2048 | Past this many layers, a canvas works out where they stand and writes their table rows on several threads |
| `r.DreamUI.QuietRenderLayers` | 1 | A moving layer whose widgets need no word of its moves is not walked into |
| `r.DreamUI.RefreshMovesInPlace` | 1 | A canvas whose widgets only moved keeps its draw calls and refreshes vertices and bounds |
| `r.DreamUI.DeferTransformNotifications` | 1 | A widget's move is announced once, at the UI manager's next flush, rather than at every write |
| `r.DreamUI.AsyncVertexTransformMinVertices` | 256 | A geometry with at least this many vertices is transformed on a worker |
| `r.DreamUI.ParallelVertexRefreshMinCanvases` | 32 | With this many canvases refreshing vertices in a frame, the task graph's workers help |
| `r.DreamUI.ParallelLayerPlacementMinCanvases` | 32 | The same, for placing render layers |
| `r.DreamUI.ParallelRaycastMinCanvases` | 32 | Past this many root canvases, a world-space raycaster runs the cheap bounds tests in parallel |
| `r.DreamUI.ParallelRaycastMinVisuals` | 2048 | Past this many visuals on a canvas, a ray looks at them on several threads |
| `r.DreamUI.EditorWorldTickIntervalDuringPlay` | 0.5 | While a play session runs, the level being edited updates its UI at most this often, in seconds |

**Animation**

| Variable | Default | Meaning |
| --- | --- | --- |
| `DreamUI.Animation.Ticker` | 1 | Animation players are ticked by DreamGUI's ticker, which ticks only the ones with something to do |
| `DreamUI.Animation.DirectEvaluation` | 1 | An animation of plain property tracks is evaluated straight from its channels, not through the sequencer's entity system |
| `DreamUI.Animation.LitePlayer` | 1 | Such an animation keeps its own time between loop boundaries, and a player that is not playing skips the per-frame update |
| `DreamUI.Animation.ReusePlayers` | 1 | A player whose instance ended is kept to play that animation's next instance |
| `DreamUI.Animation.MaxSparePlayers` | 2 | How many spare players each animation component keeps |
| `DreamTween.MaxStepSeconds` | 0 | The longest step one frame moves the tweens; 0 is off. An entrance started just before a loading hitch plays on instead of appearing at its end |

**Text and scrolling**

| Variable | Default | Meaning |
| --- | --- | --- |
| `DreamGUI.Text.SmallTextCoverage` | -1 | Whether small text draws from hinted coverage glyphs. -1 follows the project setting, 0 never, 1 for every font that leaves it to the project. A change repaints every text |
| `DreamGUI.Text.SmallTextMaxPixelSize` | 0 | Above 0, replaces the project's small-text pixel limit, to force coverage onto larger labels for a measurement |
| `DreamGUI.Text.SmallTextOnMove` | 0 | What small text does when it moves off the pixel grid: 0 repaints at every move; 1 keeps its coverage quads and repaints after holding still for 3 frames; 2 repaints once from the field, then from coverage once still |
| `DreamGUI.Text.IncrementalLayout` | 1 | A field being typed into, or a long text whose content changed in two layouts in a row, lays out again only what an edit touched |
| `DreamGUI.Text.IncrementalParse` | 1 | An incremental layout finds the edit from the text itself and reads only the window around it |
| `DreamGUI.Text.IncrementalMeasure` | 1 | Inside a long paragraph, only a window around the edit is shaped and measured again |
| `DreamGUI.Text.InPlaceDisplayList` | 1 | The display list written last time is edited where it stands |
| `DreamGUI.Text.ShapeCache` | 1 | A word shaped before (same face, size, script, direction, language and features) comes from the cache |
| `DreamGUI.Text.ShapeCacheKB` | 4096 | The shape cache's budget, about 15,000 words; a change empties the cache |
| `DreamGUI.Scroll.SnapToDevicePixels` | 0 | A scroll view on a 2D canvas moves its content by whole device pixels, so small text keeps its pixels while scrolling |

## Log categories

| Category | Who writes to it |
| --- | --- |
| `DreamGUI` | The core, and the input, controls, extensions and samples modules |
| `LogDreamGUIRenderer` | The renderer |
| `DreamTween` | Tweens |
| `DreamGUIEditor` | The designer and asset tools (text compile, write-back, the bridge) |
| `DreamGUIK2Nodes` | The Blueprint nodes |
| `LogDreamGUIReferenceDocs` | The reference-docs commandlet |

Verbosity is set the engine's way: `-LogCmds="DreamGUI Verbose"` on the command line, or `Log DreamGUI Verbose` at the
console.
