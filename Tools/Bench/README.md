# Benchmarks: two walls of buttons

What DreamGUI's performance work was measured with, from October 2026 on. Two scenes, each the worst case of one kind
of UI, run on the test host (`Tools/TestHost`) in the editor's PIE or in a `-game` process on the editor binary.

| Mode | Scene | What it costs |
|---|---|---|
| `screen` | One screen-space canvas of 5000 buttons, all turning at once when its Play button is clicked, round after round | The game thread: animation players, render layers, the canvas update |
| `world` | A level of 2688 world-space button panels, each its own actor and canvas, animating on their own | Both threads: per-panel passes on the game thread, one draw per panel on the render thread |

The scenes are content of the project that uses DreamGUI, not of the plugin, and are not in this repository. Copy them
into the test host's `Content` before running: a widget Blueprint whose root holds the buttons and a `Button_On_Clicked`
event (or rows with a `CallAnimation` function) for the screen wall -- `-ScreenWidget` names its class -- and a level of
`DreamWorldWidgetActor`s for the world wall -- `-Map` names it. The defaults are the names they have in DevProject.

## Running

```
$env:DREAMGUI_TEST_PROJECT = '<host>\DreamGUITestHost.uproject'
$env:DREAMGUI_ENGINE = 'C:\Program Files\Epic Games\UE_5.8'

pwsh -File Tools/Bench/bench_launch.ps1 -Mode world -Tag w1 -Csv 1          # PIE, CSV profile of the animated windows
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Tag s1 -Game -Csv 1   # -game: no editor UI in the frame
pwsh -File Tools/Bench/bench_series.ps1 -Runs world:warm,world:w1,world:w2,screen:s1,screen:s2 -Report r1
```

Everything goes to `<host>/Saved/DreamGUIBench`: the copied log, the trace (`-Trace 1`), samples (`-Sample 1`).

- `bench_launch.ps1` -- one session. Measures an idle window, then the animated windows, and prints the `[DreamPerf]`
  lines (frame count, average, median, p95, max). `-AbCmds` measures a side B after console commands; `-SetupCmds` runs
  console commands for the whole session, both sides alike, as the game starts; `-StatsCmds` runs commands around the
  windows (`DreamUI.Stats`). Turns the test host's verification switches off for the session
  (`r.DreamUI.VerifyPartialPrepare`, `r.DreamUI.VerifyKeptPointers`): they cost what the shortcuts save.
- `bench_series.ps1` -- several sessions, with the CSV medians of each in one report.
- `csv_summary.py` -- medians, averages and p95 of a CSV profile's columns (`--columns=`, `--dir=`).
- `csv_spikes.py` -- how many frames went over each budget, and the frames over `--over=` with their neighbours.
- `insights_top.py` -- a trace's heaviest timers over a region (`--region DreamPerf_Anim_A`), per frame.
- `frame_breakdown.py` -- the slowest frames of a trace, broken down one by one: what a spike was made of.
- `run_sampler.ps1`, `StackSampler.cs`, `cmp_samples.py` -- a sampling profiler for one thread of the running process
  (`bench_launch.ps1 -Sample 1 -SamplePhase window|all|edges|ends`), and a comparison of two of its reports.

## Small text under motion: the coverage A/B

Small text (20 device pixels and under) draws from coverage glyphs, which have to be repainted when a text moves off
the device's pixel grid. What that costs while things move is measured as an A/B in one session, coverage on (side A)
against off (side B, `DreamGUI.Text.SmallTextCoverage 0`):

```
# twice: the first launch after a build compiles shaders in the background, and is discarded
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag covwarm -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov1 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
# the same with render layers off on both sides
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov2 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0' -SetupCmds 'r.DreamUI.RenderLayers 0'
# controls, once each: the world wall and Lvl_Test_UI (DevProject's, copied into the host as the walls are) are
# world-space, where coverage never runs, so their two sides should not differ
pwsh -File Tools/Bench/bench_launch.ps1 -Mode world -Game -Csv 1 -Trace 0 -Tag covworld -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
pwsh -File Tools/Bench/bench_launch.ps1 -Mode world -Game -Csv 1 -Trace 0 -Tag covui -Map /Game/Maps/Lvl_Test_UI -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
```

