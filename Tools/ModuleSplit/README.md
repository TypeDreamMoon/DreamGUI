# Splitting a runtime module off the core

The core, `Source/DreamGUI`, is being split into runtime modules by layer. `Tools/Tests/module-owners.csv`
says which module each runtime file is headed for; the two scripts here move one module's files and write
the CoreRedirects that keep old assets loading. Both print what they would do unless given `--apply`.

| File | What it does |
|---|---|
| `move_module_files.py <Module>` | `git mv`s the module's files to `Source/<Module>`, same path under `Public/` or `Private/`; rewrites the includes that only worked inside one module (same directory, `../`, `"DreamGUI/Public/..."`) into the module-relative form; renames `DREAMGUI_API` to the module's macro; updates `module-owners.csv`. Refuses while a file that stays in the core still includes a moving one |
| `generate_split_redirects.py <Module>` | Scans the moved headers for reflected types and appends one redirect per type, from `/Script/DreamGUI` to `/Script/<Module>`, to `Config/DefaultDreamGUI.ini`; points the existing entries that led to one of those types at the new package, since redirects do not chain |

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
4. `python Tools/ModuleSplit/generate_split_redirects.py <Module> --apply`.
5. Add the module to `DreamGUIEditor.Build.cs` and `DreamGUITests.Build.cs` (the tests also get its
   `Private` directory), and to every runtime module above it that uses it.
6. Update the metadata strings that name a moved type by path (`AllowedClasses`, `MustImplement`, ...): the
   engine reads those as they are, without redirects.
7. Build, then run the whole suite. `DreamGUI.Packaging.EveryTypeInASplitOffModuleAnswersToItsOldCorePath`
   checks that every type in the new package answers to its old path, and the old-asset fixtures, the asset
   smoke test and the redirect tests check the rest.
8. Resave the plugin's assets that name a moved type, and regenerate `Docs/Reference`.
