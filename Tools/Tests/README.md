# Running the DreamGUI tests

Everything needed to build the plugin's host project, run its automation tests headlessly and
judge the result lives here, next to the tests it runs.

| File | What it is |
|---|---|
| `Invoke-DreamGUITests.ps1` | The runner: pre-flight, static checks, build, manifest check, editor run(s), digest |
| `presets.json` | What each preset runs: selection, editor arguments, floor on the test count, timeout |
| `digest.py` | Judges one editor run from the engine's JSON report and the log; also reduces a build log |
| `known-issues.json` | Tests known to be red that must not turn a run red |
| `static_checks.py`, `static-checks-allow.json` | Cheap checks of the C++ and of the tests' own rules, and the findings they accept |
| `coverage_matrix.py`, `coverage.json`, `COVERAGE.md` | The control-by-input-by-configuration coverage table and its tag convention |
| `sourcescan.py` | What the source declares as tests, shared by all of the above |
| `hooks/pre-push`, `Install-DreamGUIHooks.ps1` | The optional pre-push hook and its installer |

Requirements: PowerShell 7.2 or later (`pwsh`), Python 3.8 or later on `PATH` as `python`, git.

## Running

```
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1                    # Quick: build, then the headless suite
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Preset Rhi -NoBuild
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Filter "StartsWith:DreamGUI.Button" -Repeat 5
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Project I:\UnrealProject_Moon\DEV_58\DevTest\DevTest.uproject
```

The steps, in order:

1. **Pre-flight.** The engine (`-Engine`, default `F:\UnrealEngine\UE_Moon`) and the project exist;
   the report directory is not on drive C (it is nearly full); Python 3 answers; and no Unreal
   editor has this `.uproject` on its command line. An open editor would hold the DLLs the build
   has to replace (LNK1104) and share the project's `Saved` directory with the test run, so the
   runner refuses (exit 2) unless `-AllowEditorOpen`.