The screen wall's labels are its buttons' own text at 16 px, so every one of them is small text; the 5000 of them start
turning in one frame. Read side A against side B from the `[DreamPerf] screen anim A` and `anim B` lines (the CSV covers
side A only), and the counters from the `DreamUI.Stats` output at the start and the end of each side's rounds
(`DreamUIRenderStats`: TextPaints, TextMoveRepaints, SmallTextPlacements, SharpenSweepTexts, SharpenRepaints,
CoverageItemsDrawn, CoverageGlyphLookups, CoverageRastersSync, CoverageJobs, CoverageFlushes, FontAtlasUploadBytes).
Something is wrong when:

- the worst frame of the rounds' first frames (every button starting to turn) is 5 ms or more above side B's;
- TextMoveRepaints per frame comes near the number of moving texts during a scroll (a repaint on every move);
- SmallTextPlacements per frame comes near the number of animated texts once they are render layers;
- CoverageFlushes is anything but 0 in the steady rounds.

The motion switches that change what a player sees are measured the same way and left at today's behaviour until the
numbers decide: `-AbCmds 'DreamGUI.Text.SmallTextOnMove 1'` (keep the painted coverage quads while a text moves, repaint
once it is still) or `2` (the field while it moves), and `-AbCmds 'DreamGUI.Scroll.SnapToDevicePixels 1'` (scroll views
move their content by whole device pixels, which keeps coverage with no repaint).

The same question on a fixed scene, inside the editor, is the Perf preset's render benchmark
(`DreamGUI.Performance.RHI.TheBenchmarkSceneRecordsWhatEachStageOfItsFramesCosts`): its labels slide by 0.37 of a
pixel a frame, scroll by whole pixels, tween their scale, turn (with render layers on and off), zoom and pulse their
colour, each motion timed with coverage on and off, and `Saved/DreamGUITests/Perf/Benchmark.json` holds each phase's
counters per frame (`<motion>.CoverageOn`, `<motion>.CoverageOff`), which `Tools/Tests/perf_report.py` compares.

## A packaged build

The world wall plays by itself, so a packaged Development build can be measured without the editor's Python. From the
host's directory:

```
$env:NO_PROXY = "$env:NO_PROXY,[::1]"   # behind a local proxy: UAT's Zen probe of [::1] must not go to it
RunUAT.bat BuildCookRun -project=<host>\DreamGUITestHost.uproject -platform=Win64 -clientconfig=Development -build -cook `
    -stage -pak -map=/Game/Maps/Lvl_Test_UI_BenchMark_Workdspace_5000Button -unattended -nop4 -NoCompileEditor `
    '-ubtargs=-DisableAdaptiveUnity'
Saved\StagedBuilds\Windows\DreamGUITestHost.exe /Game/Maps/Lvl_Test_UI_BenchMark_Workdspace_5000Button -windowed `
    -resx=1920 -resy=1080 -csvCaptureFrames=1800 -ExitAfterCsvProfiling
python Tools/Bench/csv_summary.py "<the new CSV under Saved\StagedBuilds\Windows\DreamGUITestHost\Saved\Profiling\CSV>"
```

- `-DisableAdaptiveUnity`: the host's plugin is a git worktree whose files a sync has just copied, and adaptive unity
  takes every one of them for a file being edited and compiles them one by one -- hundreds, for an hour or more.
- `-ExitAfterCsvProfiling` ends the game when the capture ends; there is no `-csvExitOnCompletion`.
- The level has no PlayerStart: the default view looks at the panels from behind, and the first frames load.
  `csv_summary.py`'s medians are not moved by those; its averages are.
- The screen wall needs the widget the editor's Python puts on the screen, which a packaged build has no Python for:
  measure it with `-Game` instead.

## Reading the numbers

- **Discard the first launch after a build**: shaders compile in the background, and its frames are slower for it.
  `bench_series.ps1` runs a tag that starts with `warm` and leaves it out of the report.
- **Compare within one sitting.** Numbers from different days, or before and after anything else changed on the
  machine (another program, a second monitor, the window covered), are not comparable; the frame-time cap of 60 FPS hides
  the difference until it does not. Judge a change by an A/B in one session (`-AbCmds`), or by the share a function has
  in a sampled profile.
- **PIE pays for the editor.** About half of a PIE game thread is the editor's own UI; `-Game` leaves it out, and a
  packaged build leaves out the rest.
- **A trace costs frame time**, each timing scope about 0.3 us here: a session that measures runs with `-Trace 0`, and
  a breakdown is a separate, traced session. DreamGUI's per-canvas, per-widget and per-player scopes are on a channel of
  their own, `DreamUIDetail`, off unless named in `-TraceChannels`.
