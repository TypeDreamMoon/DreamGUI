# DreamGUI test host

A minimal Unreal project that exists only to build DreamGUI and run its automation suite, away from
any project you actually work in.

## Why

The suite used to be built and run inside the working project (DevTest). That had three costs:

- **The working project's editor had to be closed.** Building links the plugin's DLLs, and a running
  editor holds them open; a run also needs an editor process of its own on the same project.
- **Other sessions share the working tree.** Anything else committing to or editing the same checkout
  changes what a run is testing, half-way through.
- **"The project builds" is not "the plugin builds".** A project that happens to enable other plugins,
  or carries its own config, can hide a dependency the plugin forgot to declare.

The host fixes all three. Its `Plugins/DreamGUI` is a separate **git worktree** of the DreamGUI
repository, with its own `Binaries/` and `Intermediate/`, so it builds and runs while the working
project's editor stays open; it enables nothing but DreamGUI and Enhanced Input; and it has no code of its
own but the two smoke probes (below), each off unless a game asks for it, and no content but the old-asset fixtures
and the text smoke test's assets (below).

## Layout

```
DreamGUITestHost/                       (default I:\UnrealProject_Moon\DEV_58\DreamGUITestHost)
  DreamGUITestHost.uproject             from Template/
  Config/DefaultEngine.ini              from Template/
  Config/DefaultInput.ini               from Template/
  Config/DefaultGame.ini                from Template/
  Source/DreamGUITestHost*.Target.cs    from Template/
  Source/DreamGUITestHost/              from Template/ -- the primary game module, with the two smoke probes
  Content/DreamGUIFixtures/             from Template/ -- the old-asset fixtures (below)
  DUI/TextSmoke.dui                     from Template/ -- the smoke test's screen (below)
  Content/DreamGUISmoke/                made by make_text_smoke_assets.py -- the smoke test's assets (below)
  Plugins/DreamGUI/                     a git worktree of the DreamGUI repository
  .dreamgui-testhost.json               what the host was made from (template version, repo, branch)
  Binaries/ Intermediate/ Saved/        created by the first build and run
```

The template lives here, in `Tools/TestHost/Template/`. It is not under the plugin's `Source/` or
`Tests/` folder, which are the only places UnrealBuildTool looks for module and target rules inside a
plugin, so no project that has DreamGUI in its `Plugins/` ever picks the template's `.Build.cs` or
`.Target.cs` files up.

## The old-asset fixtures

`Template/Content/DreamGUIFixtures` holds widget Blueprints and a level saved by the plugin at 1.0.0, the first
public release, and `Snapshot.txt`: what they held when the code that saved them read them back. Every run loads
them. The `DreamGUI.Compatibility` tests compare them with the snapshot, `DreamGUI.Assets` loads them with the
plugin's own content, and a `DreamGUI.Pie` test plays the level. A class that moves or is renamed without a
redirect, or a property a change loses, shows up in those tests.

The first set was saved on 2026-09-28, before any class moved between modules, and loaded through the plugin's
CoreRedirects; 1.0.0 ships none, so it was replaced by this one, made by 1.0.0.

They were made once, in the host, by two console commands of the test module, each in an editor of its
own, and copied here from the host's `Content/DreamGUIFixtures`:

```powershell
UnrealEditor-Cmd.exe <host>\DreamGUITestHost.uproject -ExecCmds="DreamGUI.OldAssetFixtures.Write Exit" -unattended -nullrhi
UnrealEditor-Cmd.exe <host>\DreamGUITestHost.uproject -ExecCmds="DreamGUI.OldAssetFixtures.Snapshot Exit" -unattended -nullrhi
```

They are inputs, never outputs. Remade after a class has moved, they would be new assets posing as old
ones, and the tests would prove nothing; the Write command refuses to replace a fixture that exists. A version
that has to change them -- as 1.0.0 did, dropping the redirects the old set needed -- says so in the CHANGELOG. See
`Source/DreamGUITests/Private/Compatibility/DreamOldAssetFixtures.h` for what each one holds.

## The packaged text smoke test

