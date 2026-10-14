---
title: Benchmarks
description: The two walls of buttons in Tools/Bench — a screen of 5000 buttons and a level of 2688 world-space panels — how to run them in PIE or -game, how to read the CSV profiles and traces they leave, and the rules for reading the numbers.
---

`Tools/Bench` holds the benchmarks DreamGUI's performance work was measured with: two scenes, each the worst case of one
kind of UI, run on the test host (see [The automation suite](tests.md#the-test-host)) in the editor's PIE or in
a `-game` process on the editor binary.

| Mode | Scene | What it costs |
| --- | --- | --- |
| `screen` | One screen-space canvas of 5000 buttons, all turning at once when its Play button is clicked, round after round | The game thread: animation players, render layers, the canvas update |
| `world` | A level of 2688 world-space button panels, each its own actor and canvas, animating on their own | Both threads: per-panel passes on the game thread, one draw per panel on the render thread |

**Note:**

  **The scenes are not in the repository.** They are content of the project that uses DreamGUI, not of the plugin. Copy them
  into the test host's `Content` before running: for the screen wall, a widget Blueprint whose root holds the buttons and a
  `Button_On_Clicked` event (or rows with a `CallAnimation` function), named by `-ScreenWidget`; for the world wall, a level of
  `DreamWorldWidgetActor`s, named by `-Map`. The defaults are the names they have in the development project.

## Running

```powershell
$env:DREAMGUI_TEST_PROJECT = '<host>\DreamGUITestHost.uproject'
$env:DREAMGUI_ENGINE = '<engine root>'

pwsh -File Tools/Bench/bench_launch.ps1 -Mode world -Tag w1 -Csv 1          # PIE, CSV profile of the animated windows
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Tag s1 -Game -Csv 1   # -game: no editor UI in the frame
pwsh -File Tools/Bench/bench_series.ps1 -Runs world:warm,world:w1,world:w2,screen:s1,screen:s2 -Report r1
```

Everything goes to `<host>/Saved/DreamGUIBench`: the copied log, the trace (`-Trace 1`), samples (`-Sample 1`).

`bench_launch.ps1` is one session: it starts the editor on the host, driven by `bench_run.py`, measures an idle window and
then the animated windows, and prints the `[DreamPerf]` lines (frame count, average, median, p95, max). It only ever stops the
process it started, and refuses to start while an editor of the same project is running. For the session it turns the test
host's two verification switches off (`r.DreamUI.VerifyPartialPrepare`, `r.DreamUI.VerifyKeptPointers`) — they cost what the
shortcuts save.

| Parameter | What it does |
| --- | --- |
| `-Mode` | `screen` or `world` |
| `-Tag` | A name for this run; the trace, the samples and the copied log are named after it |
| `-Game` | A `-game` process on the editor binary instead of PIE |
| `-Map`, `-ScreenWidget` | The level; the screen wall's widget class |
| `-Csv 1`, `-Trace 1`, `-TraceChannels` | A CSV profile; an Insights trace; extra trace channels (`DreamUIDetail`, say) |
| `-StatsCmds` | Console commands run at the start and end of the animated windows (`;`-separated), e.g. `DreamUI.Stats` |
| `-AbCmds` | An A/B: side A measured first, then these commands, then side B |
| `-SetupCmds` | Console commands for the whole session, both sides alike, run as the game starts, e.g. `r.DreamUI.RenderLayers 0` |
| `-Warmup`, `-Window`, `-Rounds` | Warm-up seconds, measured-window seconds, number of rounds |
| `-Sample 1`, `-SamplePhase` | Sample the game thread with `StackSampler.cs` over the `window`, `all`, `edges` or `ends` phase |
| `-Cursor` | Where the pointer rests: `away` (top-left corner, no ray into the wall), `center` or `keep` |

## Scripts that read profiles and traces

| Script | What it does |
| --- | --- |
| `bench_series.ps1` | Several sessions, with the CSV medians of each in one report; a tag starting with `warm` is left out |
| `csv_summary.py` | Medians, averages and p95 of a CSV profile's columns (`--columns=`, `--dir=`) |
| `csv_spikes.py` | How many frames went over each budget, and the frames over `--over=` with their neighbours |
| `insights_top.py` | A trace's heaviest timers over a region (`--region DreamPerf_Anim_A`), per frame |
| `frame_breakdown.py` | The slowest frames of a trace, broken down one by one: what a spike was made of |
| `run_sampler.ps1`, `StackSampler.cs`, `cmp_samples.py` | A sampling profiler for one thread of the running process, and a comparison of two of its reports |

## An example: small text under motion

Small text (20 device pixels and under) draws from coverage glyphs, which must be repainted when a text moves off the device's
pixel grid. What that costs while things move is measured as an A/B in one session — coverage on for side A, off for side B
(`DreamGUI.Text.SmallTextCoverage 0`):

```powershell
# twice: the first launch after a build compiles shaders in the background, and is discarded
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag covwarm -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov1 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
# the same with render layers off on both sides
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov2 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0' -SetupCmds 'r.DreamUI.RenderLayers 0'
```

The screen wall's labels are its buttons' own text at 16 px, so every one is small text, and all 5000 start turning in one
frame. Read side A against side B from the `[DreamPerf] screen anim A` and `anim B` lines (the CSV covers side A only), and
the counters from the `DreamUI.Stats` output at the start and end of each side. Something is wrong when:

- the worst of the rounds' first frames (every button starting to turn) is 5 ms or more above side B's;
- `TextMoveRepaints` per frame comes near the number of moving texts during a scroll (a repaint on every move);
- `SmallTextPlacements` per frame comes near the number of animated texts once they are render layers;
- `CoverageFlushes` is anything but 0 in the steady rounds.

Coverage never runs in world space, so the world wall's two sides should not differ — it is the control.

The same question on a fixed scene, inside the editor, is the `Perf` preset's render benchmark: its labels slide by 0.37 of
a pixel a frame, scroll by whole pixels, tween their scale, turn (with render layers on and off), zoom and pulse their colour,
each motion timed with coverage on and off, with the results in `Saved/DreamGUITests/Perf/Benchmark.json`.

## A packaged build

The world wall plays by itself, so a packaged Development build can be measured without the editor's Python: cook and pack
the world wall's level with `RunUAT.bat BuildCookRun`, run the packaged game with `-csvCaptureFrames=1800 -ExitAfterCsvProfiling`
(which ends the game when the capture ends), and read the new CSV with `csv_summary.py`. Things to know:

- Add `-DisableAdaptiveUnity` to the build: the host's plugin is a git worktree whose files a sync has just copied, and
  adaptive unity takes every one of them for a file being edited and compiles them one by one — hundreds, for an hour or more.
- The level has no PlayerStart: the default view looks at the panels from behind, and the first frames load.
  `csv_summary.py`'s medians are not moved by that; its averages are.
- The screen wall needs the widget the editor's Python puts on the screen, which a packaged build has no Python for: measure
  it with `-Game` instead.

The full commands are in `Tools/Bench/README.md`.

## Reading the numbers

- **Discard the first launch after a build**: shaders compile in the background, and its frames are slower for it.
  `bench_series.ps1` runs a tag that starts with `warm` and leaves it out of the report.
- **Compare within one sitting.** Numbers from different days, or from before and after anything else changed on the machine
  (another program, a second monitor, the window covered), are not comparable; the 60 FPS frame cap hides the difference until
  it does not. Judge a change by an A/B in one session (`-AbCmds`), or by a function's share in a sampled profile.
- **PIE pays for the editor.** About half of a PIE game thread is the editor's own UI; `-Game` leaves that out, and a packaged
  build leaves out the rest.
- **A trace costs frame time**, each timing scope about 0.3 µs on the author's machine: a session that measures runs with
  `-Trace 0`, and a breakdown is a separate, traced session. DreamGUI's per-canvas, per-widget and per-player scopes are on a
  channel of their own, `DreamUIDetail`, off unless named in `-TraceChannels`.
