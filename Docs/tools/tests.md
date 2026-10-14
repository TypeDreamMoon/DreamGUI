---
title: The automation suite
description: The plugin's own automation suite (more than 2400 tests in a full run), Invoke-DreamGUITests.ps1 and its presets, the static checks and their rules, the test host project, and the packaged text smoke test of the release gate.
---

The automation suite is part of the plugin — upstream had none. It lives in the `DreamGUITests` module (loaded in the
editor only), and its tests are named `DreamGUI.<Area>.<Sentence>`, for example
`DreamGUI.Button.ATapOnTheButtonClicksItOnce`; the tween tests are under `DreamTween.*`. An `All` run runs **more than
2400** tests.

## The simplest way to run it

At the editor's console:

```text
Automation RunTests DreamGUI
```

or headless:

```powershell
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests DreamGUI" -unattended -nopause -NullRHI -TestExit="Automation Test Queue Empty"
```

`RunTests` splits its filter on `+` and ORs the terms: `StartsWith:X` matches names starting with `X.`, `^X$` matches
exactly, `Group:X` expands an ini group, and anything else is a case-insensitive **substring**. A filter cannot contain
commas, semicolons or quotes — `-ExecCmds` splits on commas and the Automation command on semicolons.

Under `-nullrhi` the engine drops every test flagged `NonNullRHI` (the ones that read pixels) by itself, so those need
`-RenderOffScreen`: a real RHI without a window.

## The runner: Invoke-DreamGUITests.ps1

What you use day to day is `Tools/Tests/Invoke-DreamGUITests.ps1`. It builds, runs and judges, and says how many tests
actually ran. It needs PowerShell 7.2 or later (`pwsh`), Python 3.8 or later on `PATH` as `python`, and git.

```powershell
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1                        # Quick: build, then the headless suite
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Preset Rhi -NoBuild
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Filter "StartsWith:DreamGUI.Button" -Repeat 5
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Project <path>\MyGame.uproject -Engine <engine root>
```

It does six things, in order:

### Pre-flight
The engine and the project exist; the report directory is not on drive C (`-AllowSystemDrive` or
`DREAMGUI_ALLOW_DRIVE_C=1` allows it); Python answers; and no editor has this `.uproject` on its command line — an open
editor holds the DLLs the build must replace (LNK1104) and shares `Saved` with the run, so the runner refuses (exit 2)
unless `-AllowEditorOpen`.

### Static checks
`static_checks.py`, below. Skip with `-SkipStaticChecks`.

### Build
The whole editor target, with `-NoEngineChanges`: a build that would recompile or rewrite anything in the engine is refused
before its first action. **Never `-Module=`** — a restricted build does not rewrite the plugin's module manifest, and a
plugin whose manifest misses a module fails to load as a whole. Skip with `-NoBuild`.

### Manifest
The plugin's `Binaries\Win64\UnrealEditor.modules` must carry the engine's BuildId and list every module
`DreamGUI.uplugin` builds into an editor, each with a DLL that is not empty. Checked with `-NoBuild` too.

### Run
`UnrealEditor-Cmd.exe` with `Automation RunTests <filter>` plus the preset's arguments; past the preset's timeout
(`-TimeoutMinutes` overrides it) the editor is killed with everything it started. `-Repeat N` runs N times after one build,
each in a fresh editor.

### Digest
`digest.py` judges the run from the engine's JSON report and the log; see below.

Which project: `-Project`, else the `DREAMGUI_TEST_PROJECT` environment variable, else the test host when it exists. The
plugin under test is the DreamGUI inside that project's `Plugins` directory, which need not be the checkout the runner
lives in — the banner prints its path, branch and commit. The engine: `-Engine`, else `DREAMGUI_ENGINE`.

### Presets

Defined in `presets.json`; the runner owns the list, and nothing depends on the engine's ini groups.