Everything else the suite runs is in the editor, on uncooked content. This one is a packaged game: a screen of the text
cases that depend on what a cook and the packaging preset ship -- fonts read from their cooked bytes, fallbacks by
culture, colour emoji, small text from coverage glyphs, a justified paragraph, Japanese line breaks, the safe zone --
written down field by field by a probe and held to the same build run on uncooked content.

| Piece | Where |
| --- | --- |
| The screen | `Template/DUI/TextSmoke.dui`, copied to the host's `DUI/` |
| Its assets: `Font_Emoji` (the engine's Noto Color Emoji, embedded), `Font_Text` (the default font with fallbacks for zh-Hans, ja at scale 1.2 and the emoji), `WBP_TextSmoke`, `L_TextSmoke` | `/Game/DreamGUISmoke`, made by `make_text_smoke_assets.py`; always cooked (`Template/Config/DefaultGame.ini`) |
| The probe | `Template/Source/DreamGUITestHost/DreamGUIPackagedSmoke.cpp`: switched on by `-DreamGUITextSmoke=<dir>`, Shipping included |
| The comparison | `Tools/Tests/compare_text_smoke.py` |

The probe waits for the game's player, puts `WBP_TextSmoke` on the viewport, times every frame from there (the first
frame that paints the texts is where a cold glyph atlas costs), waits until every glyph has landed and the small text
has settled, and then writes into `<dir>`:

- `TextSmoke.json`: each text's display list (every glyph's face, glyph, colour flag and pen position; each line's start
  and reach), its small-text gate, the fonts' answers for `A`, a Han ideograph, a kana and an emoji, the fallback
  entries, what `zh-CN` and `ja` fall back to in ICU, the safe zone's inset beside what the platform asks for, how
  colourful the picture is where the emoji are, the frame times, and the memory report (`DreamGUI.Memory Json`);
- `TextSmoke.png`: the viewport.

Then it asks the game to exit. The comparison checks each run by itself (the list is at the top of the script) and the
two runs against each other, field by field, positions to a hundredth of a unit. Its exit code is 0 when they agree.

### Running it

In PowerShell, from anywhere; `$Host_` is the host directory. The steps build two targets and cook, so they take a while
(roughly an hour on this machine): run them as one script in the background.

```powershell
$Engine = 'C:\Program Files\Epic Games\UE_5.8'
$Host_  = 'I:\UnrealProject_Moon\DEV_58\DreamGUITestHost'
$Proj   = "$Host_\DreamGUITestHost.uproject"
$Out    = "$Host_\Saved\TextSmoke"
$env:NO_PROXY = "$env:NO_PROXY,[::1]"

# The editor target, which the asset script and the reference run use.
& "$Engine\Engine\Build\BatchFiles\Build.bat" DreamGUITestHostEditor Win64 Development -Project="$Proj" -WaitMutex -NoHotReloadFromIDE -NoEngineChanges -DisableAdaptiveUnity

# The assets (once, and again after the screen or the fonts change).
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$Proj" -EnablePlugins=PythonScriptPlugin -run=pythonscript `
    -script="$Host_\Plugins\DreamGUI\Tools\TestHost\make_text_smoke_assets.py" -unattended -nullrhi

# The game target, Development and Shipping.
& "$Engine\Engine\Build\BatchFiles\Build.bat" DreamGUITestHost Win64 Development -Project="$Proj" -WaitMutex -NoHotReloadFromIDE -NoEngineChanges -DisableAdaptiveUnity
& "$Engine\Engine\Build\BatchFiles\Build.bat" DreamGUITestHost Win64 Shipping -Project="$Proj" -WaitMutex -NoHotReloadFromIDE -NoEngineChanges -DisableAdaptiveUnity

# The reference: the same build on uncooked content.
& "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" "$Proj" /Game/DreamGUISmoke/L_TextSmoke -game -windowed -ResX=1280 -ResY=720 `
    -DreamGUITextSmoke="$Out\ref" -dpcvars=r.DebugSafeZone.TitleRatio=0.9 -unattended -nosound -abslog="$Out\ref.log"

# Cook, stage and pack, with the ICU data a CJK game ships with; then run the packaged game the same way.
Remove-Item -Recurse -Force "$Host_\Saved\Cooked", "$Host_\Saved\StagedBuilds" -ErrorAction SilentlyContinue
& "$Engine\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="$Proj" -noP4 -platform=Win64 -clientconfig=Development `
    -skipbuild -cook -map=/Game/DreamGUISmoke/L_TextSmoke -stage -pak -I18NPreset=EFIGSCJK -unattended -utf8output -WaitForUATMutex
