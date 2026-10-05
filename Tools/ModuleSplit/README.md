# Splitting a runtime module off the core

The core, `Source/DreamGUI`, was split into runtime modules by layer, one module at a time, with the scripts
here; they stay for the next module to leave it. `Tools/Tests/module-owners.csv` says which module each runtime
file belongs to. The scripts move one module's files, list the types that moved, and check what a game build
will see. `move_module_files.py` and `retarget_script_paths.py` print what they would do unless given `--apply`.

| File | What it does |
|---|---|
| `move_module_files.py <Module>` | `git mv`s the module's files to `Source/<Module>`, same path under `Public/` or `Private/`; rewrites the includes that only worked inside one module (same directory, `../`, `"DreamGUI/Public/..."`) into the module-relative form; renames `DREAMGUI_API` to the module's macro; updates `module-owners.csv`. Refuses while a file that stays in the core still includes a moving one |
| `moved_types.py <Module>` | Scans the moved headers for reflected types and lists each with its old `/Script/DreamGUI` path and its new one; `--redirects` prints a `[CoreRedirects]` block for them instead, for a project's own config. Writes nothing: the plugin ships no CoreRedirects |
| `retarget_script_paths.py <Module>` | Rewrites every `/Script/DreamGUI.<moved type>` in the plugin's sources and docs to the new package -- metadata strings above all, which the engine reads as they are. Takes the names from `moved_types.py`. Leaves the tests and the old-asset snapshot alone, and lists what it left |
| `check_game_includes.py` | Lists, for every runtime `.cpp` and header, the engine types it uses without its own includes bringing them in -- as a game build without a shared PCH sees it: `WITH_EDITOR` code and the engine's deprecated transitive includes dropped. BuildPlugin compiles the game target that way, so a file that only compiled through its unity-blob neighbours breaks when a split regroups them. Prints candidates to read; a name used only through a pointer is fine |

## The steps, in order

1. Cut every include from a lower layer into the module first, in the core, with the tests green: the move
   script lists the ones left, and `static_checks.py` (rules `layering`, `layering-stale`) keeps
   `Tools/Tests/layering-allow.json` honest. A reflected type the core names without an include -- a
   forward-declared `UPROPERTY` type -- does not show up there; it shows up when the core stops linking.
2. Add the module: `Source/<Module>/<Module>.Build.cs`, a module class whose `StartupModule` calls
   `DreamUI::RegisterRuntimeScriptPackage("/Script/<Module>")` and whose `ShutdownModule` unregisters it and
   calls `FDreamUIWidgetRegistry::UnregisterModule`, and a `.uplugin` entry: `Runtime`, `PostConfigInit`, the
   core's `PlatformAllowList`, in layer order.
3. `python Tools/ModuleSplit/move_module_files.py <Module> --apply`. The renames are now staged: look at
   `git diff --cached --stat` before any other commit.
4. `python Tools/ModuleSplit/moved_types.py <Module>` lists the types that moved. The plugin ships no
   CoreRedirects (`DreamGUI.Packaging.ThePluginShipsNoCoreRedirects`), so an asset saved before the split that
   names one of them no longer finds it -- the plugin's own and every project's. `--redirects` prints the block
   that bridges them: put it in the working project's `Config/DefaultEngine.ini` until step 9 is done, and hand it
   on in the CHANGELOG and `Docs/Migration.md`, the way 1.0.0 hands on 2.1.0's. Entries in 2.1.0's block that
   lead to a moved type's old path lead nowhere after the split (a redirect does not chain); the migration notes
   have to say so.
5. Add the module to `DreamGUIEditor.Build.cs` and `DreamGUITests.Build.cs` (the tests also get its
   `Private` directory), and to every runtime module above it that uses it.
6. `python Tools/ModuleSplit/retarget_script_paths.py <Module> --apply`, for the metadata strings that name a
   moved type by path (`AllowedClasses`, `MustImplement`, ...) and for the docs: the engine reads a metadata
   string as it is.
7. `python Tools/ModuleSplit/check_game_includes.py`, and give every file it lists that really uses a type by value
   the include it needs. The editor build cannot show these; BuildPlugin's game target shows them half an hour later.
8. Build, then run the whole suite. A class that never carried an API macro, because every user sat in the same
   module, fails to link once one of them stays behind: the move script only renames macros that are there. The
   renderer's vertex buffer was one. `DreamGUI.Packaging.EveryTypeInASplitOffModuleAnswersToItsOldCorePath`
   checks that every type in the new package answers to its old path where the plugin resolves one itself, and
   the old-asset fixtures and the asset smoke test check the rest. A fixture that names a moved type fails to
   load: replacing the fixtures is a change the CHANGELOG states (`Tools/TestHost/README.md`).
9. Resave the plugin's assets that name a moved type, take the borrowed block out of `Config/DefaultEngine.ini`
   again, and regenerate `Docs/Reference`.
