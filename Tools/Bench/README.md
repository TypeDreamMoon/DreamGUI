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
  lines (frame count, average, median, p95, max). `-AbCmds` measures a side B after console commands; `-StatsCmds` runs
  commands around the windows (`DreamUI.Stats`). Turns the test host's verification switches off for the session
  (`r.DreamUI.VerifyPartialPrepare`, `r.DreamUI.VerifyKeptPointers`): they cost what the shortcuts save.
- `bench_series.ps1` -- several sessions, with the CSV medians of each in one report.
- `csv_summary.py` -- medians, averages and p95 of a CSV profile's columns (`--columns=`, `--dir=`).
- `csv_spikes.py` -- how many frames went over each budget, and the frames over `--over=` with their neighbours.
- `insights_top.py` -- a trace's heaviest timers over a region (`--region DreamPerf_Anim_A`), per frame.
- `frame_breakdown.py` -- the slowest frames of a trace, broken down one by one: what a spike was made of.
- `run_sampler.ps1`, `StackSampler.cs`, `cmp_samples.py` -- a sampling profiler for one thread of the running process
  (`bench_launch.ps1 -Sample 1 -SamplePhase window|all|edges|ends`), and a comparison of two of its reports.

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