& "$Host_\Saved\StagedBuilds\Windows\DreamGUITestHost\Binaries\Win64\DreamGUITestHost.exe" /Game/DreamGUISmoke/L_TextSmoke `
    -windowed -ResX=1280 -ResY=720 -DreamGUITextSmoke="$Out\cooked" -dpcvars=r.DebugSafeZone.TitleRatio=0.9 -unattended -nosound -abslog="$Out\cooked.log"

python "$Host_\Plugins\DreamGUI\Tools\Tests\compare_text_smoke.py" "$Out\ref" "$Out\cooked"
```

Variants, each a cook and a run of its own into another directory, compared with the same reference:

- **`-I18NPreset=English`**, the engine's default preset: only what `Docs/FontsAndPackaging.md` says degrades may differ
  -- what `zh-CN` falls back to (compare with `--allow-icu-differences`), and with it possibly the Chinese text's face,
  and the Japanese paragraph's line breaks.
- **`-clientconfig=Shipping`**: the executable is `DreamGUITestHost-Win64-Shipping.exe`. Shipping writes no log and keeps
  the debug safe zone off, so the comparison checks the JSON alone and skips the safe zone.

Things to know:

- **A host made before the smoke test** gets the template's new files (the probe, `DUI/TextSmoke.dui`) from
  `New-DreamGUITestHost.ps1` as it is, and the changed ones -- the game module, its `Build.cs` and `Target.cs`,
  `Config/DefaultGame.ini` -- with `-Force`, which keeps a copy of each file it replaces. The click smoke probe came with
  template version 5 the same way: its two files as they are, the game module and its `Build.cs` with `-Force`.
- **The safe zone is not compared between the runs, by design.** An editor build fits the platform's safe zone to the
  viewport it is given; a cooked one answers in pixels of the primary display, as UMG's `SSafeZone` does
  (`FSlateApplicationBase::GetSafeZoneSize`). Each run is checked against what the platform asked for in that run.
- **The game target and the engine.** Against a launcher (installed) engine the game target links the engine's
  precompiled libraries and compiles only the host module and the plugin. Against a source engine it is monolithic
  with a build environment of its own, which compiles the whole engine into this project (see "Building and running")
  -- build it there only on purpose.
- **What the probe needs from the host:** the screen's assets in `/Game/DreamGUISmoke` (the script),
  `DirectoriesToAlwaysCook` (the template's `DefaultGame.ini`, since nothing references the screen: the probe loads it
  by name), and the template's game module with `DreamGUI`, `Json`, `Slate` and `SlateCore` as its private dependencies.

## The click smoke test

The suite's clicks are the test driver's: in the editor, put in at the viewport or at the input system. This one is a
game's, end to end -- a mouse move, a button down and a button up handed to `FSlateApplication`'s own entry points
(`ProcessMouseMoveEvent`, `ProcessMouseButtonDownEvent`, `ProcessMouseButtonUpEvent`), the ones the platform's mouse
messages go into, so Slate hit-tests the window, the game viewport takes them, the player controller hears the button and
the viewport keeps the position, and DreamGUI's input takes the press from there to the button.

| Piece | Where |
| --- | --- |
| The probe | `Template/Source/DreamGUITestHost/DreamGUIClickSmoke.cpp`: switched on by `-DreamGUIClickSmoke=<dir>`, Shipping included |
| The check | `Tools/Tests/check_click_smoke.py` |

The probe waits for the game's player, puts it in a menu's input mode, the engine's Game and UI (the cursor shown; the
mouse captured while a button is held, as the mode has it, but not hidden then or locked to the window), makes a `UDreamButton` with the Create Dream Widget node's calls for the player, adds it to the
viewport off the middle of the screen, and waits until it has been drawn inside the viewport for ten frames. Then six
steps, three frames apart: a move onto the button's middle, a left button down and up there, a move to a corner, a down
and up there. After each it writes down where the viewport has its cursor, where the player sees the mouse, and what the
button announced (hovered, unhovered, pressed, released, clicked), into `<dir>/ClickSmoke.json`, and asks the game to exit.
The check passes a run whose button was hovered, pressed, released and clicked once, in that order, by the click on it,
and pressed and clicked by nothing more after the click away.