| Preset | Runs | Editor arguments |
| --- | --- | --- |
| `Quick` | every `DreamGUI.*` and `DreamTween.*` test except the PIE layer (`DreamGUI.Pie.*`) | `-nullrhi` |
| `Interaction` | the tests declared under `Private/Interaction` and `Private/Driver/Tests` | `-nullrhi` |
| `Designer` | `DreamGUI.Designer.*` | `-nullrhi` |
| `Rhi` | every test flagged `NonNullRHI`, except the PIE layer's and the benchmark | `-RenderOffScreen` |
| `Validate` | the same, under the RHI validation layer | `-RenderOffScreen -rhivalidation` |
| `Pie` | `DreamGUI.Pie.*` (its `NonNullRHI` tests run in `PieRhi`) | `-nullrhi` |
| `PieRhi` | the `NonNullRHI` tests of `DreamGUI.Pie.*`, on a real RHI | `-RenderOffScreen` |
| `Exit` | `DreamGUI.Lifecycle.Smoke.*`, then the editor's own exit, its log searched afterwards | `-nullrhi` |
| `Perf` | the benchmark, `DreamGUI.Performance.*`, alone in its editor | `-RenderOffScreen` |
| `All` | everything, in one editor on a real RHI | `-RenderOffScreen` plus `-dpcvars` that turn GPU Scene's reserved buffers off |

`All` turns GPU Scene's reserved buffers off because of how many worlds the suite makes: each test world's scene reserves
8 GiB of GPU address space, a destroyed world keeps its scene until the next garbage collection (about a minute apart in
the editor), and at twenty worlds a second the process runs out of address space within seconds, after which D3D12
removes the device. Headless, the same worlds cost nothing on the GPU.

Each preset has a **floor**: the least number of tests that must actually run, fewer being an infrastructure failure
(exit 2). It is what stops a plugin that loaded without its test module from passing as "0 tests, 0 failures". Floors
only ever go up.

### Exit codes and the report

| Code | Meaning |
| --- | --- |
| 0 | green: every test that ran passed, known issues aside |
| 1 | red: at least one test failed that is not a known issue |
| 2 | not judged: static checks, build, manifest, a crash, an ensure, a timeout, a missing report, fewer tests than the floor, declared tests that should have run and did not, or the editor exiting with a non-zero code |

**A run with an ensure is not green** even when every test passed: an ensure is a bug the tests did not assert on.

