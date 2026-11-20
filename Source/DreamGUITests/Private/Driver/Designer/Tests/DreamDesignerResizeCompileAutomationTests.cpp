// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "DreamWidgetBlueprint.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"

/*
 * A control resized in the designer keeps its size through a compile.
 *
 * Reported against a Button holding a label and a TextInput: dragged taller, they went back to the height their text
 * asks for as soon as the Blueprint compiled (or anything else rebuilt the preview), with the canvas slot's Auto Size off.
 */
namespace DreamDesignerResizeCompileTestLocal
{
	using namespace DreamTests;

	bool DragTallerThenCompile(FAutomationTestBase& InTest, const TCHAR* InName, UClass* InControlClass)
	{
		FScopedDesignerPreferences Preferences;
		Preferences.SetGridSnap(false);
		FScopedDesignerSession Session(InName, /*bGiveRootAPanel*/ true);
		if (!Session.IsReady())
		{
			InTest.AddError(FString::Printf(TEXT("No designer to drive: %s."), *Session.GetFailure()));
			return false;
		}
		FDreamDesignerDriver& Driver = Session.GetDriver();
		FBox2D WorkArea(ForceInit);
		if (!PrepareDesignerOneToOne(InTest, Driver, WorkArea))
		{
			return false;
		}
		UDreamWidget* Template = DropOntoRootAndFindTemplate(Driver, InControlClass, FIntPoint(
			FMath::RoundToInt32(WorkArea.Min.X + (WorkArea.Max.X - WorkArea.Min.X) * 0.4),
			FMath::RoundToInt32(WorkArea.Min.Y + (WorkArea.Max.Y - WorkArea.Min.Y) * 0.4)));
		if (!InTest.TestNotNull(TEXT("The control was dropped on the canvas"), Template))
		{
			return false;
		}
		SelectOnlyInDesigner(Driver, Template);
		Driver.PumpFrame();
		const TOptional<FBox2D> Before = Driver.WidgetPixelRect(Driver.PreviewFor(Template));
		if (!InTest.TestTrue(TEXT("The control is on screen"), Before.IsSet()))
		{
			return false;
		}
		// What the designer shows: a control nobody sized is its style's height, whatever the class default the template
		// still carries.
		const double HeightBefore = Driver.PreviewFor(Template)->GetHeight();
		InTest.TestTrue(FString::Printf(TEXT("%s: dropped, it shows its style's height (%.1f), not the class default (%.1f)"),
			InName, HeightBefore, Template->GetHeight()), HeightBefore < Template->GetHeight());
		const FIntPoint Handle(FMath::RoundToInt32(Before->GetCenter().X), FMath::RoundToInt32(Before->Max.Y));
		InTest.TestTrue(TEXT("The drag of the bottom handle completes"), Driver.DragFromTo(Handle, Handle + FIntPoint(0, 40), /*Steps*/ 4));
		Driver.PumpFrame();
		const double HeightDragged = Template->GetHeight();
		const UDreamPanelSlot* Slot = Template->GetPanelSlot();
		InTest.AddInfo(FString::Printf(TEXT("%s: height %.1f, dragged to %.1f; slot auto size %d, authored fallback %s"),
			InName, HeightBefore, HeightDragged, Slot != nullptr && Slot->bAutoSize ? 1 : 0,
			Slot != nullptr ? *Slot->GetAuthoredDesiredSizeFallback().ToString() : TEXT("-")));
		if (!InTest.TestTrue(TEXT("The drag made it taller"), HeightDragged > HeightBefore + 10.0))
		{
			return false;
		}

		FCompilerResultsLog Results;
		FKismetEditorUtilities::CompileBlueprint(Session.GetBlueprint(), EBlueprintCompileOptions::SkipGarbageCollection, &Results);
		Driver.PumpFrame();
		Driver.PumpFrame();
		const UDreamWidget* Preview = Driver.PreviewFor(Template);
		InTest.AddInfo(FString::Printf(TEXT("%s after compile: template %.1f, preview %.1f"),
			InName, Template->GetHeight(), Preview != nullptr ? Preview->GetHeight() : -1.0f));
		InTest.TestTrue(FString::Printf(TEXT("%s: the template keeps the dragged height through the compile: %.1f, dragged %.1f"),
			InName, Template->GetHeight(), HeightDragged), FMath::IsNearlyEqual(Template->GetHeight(), HeightDragged, 0.5));
		InTest.TestTrue(FString::Printf(TEXT("%s: and so does the rebuilt preview: %.1f"), InName, Preview != nullptr ? Preview->GetHeight() : -1.0f),
			Preview != nullptr && FMath::IsNearlyEqual(Preview->GetHeight(), HeightDragged, 0.5));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerResizeButtonCompileTest,
	"DreamGUI.Designer.Driver.AButtonDraggedTallerKeepsItsHeightThroughACompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerResizeButtonCompileTest::RunTest(const FString&)
{
	return DreamDesignerResizeCompileTestLocal::DragTallerThenCompile(*this, TEXT("DesignerResizeButtonCompile"), UDreamButton::StaticClass());
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerResizeTextInputCompileTest,
	"DreamGUI.Designer.Driver.ATextInputDraggedTallerKeepsItsHeightThroughACompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDesignerResizeTextInputCompileTest::RunTest(const FString&)
{
	return DreamDesignerResizeCompileTestLocal::DragTallerThenCompile(*this, TEXT("DesignerResizeTextInputCompile"), UDreamTextInput::StaticClass());
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