For the run, the probe turns `Slate.EnableSyntheticCursorMoves` off (and back on as it ends): Slate sends a move of its own
every frame from where the desk's cursor really is, which would carry the game's cursor off the button between the
probe's move and its press. It never moves the desk's cursor. Leave the mouse alone while the window is up: a real move
over the game's window is a move of the game's cursor too.

### Running it

The reference half of the text smoke test's commands, with the click probe's switch: the editor target, then the game on
uncooked content. A few seconds once the editor target is built.

```powershell
$Engine = 'F:\UnrealEngine\UE_Moon'
$Host_  = 'I:\UnrealProject_Moon\DEV_58\DreamGUITestHost'
$Proj   = "$Host_\DreamGUITestHost.uproject"
$Out    = "$Host_\Saved\ClickSmoke"

& "$Engine\Engine\Build\BatchFiles\Build.bat" DreamGUITestHostEditor Win64 Development -Project="$Proj" -WaitMutex -NoHotReloadFromIDE -NoEngineChanges -DisableAdaptiveUnity
& "$Engine\Engine\Binaries\Win64\UnrealEditor.exe" "$Proj" -game -windowed -ResX=1280 -ResY=720 `
    -DreamGUIClickSmoke="$Out\uncooked" -unattended -nosound -abslog="$Out\uncooked.log"
python "$Host_\Plugins\DreamGUI\Tools\Tests\check_click_smoke.py" "$Out\uncooked"
```

In the packaged text smoke test, the packaged game takes the switch as well -- the probe makes everything it clicks, so
there is nothing more to cook -- and the check takes both runs:

```powershell
& "$Host_\Saved\StagedBuilds\Windows\DreamGUITestHost\Binaries\Win64\DreamGUITestHost.exe" `
    -windowed -ResX=1280 -ResY=720 -DreamGUIClickSmoke="$Out\cooked" -unattended -nosound -abslog="$Out\cooked.log"
python "$Host_\Plugins\DreamGUI\Tools\Tests\check_click_smoke.py" "$Out\uncooked" "$Out\cooked"
```

The packaged run needs the game target built, which against a source engine compiles the whole engine into the host (see
"Building and running"): build it on purpose, as the text smoke test does.

## Creating it

```powershell
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -WhatIf   # see what it would do
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1
```

| Parameter | Default | |
| --- | --- | --- |
| `-Root` | `I:\UnrealProject_Moon\DEV_58\DreamGUITestHost` | The host directory. Refused on drive C unless `-AllowSystemDrive`. |
| `-RepoPath` | `I:\UnrealProject_Moon\DEV_58\DevTest\Plugins\DreamGUI` | Any working tree of the repository. |
| `-Branch` | `feat/tests-completion` | What the worktree must have (or is created with). |
| `-EngineRoot` | looked up from the `.uproject`'s engine association | |
| `-Force` | off | Replace files that differ from the template; each old one is kept as `<name>.bak-<timestamp>`. |
| `-KeepCurrentHead` | off | Accept an existing worktree on another branch or a detached commit. |
| `-Detach` | off | Follow `-Branch` with a detached HEAD. Needed when the branch is checked out in the repository's own working tree, since a branch can be checked out in one worktree at a time. A clean detached worktree is moved to the branch's tip on every run, so the host tests what has been committed. |
| `-AllowSystemDrive` | off | Accept `-Root` on drive C, for a machine whose only drive is C and has room on it (or set `DREAMGUI_ALLOW_DRIVE_C=1`). |
| `-WhatIf` | off | Report only. |

What it does, in order:

1. **Checks before touching anything**: git is on `PATH`; `-Root` is not on drive C (following links,
   so a link from D: into C: is caught too), is not inside the repository, and does not already hold
   another project; the repository is a git repository; the engine exists.
