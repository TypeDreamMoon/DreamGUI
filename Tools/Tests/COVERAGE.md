# Interaction coverage

"Tested" is measured, not felt: every interactive control or behaviour, crossed with every way a
player reaches it and every configuration it ships in, is a cell of one table, and a cell is either
covered by a driver test, declared not applicable with a reason, or a hole.

- **Rows** are the controls and behaviours: the `UCLASS`es of `Source/DreamGUI/Public/Controls/*.h`
  and `Source/DreamGUI/Public/Interaction/*.h`, grouped into rows and filtered in
  [`coverage.json`](coverage.json). A class that is in neither its `rows` nor its `excluded` list
  appears as an *unclassified* row full of holes, so a new control cannot slip past the table.
- **Columns** are the inputs crossed with the configurations:

  | Input | Tag | What the test does |
  |---|---|---|
  | Mouse | `[Pointer]` | moves, hovers, presses, clicks, drags or scrolls the pointer |
  | Touch | `[Touch]` | puts fingers down, moves and lifts them (taps, touch drags) |
  | Gamepad and keyboard navigation | `[Nav]` | navigates by direction, accepts, goes back, or drives the virtual cursor |
  | Keyboard text | `[Text]` | types characters or editing keys into a text control (only rows marked `text`; for the key selector, the key it captures) |

  | Configuration | Tag | What it means |
  |---|---|---|
  | Default | `[Animated]` | the control as shipped: its transitions and animations running (the rig's world has a game instance, so tweens tick). A test that turns them off to make its assertions easy does **not** cover this column |
  | Disabled | `[Disabled]` | the control disabled or made non-interactable, before or during the interaction |
  | Scaled canvas | `[Scaled]` | the root canvas scaled against a reference resolution (0.5x, 2x, ...) |
  | World space | `[World]` | the panel in the level, hit through a camera (a world-space raycaster, a render target on a mesh) |

## Declaring what a test covers

Register tags right after the declaration, with the **same full name** — the framework files tags
under the exact name given (`FAutomationTestFramework::RegisterAutomationTestTags`), and a typo files
them under a test that does not exist, silently:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonTapClicksTest,
	"DreamGUI.Button.ATapOnTheButtonClicksItOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamButtonTapClicksTest, "DreamGUI.Button.ATapOnTheButtonClicksItOnce", "[Touch][Animated]")

bool FDreamButtonTapClicksTest::RunTest(const FString& Parameters)
```

Rules (the static checks enforce the first three):

1. Every tag comes from the vocabulary above (`inputs`, `configs` and `extraTags` in `coverage.json`).
2. A test with an input tag carries at least one configuration tag; it covers every pair of its
   input and configuration tags (`[Pointer][Touch][Animated][Scaled]` covers four cells).
3. The name in `REGISTER_SIMPLE_AUTOMATION_TEST_TAGS` is the test's full name, character for character.
4. Tag only what the test would catch: a tag says "if this input under this configuration broke,
   this test would go red". A test that merely passes through a state does not cover it.
5. Only driver tests count — tests that reach the control through `FDreamDriverRig` (or the PIE rig)
   the way a player would. A test that calls a control's handlers directly is not a coverage test.
6. The row comes from the name: a test belongs to every row one of whose `areas` its name starts
   with, after `DreamGUI.` (`"Button"` means `DreamGUI.Button.*`). A journey that crosses several
   controls is named after the one it is about; add an area to a row when a new naming scheme needs one.
7. A cell that cannot apply (say, disabling a subsystem that has no disabled state) is declared in
   the row's `na` in `coverage.json` — key `Input`, `Config` or `Input.Config`, value the reason —
   never left as a hole and never covered by a test that proves nothing. The Text column needs no
   such entry: it only exists for rows marked `text`.

## Drawing the table

```
python Tools/Tests/coverage_matrix.py                 # tags only: the real count
python Tools/Tests/coverage_matrix.py --heuristic     # also guess untagged tests from their bodies
python Tools/Tests/coverage_matrix.py --heuristic --write-doc   # refresh the section below
python Tools/Tests/coverage_matrix.py --fail-on-holes # exit 1 while any hole remains
```

`--heuristic` exists for the first picture, before tests carry tags: it recognises the controls from
test names and the columns from what a test body calls (`Click(`, `TouchDown(`, `Navigate(`,
`Type(`, a scaled canvas, a world-space camera, `SetIsEnabled(false)`, a duration set to zero), only
for tests that use the driver. Its cells are marked `?` and are guesses; tag the tests to make them
count for real.

The section between the markers below is generated; edit `coverage.json` or the tests, then
regenerate it.

<!-- coverage-matrix:begin -->
Not generated yet: run `python Tools/Tests/coverage_matrix.py --heuristic --write-doc`.
<!-- coverage-matrix:end -->