2. **Static checks** (`static_checks.py`); skip with `-SkipStaticChecks`.
3. **Build**: `Build.bat <Project>Editor Win64 Development -Project=... -WaitMutex -NoHotReloadFromIDE
   -NoEngineChanges`, the whole editor target. Never `-Module=`: a restricted build does not rewrite
   the plugin's module manifest, and a plugin whose manifest misses a module fails to load as a whole.
   `-NoEngineChanges` makes UnrealBuildTool refuse, before running any action, a build that would
   recompile or rewrite anything in the engine (the editor target shares the engine's build products).
   Skip the build with `-NoBuild`.
4. **Manifest**: the plugin's `Binaries\Win64\UnrealEditor.modules` must carry the engine's BuildId
   (the module manager skips every module of a manifest whose BuildId differs) and list every module
   `DreamGUI.uplugin` builds into an editor, each with a DLL that is not empty. Checked with
   `-NoBuild` too.
5. **Run**: `UnrealEditor-Cmd.exe "<project>" -ExecCmds="Automation RunTests <filter>" -unattended
   -nopause -NoSplash -NoSound -TestExit="Automation Test Queue Empty" -ReportExportPath=<dir>
   -abslog=<dir>\run.log -ShaderWorkingDir=<project>\Intermediate\ShaderWorkingDir` plus the
   preset's arguments. The shader working directory defaults to `%TEMP%` on drive C otherwise
   (`FPaths::ShaderWorkingDir`); `-ShaderWorkingDir` moves it. The editor is killed, with everything
   it started, when it outlives the preset's timeout (`-TimeoutMinutes` overrides).
6. **Digest** (`digest.py`), below.

`-Repeat N` runs steps 5 and 6 N times, each in a fresh editor, after one build.

### Which project

`-Project` names the `.uproject`. Without it: `$env:DREAMGUI_TEST_PROJECT`, else the test host
project `I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\DreamGUITestHost.uproject` when it exists,
else `I:\UnrealProject_Moon\DEV_58\DevTest\DevTest.uproject`. The plugin under test is the DreamGUI
inside that project's `Plugins` directory, which need not be the checkout the runner lives in; the
banner prints its path, branch and commit.

**Use the host project when your editor is open.** The host project (see `Tools/TestHost/`) is a
minimal project whose `Plugins/DreamGUI` is a separate git worktree of this repository with its own
`Binaries` and `Intermediate`: building and testing it touches nothing your DevTest editor has
loaded, and it proves the plugin builds on its own. Its limit: it tests what is committed on its
worktree's branch, not the uncommitted state of your DevTest checkout.

## Presets

Defined in `presets.json` (the runner owns this list; nothing depends on the engine's ini groups).

| Preset | Runs | Editor arguments | Floor | Timeout |
|---|---|---|---|---|
| `Quick` | every `DreamGUI.*` and `DreamTween.*` test except the PIE layer (`DreamGUI.Pie.*`) | `-nullrhi` | 1386 | 20 min |
| `Interaction` | the tests declared under `Private/Interaction` and `Private/Driver/Tests` | `-nullrhi` | 233 | 15 min |
| `Designer` | `DreamGUI.Designer.*` | `-nullrhi` | 88 | 20 min |
| `Rhi` | every test flagged `NonNullRHI`, except the PIE layer's | `-RenderOffScreen` | 12 | 30 min |
| `Validate` | the same, under the RHI validation layer | `-RenderOffScreen -rhivalidation` | 12 | 40 min |
| `Pie` | `DreamGUI.Pie.*` (its `NonNullRHI` probes are dropped here and run in `All`) | `-nullrhi` | 9 | 30 min |
| `All` | everything, in one editor on a real RHI | `-RenderOffScreen -dpcvars=r.GPUScene.UseReservedResources=0,r.GPUScene.InstanceDataTileSizeLog2=-1` | 1404 | 60 min |

Under `-nullrhi` the engine itself drops every test flagged `NonNullRHI` before any filter applies
(`FAutomationTestFramework::GetValidTestNames`), so the headless presets need not exclude the pixel
tests. `-RenderOffScreen` gives a real RHI without a window.

`All` turns GPU Scene's reserved buffers off because of how many worlds the suite makes: every test
world's scene reserves 8 GiB of GPU address space for them (four 2 GiB buffers, `GPUScene.cpp`), a
destroyed world keeps its scene until the next garbage collection, which the editor runs a minute
apart, and at twenty worlds a second the process runs out of address space within seconds — D3D12
then removes the device (`DXGI_ERROR_DRIVER_INTERNAL_ERROR`) on the next reservation. Headless, the
same worlds cost nothing on the GPU.

The **floor** is the least number of tests that must actually run; fewer is an infrastructure
failure (exit 2). It is what stops a plugin that loaded without its test module from passing as
"0 tests, 0 failures". Floors only ever go up: raise them when tests are added.

A preset selects declared tests with `include` (name prefixes), `sources` (directories under
`Source/DreamGUITests/`), `flags` (EAutomationTestFlags they must carry) — each one given narrows
the selection — and `exclude` (name prefixes) removes. The engine cannot exclude (its RunTests terms
are OR-ed), so `sourcescan.py filter --preset <name>` turns the selection into the shortest filter
that matches exactly the selected tests, and the runner passes that. The resolved filter is saved
as `filter.txt` in the report.

### The engine's filter syntax (`-Filter`)

`Automation RunTests <filter>` (AutomationCommandline.cpp) splits the filter on `+` and OR-s the
terms: `StartsWith:X` matches names starting with `X.` (the dot is added), `^X$` matches exactly,
`Group:X` expands an ini group, anything else is a case-insensitive **substring**. `-Filter` replaces
the preset's selection (its arguments and timeout still apply; the floor becomes 1 unless
`-MinTests`). Commas, semicolons and quotes cannot be used: `-ExecCmds` splits on commas and the
Automation command on semicolons.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | green: every test that ran passed, known issues aside |
| 1 | red: at least one test failed that is not a known issue |
| 2 | not judged: static checks, build, manifest, a crash, an ensure, a timeout, a missing report, fewer tests than the floor, declared tests that should have run and did not, or the editor exiting with a non-zero code |

A run with an ensure is not green even when every test passed: an ensure is a bug the tests did
not assert on.

## The report

`<project>\Saved\DreamGUITestReports\<yyyyMMdd-HHmmss>-<Preset>\` (move the root with `-ReportDir`;
not `Saved\DreamGUITests`, which is the test module's own scratch root whose subdirectories the
tests delete):

| File | |
|---|---|
| `summary.md` | the page to read: verdict, counts, failures with their first error, known issues, ensures, crash, slow and flaky tests |
| `digest.json` | the same, for scripts |
| `index.json`, `index.html` | the engine's own report (`-ReportExportPath`) |
| `run.log` | the editor log (`-abslog`) |
| `run-info.json` | what was asked: preset, filter, arguments, floor, commit, exit code, timings |
| `filter.txt`, `static-checks.txt`, `build.log`, `manifest.txt` | the earlier steps |
| `stdout.txt`, `stderr.txt` | the editor's console output (mostly empty) |

With `-Repeat` above 1 each run has its own `repeat-<n>\` and the top `summary.md` tables them.
The report root keeps `latest.txt` (the last report directory) and `history.csv`.

`digest.py tests <run dir>` can be run again on any run directory. It reads the verdict from
`index.json`, which the engine writes only once the last test has finished, and reads `run.log` for
what the report cannot say: ensures (`Ensure condition failed`), crashes (the crash handler's
`Unhandled Exception` / `Fatal error!` / `Assertion failed:`), plugins that failed to load, a
filter that matched nothing, a queue that never emptied. It also cross-checks the tests that ran
against the tests the source declares for that filter: a declared test that should have run and did
not means binaries older than the source, or a module that did not load.

### History, flakiness, slow tests

Every run appends one line per test to `history.csv` in the report root: time, run, commit, whether
the tree had uncommitted changes, preset, test, result, seconds. A test that both passed and failed
at the same commit under the same preset, in clean-tree runs, is listed as **flaky**. Tests of the
`Interaction` preset slower than 100 ms and any test slower than 1 s are listed as **slow**; both
are reported and never fail a run (budgets in `presets.json`).

## Known issues

`known-issues.json` lists tests that are red on purpose for now:

```json
{ "issues": [
  { "test": "DreamGUI.Button.ARightClickDoesNotPressTheButton", "reason": "right clicks still press; the filter is not in yet",
    "decision": "the decision record or issue that says to live with it", "added": "2026-09-23", "presets": ["Quick"] }
] }
```

`test` may use `*` and `?`; `presets` is optional. A listed failure is shown under "Known issues" and
does not count; a crash, an ensure or a timeout still does. When a listed test passes, the summary
says so — take the entry out, so that the next regression is red again.

## Static checks

`python Tools/Tests/static_checks.py [--root <plugin>]` (`--list-rules` prints every rule and its
reason). The UHT- and MSVC-shaped rules catch what would stop a build (a reflected name declared
twice, a UPROPERTY hiding an ancestor's, a parameter hiding a member); the test rules hold the suite
to its own conventions: unique test classes and names, names of the form `DreamGUI.<Area>.<Sentence>`,
`EditorContext` and exactly one requested filter flag, tags that name a real test and come from the
coverage vocabulary, no hand-fed hits outside `Private/Driver` (`HitResult.Widget =`,
`HoverArray.Add(`: drive the pointer instead), `NonNullRHI` on every test that reads pixels,
`BindTest` wherever a test builds `FDreamDriverRig::Headless`, and no planning labels in comments
or strings.

A finding that is right to keep is allowed where it stands:

```cpp
HitContainer.HitResult.Widget = Target;   // static-checks: allow(hand-fed-hit) this test is about the event system's handling of a given hit
```

(on the line or the line above), or in `static-checks-allow.json` with a path glob, an optional
regular expression and a reason. Allow entries that no longer match anything are listed at the end
of each run.

`--fix-eol` rewrites the line endings of the files the branch touched to what `.gitattributes` asks
for, or else to the repository's majority (CRLF: `core.autocrlf=true` keeps the index LF and checks
out CRLF). The `eol` rule only reports new files whose endings differ.

## Coverage

See [COVERAGE.md](COVERAGE.md): the table of controls by inputs by configurations, the tags a test
registers to claim its cells, and `coverage_matrix.py` to draw it (`--heuristic` for a first picture
from untagged tests, `--fail-on-holes` for a gate).

## The pre-push hook

```
pwsh -NoProfile -File Tools\Tests\Install-DreamGUIHooks.ps1 -WhatIf     # see what it would do
pwsh -NoProfile -File Tools\Tests\Install-DreamGUIHooks.ps1             # install
pwsh -NoProfile -File Tools\Tests\Install-DreamGUIHooks.ps1 -Uninstall  # remove
```

Installing writes a small stub as `pre-push` in the repository's hooks directory (`core.hooksPath`
when set, else the common git directory's `hooks`, shared by all worktrees); an existing hook that is
not the stub is left alone unless `-Force`. The stub runs `Tools/Tests/hooks/pre-push` from the
checkout that pushes. For each branch pushed (deletions and tags are skipped) whose commit is the
checked-out `HEAD`, and when the tree has no uncommitted changes to `Source`, `Shaders`, `Config` or
`DreamGUI.uplugin`, it runs the `Quick` preset against the project that holds the checkout and
refuses the push when the result is not 0. Otherwise it says why and lets the push through untested.
An editor open on that project does not block a push either (the runner's `-SkipIfEditorOpen`):
close it, or push from the test host worktree, to have the push tested.

`DREAMGUI_SKIP_PREPUSH=1 git push ...` skips the hook.

## When something goes wrong

| Symptom | Likely cause |
|---|---|
| exit 2, "a plugin failed to load" or "declared ... did not run" | a module missing from the manifest (a `-Module=` build), a BuildId mismatch, stale binaries after `-NoBuild` |
| exit 2 at the manifest step, "zero bytes" | an interrupted link left an empty DLL; delete it and rebuild |
| build fails with LNK1104 | an editor has the project's DLLs loaded; close it or use the host project |
| exit 2, "the build would have recompiled or rewritten N engine file(s)" | an engine module is out of date for this target: build the engine (or the working project) with every editor closed. If the only module listed is NetCore, it is UnrealHeaderTool's parse race over `NetCore.init.gen.cpp` on an engine without the UHT fix (see `Tools/TestHost/README.md`, "Building and running") |
| exit 2, "fewer tests than the floor" | tests were removed or did not load; raise or lower the floor only knowingly |
| exit 2, "the log never reached Automation Test Queue Empty" | the editor stopped mid-run: see the crash lines in `summary.md` and `run.log` |
| exit 2 on a real RHI, `run.log` ends in "GPU crash detected ... Device 0 Removed" after a failed `CreateReservedResource` | GPU address space used up by test worlds awaiting garbage collection: the preset needs the `-dpcvars` that `All` carries (see Presets) |
| exit 2, "the report directory is on drive C" | pass `-ReportDir` on another drive |
