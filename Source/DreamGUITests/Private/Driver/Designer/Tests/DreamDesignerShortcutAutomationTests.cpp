// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetBlueprintEditor.h"

#include "Editor.h"
#include "Editor/Transactor.h"

/*
 * The designer's keyboard shortcuts, pressed on a keyboard: Delete, Ctrl+C and Ctrl+V, Ctrl+D, Ctrl+Z and Ctrl+Y.
 *
 * A shortcut is not the viewport's to answer. Slate sends the key to the widget with the keyboard focus -- the designer
 * viewport, once an author has clicked into it -- and from there up the focus path until something takes it: the viewport
 * client (which nudges on the arrows and frames on F), the viewport's own command list -- which holds the designer's
 * Delete, Copy, Cut, Paste and Duplicate (FDreamWidgetBlueprintEditor::GetDesignerCommandList) -- and the asset editor that
 * holds them (SStandaloneAssetEditorToolkitHost::OnKeyDown), whose toolkit command list maps Undo and Redo to the editor's
 * undo (FBlueprintEditor::CreateDefaultCommands). The test before this file stood,
 * Designer.Driver.UndoRemovesTheDroppedWidgetAndRedoBringsItBack, called Undo; these press the keys, so the whole route --
 * focus, bubbling, the chord match -- is what is being held to UMG's, whose design surface answers the same generic
 * commands from its designer's own list (FWidgetBlueprintEditor::DesignerCommandList, SDesignerView::OnKeyDown).
 *
 * NonNullRHI: Slate has to have laid the designer's tab out into a window for the viewport to take the keyboard, and
 * under -nullrhi it never does. Across engine frames, as Slate routes the keys within its own frame.
 */
namespace DreamDesignerShortcutTestLocal
{
	using namespace DreamTests;

	/** A plain widget dropped a little left of the middle, made the selection, and remembered as Target. */
	void EnqueueDropAndSelect(FAutomationTestBase* InTest, const FDesignerLatentRef& InState)
	{
		EnqueueDesignerAction(InState, [InTest, InState]()
		{
			FDreamDesignerDriver& Driver = *InState->Driver;
			const FIntPoint Size = Driver.ViewportPixelSize();
			UDreamWidget* Template = DropOntoRootAndFindTemplate(Driver, /*Plain Widget row*/ nullptr, FIntPoint(Size.X * 2 / 5, Size.Y / 2));
			if (!InTest->TestNotNull(TEXT("A plain widget was dropped under the root"), Template))
			{
				InState->bAlive = false;
				return;
			}
			InState->Made.Add(TEXT("Target"), Template);
			SelectOnlyInDesigner(Driver, Template);
		});
		EnqueueDesignerFrames(InState, 2);
	}

	/** The authored widgets directly under the asset's root. */
	TArray<UDreamWidget*> AuthoredChildren(const FDesignerLatentRef& InState)
	{
		return LiveChildrenOf(DesignerTemplateRoot(InState->Asset.Blueprint));
	}

