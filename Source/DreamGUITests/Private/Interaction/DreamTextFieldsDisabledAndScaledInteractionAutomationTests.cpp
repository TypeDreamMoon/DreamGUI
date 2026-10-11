// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamEditableText.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * THE THREE TEXT FIELDS -- THE BOXED ONE, THE BORDERLESS ONE AND THE MULTI-LINE ONE -- DISABLED, AND ON A SCALED CANVAS.
 *
 * Disabled is UWidget::SetIsEnabled(false) on the field. Slate's hit test cuts a pointer's path at the first disabled widget
 * (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp), so a click or a finger on the field reaches
 * nothing of it and gives it no focus, and keys pressed afterwards type nothing; a finger is the same press, because
 * FSlateApplication hands an unanswered touch on as a mouse press. Tab and the D-pad find their next stop through
 * FHittestGrid::FindNextFocusableWidget, which passes over a widget that is not enabled. Enabled again, the field is a
 * field: a click, a tap or a Tab edits it.
 *
 * Scaled is the rig's canvas scaled with the screen to one half and to double. SEditableText puts its cursor at the
 * character boundary under the press, found in the text's own local space (FSlateEditableTextLayout::HandleMouseButtonDown),
 * so a click or a tap on the caret between two characters puts the caret there at any scale; and the D-pad picks the
 * nearest focusable widget in the direction pressed by where the widgets are drawn, so it reaches the field below a
 * button at any scale too.
 *
 * Every test is the same interaction done to each of the three classes that have it, through one helper per interaction.
 */