2. **The worktree.** If `Plugins/DreamGUI` exists it must be a worktree of that repository -- the same
   git directory, its own working tree rather than the repository's main one reached through a link,
   registered in `git worktree list` -- with `-Branch` checked out. If it does not exist, it is created
   with `git worktree add`. An existing worktree is never removed, reset or switched.
3. **The template.** Missing files are written; matching files are left alone (line endings and a
   byte-order mark do not count as differences); a file that differs is listed with its first
   differing line and kept, unless `-Force`.
4. **`.dreamgui-testhost.json`**, rewritten only when something in it changed.
5. Prints the command that builds and runs.

It is safe to run again at any time. Exit codes: `0` the host matches the template; `1` a check
failed (printed; nothing after the failing step was done); `2` the host is usable but does not match
the template (files kept without `-Force`, or `-WhatIf` found work to do).

Creating the worktree is the one thing the script writes outside `-Root`: git records every worktree
in the repository's `.git/worktrees/` directory.

## Building and running

The test runner owns building and running; it picks the host up by itself when the host's `.uproject`
exists:

```powershell
pwsh -NoProfile -File I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\Plugins\DreamGUI\Tools\Tests\Invoke-DreamGUITests.ps1 -Project I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\DreamGUITestHost.uproject
```

See `Tools/Tests/README.md` for presets, reports and exit codes.

The editor target (`DreamGUITestHostEditor`) uses the **shared** build environment: engine modules
already built in `Engine/Binaries/Win64` are linked against as they are, and only the host module and
the plugin are compiled. So the first build costs one plugin build, not an engine build -- provided
the engine is up to date. If the engine's source has changed since it was last built, building the
host rebuilds the changed engine modules, exactly as building any other project on this engine would:
that fails with LNK1104 while any editor has those DLLs loaded, and it can leave the other projects'
modules looking out of date to their next launch. Build the engine first in that case. The runner
builds with `-NoEngineChanges`, so instead of doing any of that it stops before the first action and
lists the engine files it would have written.