	/** The designer clipboard is one for the whole process and outlives the designer that filled it: not left to the next test. */
	void ForgetClipboardAtTeardown(const FDesignerLatentRef& InState)
	{
		InState->OnTeardown.Add([]() { FDreamWidgetBlueprintEditor::DesignerClearClipboard(); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDeleteKeyTest,
	"DreamGUI.Designer.Keys.DeleteRemovesTheSelectedWidgetAndCtrlZBringsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * A widget selected and Delete pressed on the viewport: it is gone from the asset, as UMG's Delete removes the selection.
 * Ctrl+Z, pressed the same way, brings it back.
 */
bool FDreamDesignerDeleteKeyTest::RunTest(const FString&)
{
	using namespace DreamDesignerShortcutTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerDeleteKey"));
	EnqueueDesignerFrames(State, 2);
	EnqueueDropAndSelect(this, State);
	EnqueueDesignerKeyboardFocus(State, this);
	EnqueueDesignerShortcut(State, this, EKeys::Delete);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		TestFalse(TEXT("Delete took the selected widget out of the asset"), AuthoredChildren(State).Contains(State->Get(TEXT("Target"))));
		TestEqual(TEXT("...leaving the root empty"), AuthoredChildren(State).Num(), 0);
	});
	EnqueueDesignerShortcut(State, this, EKeys::Z, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		TestEqual(TEXT("Ctrl+Z brought the widget back under the root"), AuthoredChildren(State).Num(), 1);
		AddInfo(FString::Printf(TEXT("The keys went %s; %d frames waited."), *State->Route, State->Frames));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerCopyPasteKeysTest,
	"DreamGUI.Designer.Keys.CtrlCThenCtrlVWithNothingSelectedPastesACopyAtTheRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * Ctrl+C on a selected widget, the selection cleared, Ctrl+V: a copy of it -- the same class, another widget -- lands
 * under the root beside the original, which is where UMG pastes when nothing is selected. The original is untouched.
 */
bool FDreamDesignerCopyPasteKeysTest::RunTest(const FString&)
{
	using namespace DreamDesignerShortcutTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerCopyPasteKeys"));
	ForgetClipboardAtTeardown(State);
	EnqueueDesignerFrames(State, 2);
	EnqueueDropAndSelect(this, State);
	EnqueueDesignerKeyboardFocus(State, this);
	EnqueueDesignerShortcut(State, this, EKeys::C, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 1);
	EnqueueDesignerAction(State, [this, State]()
	{
		TestTrue(TEXT("Ctrl+C filled the designer's clipboard"), FDreamWidgetBlueprintEditor::DesignerHasClipboardContent());
		SelectNothingInDesigner(*State->Driver);
	});
	EnqueueDesignerFrames(State, 1);
	EnqueueDesignerShortcut(State, this, EKeys::V, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		const UDreamWidget* Original = State->Get(TEXT("Target"));
		const TArray<UDreamWidget*> Children = AuthoredChildren(State);
		if (TestEqual(TEXT("Ctrl+V put a second widget under the root"), Children.Num(), 2))
		{
			const UDreamWidget* Pasted = Children[0] == Original ? Children[1] : Children[0];
			TestTrue(TEXT("...the original among them"), Children.Contains(Original));
			TestTrue(TEXT("...and a copy of it: another widget of its class"),
				Pasted != Original && Original != nullptr && Pasted->GetClass() == Original->GetClass());
		}
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDuplicateKeyTest,
	"DreamGUI.Designer.Keys.CtrlDDuplicatesTheSelectedWidgetIntoTheSameParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * Ctrl+D on a selected widget: a duplicate of it in the same parent, as UMG's Duplicate places the copy beside the
 * original. One undo takes the duplicate away again.
 */
bool FDreamDesignerDuplicateKeyTest::RunTest(const FString&)
{
	using namespace DreamDesignerShortcutTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerDuplicateKey"));
	EnqueueDesignerFrames(State, 2);
	EnqueueDropAndSelect(this, State);
	EnqueueDesignerKeyboardFocus(State, this);
	EnqueueDesignerShortcut(State, this, EKeys::D, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		const UDreamWidget* Original = State->Get(TEXT("Target"));
		const TArray<UDreamWidget*> Children = AuthoredChildren(State);
		if (TestEqual(TEXT("Ctrl+D put a duplicate beside the original"), Children.Num(), 2))
		{
			const UDreamWidget* Duplicate = Children[0] == Original ? Children[1] : Children[0];
			TestTrue(TEXT("...of the original's class"), Original != nullptr && Duplicate != Original && Duplicate->GetClass() == Original->GetClass());
			TestTrue(TEXT("...in the original's parent"), Original != nullptr && Duplicate->GetParent() == Original->GetParent());
		}
	});
	EnqueueDesignerShortcut(State, this, EKeys::Z, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State]()
	{
		TestEqual(TEXT("Ctrl+Z took the duplicate away again"), AuthoredChildren(State).Num(), 1);
	});
	EnqueueDesignerTeardown(State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerUndoRedoKeysTest,
	"DreamGUI.Designer.Keys.CtrlZTakesBackADragAndCtrlYDoesItAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

/*
 * A widget dragged across the surface with the pointer, then Ctrl+Z: it is back where it was. Ctrl+Y: it is where the
 * drag left it. The editor's own undo and redo, reached by their keys.
 */
bool FDreamDesignerUndoRedoKeysTest::RunTest(const FString&)
{
	using namespace DreamDesignerShortcutTestLocal;
	const FDesignerLatentRef State = OpenLatentDesigner(*this, TEXT("DesignerUndoRedoKeys"));
	State->Preferences = MakeShared<FScopedDesignerPreferences>();
	State->Preferences->SetGridSnap(false);
	State->Preferences->SetGuides(false);
	TSharedRef<FVector2D> Before = MakeShared<FVector2D>(FVector2D::ZeroVector);
	TSharedRef<FVector2D> Dragged = MakeShared<FVector2D>(FVector2D::ZeroVector);
	TSharedRef<FIntPoint> From = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
	EnqueueDesignerFrames(State, 2);
	EnqueueDropAndSelect(this, State);
	EnqueueDesignerAction(State, [this, State, Before, From]()
	{
		FDreamDesignerDriver& Driver = *State->Driver;
		UDreamWidget* Template = State->Get(TEXT("Target"));
		const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
		if (!TestTrue(TEXT("The selected widget is on screen"), Rect.IsSet()))
		{
			State->bAlive = false;
			return;
		}
		*Before = Template->GetAnchoredPosition();
		*From = PointInBox(Rect.GetValue(), 0.25, 0.25);
	});
	// The press point is read when the drag begins: the widget's place on screen is known only once it was dropped.
	EnqueueDesignerPointerDrag(State, [From]() { return *From; }, [From]() { return *From + FIntPoint(70, 40); }, /*Steps*/ 4);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, Before, Dragged]()
	{
		const UDreamWidget* Target = State->Get(TEXT("Target"));
		if (!TestNotNull(TEXT("The dragged widget is still there"), Target))
		{
			State->bAlive = false;
			return;
		}
		*Dragged = Target->GetAnchoredPosition();
		// What the drag left to undo: Ctrl+Z takes back the newest entry, which has to be the drag's and not the drop's.
		const int32 Queued = GEditor != nullptr && GEditor->Trans != nullptr ? GEditor->Trans->GetQueueLength() : 0;
		const FTransaction* Newest = Queued > 0 ? GEditor->Trans->GetTransaction(Queued - 1) : nullptr;
		const FString NewestTitle = Newest != nullptr ? Newest->GetContext().Title.ToString() : FString(TEXT("nothing"));
		if (!TestFalse(FString::Printf(TEXT("The drag moved the widget: %s -> %s (the newest undo entry: %s)"), *Before->ToString(), *Dragged->ToString(), *NewestTitle),
			Dragged->Equals(*Before, 1.0)))
		{
			// With nothing of the drag's to undo, Ctrl+Z would take the drop back and the widget with it.
			State->bAlive = false;
		}
	});
	EnqueueDesignerKeyboardFocus(State, this);
	EnqueueDesignerShortcut(State, this, EKeys::Z, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, Before]()
	{
		const UDreamWidget* Target = State->Get(TEXT("Target"));
		if (!TestNotNull(TEXT("Ctrl+Z left the widget there, taking back only the drag"), Target))
		{
			State->bAlive = false;
			return;
		}
		const FVector2D Now = Target->GetAnchoredPosition();
		TestTrue(FString::Printf(TEXT("Ctrl+Z put it back: %s, was %s"), *Now.ToString(), *Before->ToString()), Now.Equals(*Before, 0.01));
	});
	EnqueueDesignerShortcut(State, this, EKeys::Y, EDreamDriverModifierKeys::Ctrl);
	EnqueueDesignerFrames(State, 2);
	EnqueueDesignerAction(State, [this, State, Dragged]()
	{
		const UDreamWidget* Target = State->Get(TEXT("Target"));
		if (!TestNotNull(TEXT("Ctrl+Y left the widget there"), Target))
		{
			State->bAlive = false;
			return;
		}
		const FVector2D Now = Target->GetAnchoredPosition();
		TestTrue(FString::Printf(TEXT("Ctrl+Y did the drag again: %s, the drag left it at %s"), *Now.ToString(), *Dragged->ToString()), Now.Equals(*Dragged, 0.01));
	});
	EnqueueDesignerTeardown(State);
	return true;
}

#endif