namespace DreamTextFieldsCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };
	const FVector2D SingleLineSize(360.0, 40.0);
	const FVector2D MultiLineSize(360.0, 160.0);

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	/** "[scale 0.5] what", so a failure says which scale it failed at. */
	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	/** The rig at InScale, bound, its scale checked; false when it did not come up as asked. */
	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}

	UDreamText* ShownTextOf(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->TextNode != nullptr ? Cast<UDreamText>(InField->TextNode->GetVisual()) : nullptr;
	}

	/** The viewport pixel of the caret before caret InCaretIndex, off the text's own caret table, or unset. */
	TOptional<FVector2D> CaretPixel(UDreamText& InShown, int32 InCaretIndex)
	{
		int32 CaretIndex = InCaretIndex;
		FVector2f CaretPosition(0.0f, 0.0f);
		int32 LineIndex = 0;
		int32 VisibleStartIndex = 0;
		InShown.FindCaretByIndex(CaretIndex, CaretPosition, LineIndex, VisibleStartIndex);
		if (CaretIndex != InCaretIndex)
		{
			return TOptional<FVector2D>();
		}
		return FDreamDriverProjection::WidgetLocalPointToPixel(InShown.GetWidget(), FVector2D(CaretPosition.X, CaretPosition.Y));
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	/** Player 0's focus, as the input system has it. */
	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	float DoubleClickTimeOf(const FDreamDriverRig& InRig)
	{
		return InRig.EventSystem() != nullptr ? InRig.EventSystem()->GetDoubleClickTime() : 0.0f;
	}

	/** The text a field is given before a test types into it, and what one more line and an X make of it. */
	struct FTypedText
	{
		/** The characters typed, the first line's and, for a multi-line field, the second's after an Enter. */
		FString FirstLine;
		FString SecondLine;
		/** The character index the caret is aimed at, and what an X typed there makes of the whole text. */
		int32 AimedCharIndex = 0;
		FString WithX;
	};

	FTypedText TypedTextFor(bool bInMultiLine)
	{
		FTypedText Typed;
		if (bInMultiLine)
		{
			// "first line\nsecond |line": the caret before the second line's "line".
			Typed.FirstLine = TEXT("first line");
			Typed.SecondLine = TEXT("second line");
			Typed.AimedCharIndex = 18;
			Typed.WithX = TEXT("first line\nsecond Xline");
		}
		else
		{
			// "alpha |bravo": the caret before the second word.
			Typed.FirstLine = TEXT("alpha bravo");
			Typed.AimedCharIndex = 6;
			Typed.WithX = TEXT("alpha Xbravo");
		}
		return Typed;
	}

	/**
	 * Click into the field named InName, type the text (and, for a multi-line field, Enter and its second line), then wait out
	 * the double-click time so the next press is a press of its own. False, having said why, when any of it did not happen.
	 */
	bool TypeAndWait(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FString& InName, const FTypedText& InTyped, double InScale)
	{
		FDreamElementRef FieldElement = InRig.Driver()->Find(FDreamBy::Name(InName));
		bool bTyped = FieldElement->Type(InTyped.FirstLine);
		if (!InTyped.SecondLine.IsEmpty())
		{
			bTyped = bTyped && InRig.Driver()->Sequence().Key(EKeys::Enter).Perform() && FieldElement->Type(InTyped.SecondLine);
		}
		return InTest.TestTrue(At(InScale, TEXT("Clicking into the field and typing completes")), bTyped)
			&& InTest.TestTrue(At(InScale, TEXT("Waiting out the double-click time completes")),
				InRig.Driver()->Sequence().WaitSeconds(DoubleClickTimeOf(InRig) + 0.1f).Perform());
	}

	/** The pixel of the caret before the typed text's aimed character, through the field's own caret table. */
	TOptional<FVector2D> AimedCaretPixel(UDreamTextInput* InField, const FTypedText& InTyped, bool bInMultiLine)
	{
		UDreamText* Shown = ShownTextOf(InField);
		if (Shown == nullptr)
		{
			return TOptional<FVector2D>();
		}
		// A multi-line text counts each line's end caret too, so its caret index is asked of the text; a single line's caret
		// index is its character index.
		const int32 CaretIndex = bInMultiLine ? Shown->GetCaretIndexByCharIndex(InTyped.AimedCharIndex) : InTyped.AimedCharIndex;
		return CaretPixel(*Shown, CaretIndex);
	}

	/**
	 * A disabled field clicked, then keys pressed: no edit, no focus, no change. Enabled again, the same click edits it.
	 */
	template<class TField>
	bool RunDisabledClickAndKeys(FAutomationTestBase& InTest, const FVector2D& InSize)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(&InTest);
		TField* Field = Rig.IsUsable() ? Rig.MakeControl<TField>(TEXT("Notes"), nullptr, InSize) : nullptr;
		if (!InTest.TestNotNull(TEXT("The rig and a field came up"), Field))
		{
			return false;
		}
		Field->SetText(TEXT("kept"));
		Field->SetIsEnabled(false);
		Rig.PumpFrames(1);

		FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Widget(Field));
		InTest.TestTrue(TEXT("Clicking the disabled field completes"), FieldElement->Click());
		InTest.TestFalse(TEXT("The click started no edit"), IsEditing(Field));
		InTest.TestFalse(TEXT("...and did not give the field the focus"), IsPartOf(FocusOf(Rig), Field));
		InTest.TestTrue(TEXT("Pressing A, Backspace and Enter completes"),
			Rig.Driver()->Sequence().Key(EKeys::A).Key(EKeys::BackSpace).Key(EKeys::Enter).Perform());
		InTest.TestEqual(TEXT("The keys typed nothing into it, took nothing out and broke no line"), Field->GetText(), FString(TEXT("kept")));

		Field->SetIsEnabled(true);
		Rig.PumpFrames(1);
		InTest.TestTrue(TEXT("Clicking it again once it is enabled completes"),
			Rig.Driver()->Sequence().WaitSeconds(DoubleClickTimeOf(Rig) + 0.1f).Click(FDreamBy::Widget(Field)).Perform());
		InTest.TestTrue(TEXT("Enabled, the same click edits it"), IsEditing(Field));
		return true;
	}

	/** A finger tapped on a disabled field: no edit, no focus. Enabled again, the same tap edits it. */
	template<class TField>
	bool RunDisabledTap(FAutomationTestBase& InTest, const FVector2D& InSize)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(&InTest);
		TField* Field = Rig.IsUsable() ? Rig.MakeControl<TField>(TEXT("Notes"), nullptr, InSize) : nullptr;
		if (!InTest.TestNotNull(TEXT("The rig and a field came up"), Field))
		{
			return false;
		}
		Field->SetText(TEXT("kept"));
		Field->SetIsEnabled(false);
		Rig.PumpFrames(1);

		FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Widget(Field));
		InTest.TestTrue(TEXT("A finger tapped on the disabled field completes"), FieldElement->Tap(0));
		InTest.TestFalse(TEXT("The tap started no edit"), IsEditing(Field));
		InTest.TestFalse(TEXT("...and did not give the field the focus"), IsPartOf(FocusOf(Rig), Field));
		InTest.TestTrue(TEXT("Pressing A completes"), Rig.Driver()->Sequence().Key(EKeys::A).Perform());
		InTest.TestEqual(TEXT("The key typed nothing into it"), Field->GetText(), FString(TEXT("kept")));

		Field->SetIsEnabled(true);
		Rig.PumpFrames(1);
		InTest.TestTrue(TEXT("Waiting out the double-click time completes"), Rig.Driver()->Sequence().WaitSeconds(DoubleClickTimeOf(Rig) + 0.1f).Perform());
		InTest.TestTrue(TEXT("A finger tapped on it once it is enabled completes"), FieldElement->Tap(1));
		InTest.TestTrue(TEXT("Enabled, the same tap edits it"), IsEditing(Field));
		return true;
	}

	/**
	 * A button, the field, a button, one after another in the tree. With the field disabled, Tab from the first button goes
	 * to the second, passing the field by; enabled again, Shift+Tab from the second button lands in the field and edits it.
	 */
	template<class TField>
	bool RunDisabledTab(FAutomationTestBase& InTest, const FVector2D& InSize)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
		Rig.BindTest(&InTest);
		UDreamButton* Before = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Before"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 140.0)) : nullptr;
		TField* Field = Before != nullptr ? Rig.MakeControl<TField>(TEXT("Notes"), nullptr, InSize, FVector2D::ZeroVector) : nullptr;
		UDreamButton* After = Field != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("After"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, -140.0)) : nullptr;
		if (!InTest.TestTrue(TEXT("The rig, two buttons and the field between them came up"), After != nullptr))
		{
			return false;
		}
		Field->SetIsEnabled(false);
		Rig.PumpFrames(1);

		InTest.TestTrue(TEXT("The first Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
		if (!InTest.TestTrue(TEXT("The first Tab lands on the button before the field"), IsPartOf(FocusOf(Rig), Before)))
		{
			return false;
		}
		InTest.TestTrue(TEXT("The second Tab completes"), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
		InTest.TestTrue(TEXT("The second Tab passed over the disabled field to the button after it"), IsPartOf(FocusOf(Rig), After));
		InTest.TestFalse(TEXT("...and started no edit"), IsEditing(Field));

		Field->SetIsEnabled(true);
		Rig.PumpFrames(1);
		InTest.TestTrue(TEXT("Shift+Tab completes"), Rig.Driver()->Sequence().ShiftTab().WaitFrames(1).Perform());
		InTest.TestTrue(TEXT("Enabled again, the field is the stop before the second button and Shift+Tab edits it"), IsEditing(Field));
		InTest.TestTrue(TEXT("Typing completes"), Rig.Driver()->Sequence().Type(TEXT("ab")).Perform());
		InTest.TestEqual(TEXT("The characters went into the field"), Field->GetText(), FString(TEXT("ab")));
		return true;
	}

	/**
	 * At half and at double: the field typed into, then a click on the caret before the aimed character. The field is still
	 * being edited, nothing is selected, and the X typed next goes in there.
	 */
	template<class TField>
	bool RunScaledClickCaret(FAutomationTestBase& InTest, const FVector2D& InSize, bool bInMultiLine)
	{
		const FTypedText Typed = TypedTextFor(bInMultiLine);
		for (const double Scale : Scales)
		{
			FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
			if (!ComeUp(InTest, Rig, Scale))
			{
				return false;
			}
			TField* Field = Rig.MakeControl<TField>(TEXT("Notes"), nullptr, InSize);
			if (!InTest.TestNotNull(*At(Scale, TEXT("A field came up")), Field))
			{
				return false;
			}
			Rig.PumpFrames(1);
			if (!TypeAndWait(InTest, Rig, TEXT("Notes"), Typed, Scale)
				|| !InTest.TestEqual(*At(Scale, TEXT("The field holds what was typed")), Field->GetText(),
					Typed.SecondLine.IsEmpty() ? Typed.FirstLine : Typed.FirstLine + TEXT("\n") + Typed.SecondLine))
			{
				return false;
			}
			const TOptional<FVector2D> Aimed = AimedCaretPixel(Field, Typed, bInMultiLine);
			if (!InTest.TestTrue(At(Scale, TEXT("The caret aimed at is on the viewport")), Aimed.IsSet()))
			{
				return false;
			}

			InTest.TestTrue(At(Scale, TEXT("A click on the caret completes")),
				Rig.Driver()->Sequence().MoveToPixel(Aimed.GetValue()).Press().Release().Perform());
			InTest.TestTrue(At(Scale, TEXT("The field is still being edited")), IsEditing(Field));
			InTest.TestFalse(At(Scale, TEXT("...with nothing selected")), Field->IsAnyTextSelected());
			InTest.TestTrue(At(Scale, TEXT("Typing X completes")), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
			InTest.TestEqual(*At(Scale, TEXT("The X went in where the click was")), Field->GetText(), Typed.WithX);
		}
		return true;
	}

	/**
	 * At half and at double: the field typed into and all of it selected with Ctrl+A, then a finger tapped on the caret before
	 * the aimed character. The selection is gone, the field is still being edited, and the X typed next goes in there.
	 */
	template<class TField>
	bool RunScaledTapCaret(FAutomationTestBase& InTest, const FVector2D& InSize, bool bInMultiLine)
	{
		const FTypedText Typed = TypedTextFor(bInMultiLine);
		for (const double Scale : Scales)
		{
			FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
			if (!ComeUp(InTest, Rig, Scale))
			{
				return false;
			}
			TField* Field = Rig.MakeControl<TField>(TEXT("Notes"), nullptr, InSize);
			if (!InTest.TestNotNull(*At(Scale, TEXT("A field came up")), Field))
			{
				return false;
			}
			Rig.PumpFrames(1);
			if (!TypeAndWait(InTest, Rig, TEXT("Notes"), Typed, Scale))
			{
				return false;
			}
			const TOptional<FVector2D> Aimed = AimedCaretPixel(Field, Typed, bInMultiLine);
			if (!InTest.TestTrue(At(Scale, TEXT("The caret aimed at is on the viewport")), Aimed.IsSet()))
			{
				return false;
			}
			InTest.TestTrue(At(Scale, TEXT("Ctrl+A completes")), Rig.Driver()->Sequence().Key(EKeys::A, EDreamDriverModifierKeys::Ctrl).Perform());
			if (!InTest.TestTrue(At(Scale, TEXT("...and selects everything")), Field->IsAnyTextSelected()))
			{
				return false;
			}

			InTest.TestTrue(At(Scale, TEXT("A finger tapped on the caret completes")),
				Rig.Driver()->Sequence().TouchDown(0, Aimed.GetValue()).TouchUp(0).Perform());
			InTest.TestTrue(At(Scale, TEXT("The field is still being edited")), IsEditing(Field));
			InTest.TestFalse(At(Scale, TEXT("The tap left nothing selected")), Field->IsAnyTextSelected());
			InTest.TestTrue(At(Scale, TEXT("Typing X completes")), Rig.Driver()->Sequence().Type(TEXT("X")).Perform());
			InTest.TestEqual(*At(Scale, TEXT("The X went in where the finger landed")), Field->GetText(), Typed.WithX);
		}
		return true;
	}

	/**
	 * At half and at double: a button above the field and another further below it. The D-pad down from the upper button
	 * reaches the field -- the nearer of the two below, by where they are drawn -- and the pad's confirm starts its edit, which
	 * takes what is typed next.
	 */
	template<class TField>
	bool RunScaledPadInto(FAutomationTestBase& InTest, const FVector2D& InSize)
	{
		for (const double Scale : Scales)
		{
			FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
			if (!ComeUp(InTest, Rig, Scale))
			{
				return false;
			}
			// All inside the root at double scale, which is 640 by 360 units.
			UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 110.0));
			TField* Field = Above != nullptr ? Rig.MakeControl<TField>(TEXT("Notes"), nullptr, InSize, FVector2D(0.0, 30.0 - InSize.Y * 0.5)) : nullptr;
			UDreamButton* Far = Field != nullptr ? Rig.MakeControl<UDreamButton>(TEXT("Far"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, -150.0)) : nullptr;
			UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
			if (!InTest.TestTrue(At(Scale, TEXT("Two buttons, the field between them and the world's input came up")),
				Far != nullptr && Above->FaceNode != nullptr && Services != nullptr))
			{
				return false;
			}
			Rig.PumpFrames(1);
			if (!InTest.TestTrue(At(Scale, TEXT("Player 0's focus can be put on the upper button")), Services->FocusForNavigation(Above->FaceNode, 0)))
			{
				return false;
			}

			InTest.TestTrue(At(Scale, TEXT("The D-pad down completes")), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform());
			if (!InTest.TestTrue(At(Scale, TEXT("The D-pad down reached the field, not the button further below")), IsPartOf(FocusOf(Rig), Field)))
			{
				return false;
			}
			InTest.TestTrue(At(Scale, TEXT("Pressing and releasing the pad's confirm completes")),
				Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
			InTest.TestTrue(At(Scale, TEXT("The confirm started the field's edit")), IsEditing(Field));
			InTest.TestTrue(At(Scale, TEXT("Typing completes")), Rig.Driver()->Sequence().Type(TEXT("ab")).Perform());
			InTest.TestEqual(*At(Scale, TEXT("The characters went into the field")), Field->GetText(), FString(TEXT("ab")));
		}
		return true;
	}
}

// ------------------------------------------------------------------------------------------------ the borderless field

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEditableTextDisabledClickTest,
	"DreamGUI.EditableText.ADisabledBorderlessFieldIsNotEditedByAClickAndTakesNoKeysUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEditableTextDisabledClickTest, "DreamGUI.EditableText.ADisabledBorderlessFieldIsNotEditedByAClickAndTakesNoKeysUntilItIsEnabled", "[Pointer][Text][Disabled]")

/* UWidget::SetIsEnabled(false) on UMG's EditableText: the click is cut off the field's path and the keys find no focus there. */
bool FDreamCoverageEditableTextDisabledClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledClickAndKeys<UDreamEditableText>(*this, SingleLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEditableTextDisabledTapTest,
	"DreamGUI.EditableText.AFingerTapOnADisabledBorderlessFieldStartsNoEditUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEditableTextDisabledTapTest, "DreamGUI.EditableText.AFingerTapOnADisabledBorderlessFieldStartsNoEditUntilItIsEnabled", "[Touch][Disabled]")

/* A touch Slate hands on as a mouse press, cut off the disabled field's path like the click. */
bool FDreamCoverageEditableTextDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledTap<UDreamEditableText>(*this, SingleLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEditableTextDisabledTabTest,
	"DreamGUI.EditableText.TabPassesOverADisabledBorderlessFieldAndStopsOnItOnceItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEditableTextDisabledTabTest, "DreamGUI.EditableText.TabPassesOverADisabledBorderlessFieldAndStopsOnItOnceItIsEnabled", "[Nav][Disabled]")

/* FHittestGrid::FindNextFocusableWidget passes over a widget that is not enabled, and stops on it again once it is. */
bool FDreamCoverageEditableTextDisabledTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledTab<UDreamEditableText>(*this, SingleLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEditableTextScaledClickTest,
	"DreamGUI.EditableText.OnACanvasScaledToHalfAndToDoubleAClickBetweenTwoCharactersPutsTheCaretBetweenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEditableTextScaledClickTest, "DreamGUI.EditableText.OnACanvasScaledToHalfAndToDoubleAClickBetweenTwoCharactersPutsTheCaretBetweenThem", "[Pointer][Text][Scaled]")

/* FSlateEditableTextLayout::HandleMouseButtonDown finds the boundary under the press in the text's own space, at any scale. */
bool FDreamCoverageEditableTextScaledClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunScaledClickCaret<UDreamEditableText>(*this, SingleLineSize, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEditableTextScaledTapTest,
	"DreamGUI.EditableText.OnACanvasScaledToHalfAndToDoubleAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEditableTextScaledTapTest, "DreamGUI.EditableText.OnACanvasScaledToHalfAndToDoubleAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem", "[Touch][Text][Scaled]")

/* The tap is the same press, landing on the same boundary at either scale and clearing the selection Ctrl+A made. */
bool FDreamCoverageEditableTextScaledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunScaledTapCaret<UDreamEditableText>(*this, SingleLineSize, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageEditableTextScaledPadTest,
	"DreamGUI.EditableText.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageEditableTextScaledPadTest, "DreamGUI.EditableText.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit", "[Nav][Scaled]")

/* The D-pad's next stop is the nearest focusable widget that way as drawn (FHittestGrid::FindNextFocusableWidget). */
bool FDreamCoverageEditableTextScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunScaledPadInto<UDreamEditableText>(*this, SingleLineSize);
}

// ------------------------------------------------------------------------------------------------ the multi-line field

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageMultiLineDisabledClickTest,
	"DreamGUI.MultiLineEditableText.ADisabledMultiLineFieldIsNotEditedByAClickAndTakesNoKeysUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageMultiLineDisabledClickTest, "DreamGUI.MultiLineEditableText.ADisabledMultiLineFieldIsNotEditedByAClickAndTakesNoKeysUntilItIsEnabled", "[Pointer][Text][Disabled]")

/* UMG's MultiLineEditableText disabled: no edit from the click, and no line from Enter. */
bool FDreamCoverageMultiLineDisabledClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledClickAndKeys<UDreamMultiLineEditableText>(*this, MultiLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageMultiLineDisabledTapTest,
	"DreamGUI.MultiLineEditableText.AFingerTapOnADisabledMultiLineFieldStartsNoEditUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageMultiLineDisabledTapTest, "DreamGUI.MultiLineEditableText.AFingerTapOnADisabledMultiLineFieldStartsNoEditUntilItIsEnabled", "[Touch][Disabled]")

bool FDreamCoverageMultiLineDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledTap<UDreamMultiLineEditableText>(*this, MultiLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageMultiLineDisabledTabTest,
	"DreamGUI.MultiLineEditableText.TabPassesOverADisabledMultiLineFieldAndStopsOnItOnceItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageMultiLineDisabledTabTest, "DreamGUI.MultiLineEditableText.TabPassesOverADisabledMultiLineFieldAndStopsOnItOnceItIsEnabled", "[Nav][Disabled]")

bool FDreamCoverageMultiLineDisabledTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledTab<UDreamMultiLineEditableText>(*this, MultiLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageMultiLineScaledClickTest,
	"DreamGUI.MultiLineEditableText.OnACanvasScaledToHalfAndToDoubleAClickOnTheSecondLinePutsTheCaretWhereItLanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageMultiLineScaledClickTest, "DreamGUI.MultiLineEditableText.OnACanvasScaledToHalfAndToDoubleAClickOnTheSecondLinePutsTheCaretWhereItLanded", "[Pointer][Text][Scaled]")

/* Two lines, the click aimed at the second line's caret before "line": the line as well as the column comes out right. */
bool FDreamCoverageMultiLineScaledClickTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunScaledClickCaret<UDreamMultiLineEditableText>(*this, MultiLineSize, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageMultiLineScaledTapTest,
	"DreamGUI.MultiLineEditableText.OnACanvasScaledToHalfAndToDoubleAFingerTapOnTheSecondLinePutsTheCaretWhereItLanded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageMultiLineScaledTapTest, "DreamGUI.MultiLineEditableText.OnACanvasScaledToHalfAndToDoubleAFingerTapOnTheSecondLinePutsTheCaretWhereItLanded", "[Touch][Text][Scaled]")

bool FDreamCoverageMultiLineScaledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunScaledTapCaret<UDreamMultiLineEditableText>(*this, MultiLineSize, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageMultiLineScaledPadTest,
	"DreamGUI.MultiLineEditableText.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageMultiLineScaledPadTest, "DreamGUI.MultiLineEditableText.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit", "[Nav][Scaled]")

bool FDreamCoverageMultiLineScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	// Shorter than the other multi-line tests' field, so the button above and the one below fit the root at double scale.
	return RunScaledPadInto<UDreamMultiLineEditableText>(*this, FVector2D(360.0, 100.0));
}

// ------------------------------------------------------------------------------------------------ the boxed field

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTextInputDisabledTapTest,
	"DreamGUI.TextInput.AFingerTapOnADisabledFieldStartsNoEditUntilItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTextInputDisabledTapTest, "DreamGUI.TextInput.AFingerTapOnADisabledFieldStartsNoEditUntilItIsEnabled", "[Touch][Disabled]")

/* UMG's EditableTextBox disabled, reached by a finger rather than the mouse (the mouse is DreamGUI.TextInput.ADisabledField*). */
bool FDreamCoverageTextInputDisabledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledTap<UDreamTextInput>(*this, SingleLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTextInputDisabledTabTest,
	"DreamGUI.TextInput.TabPassesOverADisabledFieldAndStopsOnItOnceItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTextInputDisabledTabTest, "DreamGUI.TextInput.TabPassesOverADisabledFieldAndStopsOnItOnceItIsEnabled", "[Nav][Disabled]")

bool FDreamCoverageTextInputDisabledTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunDisabledTab<UDreamTextInput>(*this, SingleLineSize);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTextInputScaledTapTest,
	"DreamGUI.TextInput.OnACanvasScaledToHalfAndToDoubleAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTextInputScaledTapTest, "DreamGUI.TextInput.OnACanvasScaledToHalfAndToDoubleAFingerTapBetweenTwoCharactersPutsTheCaretBetweenThem", "[Touch][Text][Scaled]")

bool FDreamCoverageTextInputScaledTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunScaledTapCaret<UDreamTextInput>(*this, SingleLineSize, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageTextInputScaledPadTest,
	"DreamGUI.TextInput.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageTextInputScaledPadTest, "DreamGUI.TextInput.OnACanvasScaledToHalfAndToDoubleTheDPadReachesTheFieldBelowAButtonAndConfirmStartsItsEdit", "[Nav][Scaled]")

bool FDreamCoverageTextInputScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextFieldsCoverageTestLocal;
	return RunScaledPadInto<UDreamTextInput>(*this, SingleLineSize);
}

#endif