The generated code of engine modules is shared the same way: UnrealHeaderTool writes it into the
engine's own `Intermediate`, whichever project's build ran it. That only works while UHT's output is
the same for every project, and on this engine it was not: a type-less header included by another
header of its module (NetCore's `PushModel.h`, included by `FastArraySerializer.h`) was left out of
its package whenever the included header's own parse started after the includer's `#include` line --
a race between parse threads -- which changed `/Script/NetCore`'s hash in `NetCore.init.gen.cpp` and
made NetCore look out of date to whichever project built next. UE_Moon's UHT now marks those headers
after all parsing is done (`EpicGames.UHT`, `UhtHeaderFile.Resolve`), and the output is the same on
every run. With an engine that lacks that fix, a host build refused by `-NoEngineChanges` over NetCore
alone is this race, not a stale engine.

Never build the game target (`DreamGUITestHost`) casually: a game target against a source engine is
monolithic with its own build environment, which compiles the whole engine into this project. The packaged text smoke
test builds it on purpose (above); against a launcher engine that costs a plugin build, not an engine build.

## Moving to another branch or commit

The worktree is an ordinary checkout; change it with git, in the worktree:

```powershell
git -C I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\Plugins\DreamGUI switch <branch>
git -C I:\UnrealProject_Moon\DEV_58\DreamGUITestHost\Plugins\DreamGUI switch --detach <commit>
```

- **One branch, one worktree.** git refuses to check out a branch that another worktree already has
  -- the working copy in `DevTest/Plugins/DreamGUI` included. To test a branch that is checked out
  there, detach at it (`switch --detach main`) or make a branch of your own from it.
- Then run `New-DreamGUITestHost.ps1` again with `-Branch <branch>` (or `-KeepCurrentHead` for a
  detached commit). A template file that differs from what the host has is listed, and `-Force` brings
  it up to date.
- Undoing a fix to see its test go red, then putting it back -- on a detached HEAD, so the branch never
  carries the revert and nothing has to be reset afterwards:

  ```powershell
  git -C <worktree> switch --detach
  git -C <worktree> revert --no-edit <fix commit>
  # build, run only the tests that cover it (the setup script needs -KeepCurrentHead meanwhile)
  git -C <worktree> switch <branch>
  ```

The host builds exactly what is on disk in its worktree, uncommitted changes there included. It never
sees anything in your own working copy that is not committed and checked out into the worktree.

## The derived data cache

The host has no derived-data-cache configuration, on purpose, and shares the cache the working project
already warmed:

- The cache this engine actually reads and writes is the local **Zen** store. Its data directory is
  machine-wide (`[Zen.AutoLaunch] DataPath` in the engine's `BaseEngine.ini`, resolving to
  `%LOCALAPPDATA%\UnrealEngine\Common\Zen\Data`) and so is its namespace (`ue.ddc`): every project on
  this engine uses the same store. On this machine `%LOCALAPPDATA%\UnrealEngine\Common\Zen` is a link to
  `F:\Zen\Zen`, so the data is on F:. The setup script reports where the data physically lands and warns
  if it is on drive C; moving it is a machine-wide decision, not the host's.
- The per-project FileSystem store (`<project>\LocalDerivedDataCache`) is configured `DeleteOnly` by this
  engine, so it takes no reads or writes; the host's lands on the host's own drive anyway.
- A cache hit needs the same cache key, and shader keys include the project's renderer settings and
  targeted shader formats. That is why `Config/DefaultEngine.ini` copies those sections from DevTest
  verbatim. Change one of them and the first launch compiles every shader again.

Two other things write to drive C during a run. Shader compile workers exchange their job files in
`%TEMP%\UnrealShaderWorkingDir\`; the engine accepts `-ShaderWorkingDir=<dir>` on its command line to
put them elsewhere, which matters on a cold cache. And the trace server keeps its store under
`%LOCALAPPDATA%\UnrealEngine\Common\UnrealTrace`.

## How the host differs from DevTest

| | DevTest | Host |
| --- | --- | --- |
| Game viewport client | the engine's `UGameViewportClient` | `UDreamGameViewportClient`, as the plugin's README asks of a game: characters reach text fields through `InputChar`, not the US-only key table |
| Plugins | DreamGUI plus about thirty others | DreamGUI and Enhanced Input |
| Content | the project's own, including `/Game/UI/WBP_ControlsGallery` | the old-asset fixtures, and the smoke test's assets once `make_text_smoke_assets.py` made them |
| Startup map | a full showcase level | `/Engine/Maps/Entry` (one PlayerStart) |
| `[CoreRedirects]` | none | none: the plugin ships none since 1.0.0, so every asset loads under the names it was saved with |
| MoonToon ramp atlases | loaded from the MoonToon project plugin | `None` (the plugin is not there) |
| Editor layouts, per-user settings | whatever you last left them as | engine and plugin defaults |

Things that follow from this:

- **Text input.** A test that depends on which character road is live must set it up itself (the
  driver's switch for "the host delivers characters"). In DevTest the key-table fallback is what runs
  unless a test says otherwise; in the host the viewport client's road is.
- **`DreamGUI.Animation.Playback.ProjectGallery.ButtonSlidesIn`** checks DevTest's own gallery widget
  and says "nothing to check" wherever that asset does not exist. It passes in the host without having
  tested anything.
- **Class sweeps** (tests that walk every loaded class deriving from a DreamGUI base) see only the
  plugin's own classes in the host, and also other plugins' subclasses in DevTest.
- **No redirect hides an old name.** With no `[CoreRedirects]` anywhere, an asset -- the plugin's own, a
  fixture -- that still names a moved or renamed type fails to load in a run, instead of loading through a
  redirect. (`NavigationSelection_Sprite` and `DefaultFont_Bitmap` keep LGUI's asset names; the classes they
  name are current.)

## Known limitations

- Only what is checked out into the worktree is tested. Uncommitted work in your own working copy --
  an edited material, an unsaved asset -- is not in the host until it is committed and checked out
  there.
- The first build compiles the whole plugin (every module, the tests included) into the worktree's own
  `Intermediate/`, which takes a while and a few gigabytes on the host's drive.
- Every shader-affecting setting is DevTest's. That is what keeps the cache warm, and it also means the
  host renders with ray tracing, Lumen and Substrate on, like DevTest does.