The report goes to `<project>\Saved\DreamGUITestReports\<timestamp>-<Preset>\` (move the root with `-ReportDir`). The page
to read is `summary.md`: the verdict, the counts, each failure with its first error, known issues, ensures, a crash, slow
and flaky tests. Beside it are `digest.json` for scripts, the engine's own `index.json` / `index.html`, the editor log
`run.log`, and each earlier step's output. The report root keeps `latest.txt` and `history.csv` — one line per test per
run; a test that both passed and failed at the same commit under the same preset, in clean-tree runs, is listed as
**flaky**.

`known-issues.json` lists tests that are red on purpose for now (with a reason, a decision and a date); a listed failure
does not count, though a crash, an ensure or a timeout still does. When a listed test passes, the summary says so — take
the entry out, so that the next regression is red again.

### Pictures and goldens

Every picture a pixel test holds to a golden image is written to `<project>/Saved/DreamGUITests/Captures/<name>.png`
whether it matches or not, and where it does not, `<name>.diff.png` beside it marks the differing pixels in magenta. The
goldens live in `Source/DreamGUITests/Resources/Golden/`. A picture with no golden yet passes with a warning: look at it,
and copy it there once it is right. When the renderer changed on purpose, `-DreamGUIWriteGoldens` on the editor's command
line writes every picture over its golden.

The `Perf` preset writes `Saved/DreamGUITests/Perf/Benchmark.json` and a CPU trace; `perf_report.py show / compare /
insights` reads them. Nothing fails on a time: a number is only worth something next to another run's on the same machine.

## Static checks

```powershell
python Tools/Tests/static_checks.py
python Tools/Tests/static_checks.py --list-rules
```

Cheap checks that run before the build. The UHT- and MSVC-shaped rules catch what would stop a build; the test rules hold
the suite to its own conventions.

| Rule | Level | Why |
| --- | --- | --- |
| `reflected-name` | error | UHT refuses a second reflected type or delegate of the same name |
| `shadowed-property` | error | UHT refuses a UPROPERTY that hides an ancestor UPROPERTY |
| `category-required` | error | BuildPlugin compiles the plugin as an engine plugin, where UHT refuses an exposed property or a Blueprint-callable function with no Category |
| `redeclared-function` | error | UHT refuses `UFUNCTION()` on an override, and a UPROPERTY named like an ancestor UFUNCTION |
| `ufunction-param` | error | UHT refuses a UFUNCTION parameter named like a UPROPERTY of the class |
| `param-hides-member` | error | C4458 is an error in this build |
| `test-unreadable` | error | A test the runner cannot read cannot be selected, counted or expected |
| `test-class-unique` | error | Two test declarations with one class define `RunTest` twice |
| `test-path-unique` | error | The framework keys tests by their full name; the second one is lost |
| `test-path-format` | error | Presets, filters and the coverage matrix read the name: `DreamGUI.<Area>.<Sentence>` |
| `test-flags` | error | Without `EditorContext` a test never runs in the editor |
| `test-tags` | error | The framework files tags under the full name given, silently; a typo files them under a test that does not exist |
| `hand-fed-hit` | error | A test that hands the event system its hit tests a pointer nobody can produce; drive the pointer instead |
| `pixels-need-rhi` | error | Under `-nullrhi` nothing is drawn; a pixel test without `NonNullRHI` runs there and fails or, worse, passes |
| `rig-needs-bindtest` | error | An unbound driver rig reports nothing to the test; its steps can fail without the test failing |
| `plan-label` | error | Planning labels do not belong in the code, the test names or the comments |
| `eol` | warn | Mixed line endings make every later diff of the file noisy |
| `layering` | error | A lower layer that includes a higher one cannot be split from it |
| `layering-stale` | error | The list of edges still to cut only shrinks: an edge that is gone comes off it |
| `engine-private-path` | error | The engine's `Private` and `Internal` headers change between versions without notice; the plugin reads the engine through its public headers |

A finding that is right to keep is allowed where it stands (on the line or the line above), or in
`static-checks-allow.json` with a path glob, an optional regular expression and a reason:

```cpp
HitContainer.HitResult.Widget = Target;   // static-checks: allow(hand-fed-hit) this test is about the event system's handling of a given hit
```

**The layering rules** hold the runtime files to the module layers — `DreamGUIRenderer` and `DreamTween` at the bottom,
then the core `DreamGUI`, the input `DreamGUIInput`, `DreamGUIControls` and `DreamGUIExtensions` side by side, and
`DreamGUISamples` on top. `module-owners.csv` says which module each file belongs to; a file may include its own module's
headers and lower layers', never a higher layer's or a sibling's (`layering`). The includes that still do are listed in
`layering-allow.json`, and that list only gets shorter.

**Coverage**: `Tools/Tests/COVERAGE.md` is a table of controls by inputs by configurations, where every cell is covered by
a driver test, declared not applicable with a reason, or a hole. Tests claim cells with tags (`[Pointer]`, `[Touch]`,
`[Nav]`, `[Text]` × `[Animated]`, `[Disabled]`, `[Scaled]`, `[World]`); `coverage_matrix.py` draws the table, and
`--fail-on-holes` makes it a gate.

There is also `Invoke-DreamGUINightly.ps1` for a nightly run (move the host's plugin worktree to a commit, run a list of
presets, write a summary of what changed since the last one) and an optional pre-push hook (`Install-DreamGUIHooks.ps1`,
which runs `Quick` before a push).

## The test host

`Tools/TestHost/` is a minimal Unreal project that exists only to build DreamGUI and run its suite, away from any project
you actually work in:

- **Your editor can stay open.** The host's `Plugins/DreamGUI` is a separate **git worktree** of the DreamGUI repository
  with its own `Binaries/` and `Intermediate/`, so building and running it touches nothing your editor has loaded.
- **Other sessions cannot change it under you.** It tests what is committed and checked out in its worktree, not the
  uncommitted state of your working copy.
- **"The project builds" is not "the plugin builds".** The host enables only DreamGUI and Enhanced Input and has no code of
  its own (but the smoke probe below), so it exposes a dependency the plugin forgot to declare. It uses
  `UDreamGameViewportClient`, as the README asks of a game.

```powershell
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -WhatIf   # see what it would do
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -Root <host dir> -RepoPath <DreamGUI checkout> -Branch <branch>
```

The script is safe to run again at any time: missing template files are written, matching ones left alone, differing ones
listed and kept (`-Force` replaces them, keeping a `.bak` of each); an existing worktree is never removed, reset or switched.
`-Root` is refused on drive C unless `-AllowSystemDrive`. From then on the runner picks the host up by itself when its
`.uproject` exists.

Every host run loads the **old-asset fixtures**: widget Blueprints and a level saved by the plugin at 1.0.0 — the baseline
later versions keep loading — with a snapshot of what they held when saved. The `DreamGUI.Compatibility` tests compare them
with the snapshot, so a class that moves or is renamed without a redirect, or a property a change loses, shows up there. The
first set, saved on 2026-09-28 before any class moved between modules, loaded through the plugin's CoreRedirects; 1.0.0 ships
none, so that set was replaced by one 1.0.0 saved. The fixtures are inputs, never outputs — the command that writes them
refuses to replace one that exists.

## The packaged text smoke test and the release gate

Everything else in the suite runs in the editor, on uncooked content. This one is a **packaged game**: a screen of the text
cases that depend on what a cook and the packaging preset ship — fonts read from their cooked bytes, fallbacks by culture,
colour emoji, small text from coverage glyphs, a justified paragraph, Japanese line breaks, the safe zone — written down field
by field by a probe and held to the same build run on uncooked content.

| Piece | Where |
| --- | --- |
| The screen | `Tools/TestHost/Template/DUI/TextSmoke.dui`, copied to the host's `DUI/` |
| Its assets | `/Game/DreamGUISmoke`, made by `Tools/TestHost/make_text_smoke_assets.py` |
| The probe | `DreamGUIPackagedSmoke.cpp` in the host's game module, switched on by `-DreamGUITextSmoke=<dir>`, Shipping included |
| The comparison | `Tools/Tests/compare_text_smoke.py`, exit code 0 when the two runs agree |

The probe waits for the player, puts `WBP_TextSmoke` on the viewport, waits until every glyph has landed and the small text
has settled, writes `TextSmoke.json` (each text's display list, the fonts' answers, the fallback entries, what ICU falls back
to, the safe zone, the frame times, the `DreamGUI.Memory Json` memory report) and a picture of the viewport, `TextSmoke.png`,
and asks the game to exit. The whole flow — build the editor target, make the assets, build the game target in Development
and Shipping, the uncooked reference run, cook and pack with `-I18NPreset=EFIGSCJK`, the packaged run, the comparison —
builds two targets and cooks, about an hour on the author's machine; run it as one script in the background. The full
commands are in `Tools/TestHost/README.md`.

**The release gate** is what a version goes through before it is tagged: BuildPlugin (compiling the plugin for the editor
and for the game target in Development and Shipping), plus this packaged text smoke test. **Until the gate has run for a
version, no packaged game of that version has been run.** Win64 is the only platform built and run; see
[Platforms](../guides/platforms.md).
