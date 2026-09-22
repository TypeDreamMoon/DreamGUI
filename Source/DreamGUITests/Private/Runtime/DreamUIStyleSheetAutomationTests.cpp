// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamToggle.h"
#include "Controls/DreamUIStyleSheet.h"
#include "Core/DreamGUISettings.h"
#include "Interaction/UIToggle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The style indirection: where a control's look comes from.
 *
 * The resolution order is the entire contract, so that is what gets pinned -- through a real
 * control (the toggle) rather than by calling the sheet's accessors, because "the sheet returns the
 * right struct" and "the control's parts end up wearing it" are different claims and only the
 * second one is the feature. The observable is UUIToggle's colours, which ApplyStyle pushes and
 * which have public getters.
 *
 *   1. No sheet configured        -> the instance's inline Style (whose defaults are the theme).
 *   2. Sheet configured           -> the sheet's family default, and the inline Style is IGNORED --
 *                                    deliberately whole-for-whole, not merged.
 *   3. StyleVariant set           -> the named entry; a name that matches nothing falls back to the
 *                                    family default rather than failing.
 *   4. StyleSource = Inline       -> the instance's Style wins even with a sheet present.
 *
 * The tests mutate the settings CDO (the sheet is configured there), so every path restores it --
 * a leaked setting would silently re-theme every later test that builds a control.
 */
namespace DreamUIStyleSheetTestLocal
{
	/** Scoped settings mutation: point the project at InSheet, put it back on destruction. */
	struct FScopedProjectSheet
	{
		TSoftObjectPtr<UDreamUIStyleSheet> Saved;

		explicit FScopedProjectSheet(UDreamUIStyleSheet* InSheet)
		{
			UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
			Saved = Settings->DefaultStyleSheet;
			Settings->DefaultStyleSheet = InSheet;
		}

		~FScopedProjectSheet()
		{
			GetMutableDefault<UDreamGUISettings>()->DefaultStyleSheet = Saved;
		}
	};

	UDreamToggle* MakeToggle()
	{
		UDreamToggle* Toggle = NewObject<UDreamToggle>(GetTransientPackage());
		Toggle->Initialize();
		return Toggle;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStyleSheetDefaultTest,
	"DreamGUI.Controls.StyleSheet.TheSheetDecidesAndAVariantRefines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStyleSheetDefaultTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIStyleSheetTestLocal;

	UDreamUIStyleSheet* Sheet = NewObject<UDreamUIStyleSheet>(GetTransientPackage());
	Sheet->Toggle.TickChecked = FColor(1, 2, 3, 255);
	FDreamToggleStyle Danger = Sheet->Toggle;
	Danger.TickChecked = FColor(200, 30, 30, 255);
	Sheet->ToggleVariants.Add(TEXT("Danger"), Danger);

	FScopedProjectSheet Scope(Sheet);

	// The family default, and proof the inline Style lost: the instance authors a colour of its own
	// and the sheet's arrives anyway. Whole-for-whole is the contract, not a merge.
	{
		TStrongObjectPtr<UDreamToggle> Toggle(NewObject<UDreamToggle>(GetTransientPackage()));
		Toggle->Style.TickChecked = FColor(9, 9, 9, 255);
		Toggle->Initialize();
		if (TestNotNull(TEXT("behaviour exists"), Toggle->ToggleBehaviour.Get()))
		{
			TestEqual(TEXT("the sheet's colour arrived at the parts"),
				Toggle->ToggleBehaviour->GetOnColor(), FColor(1, 2, 3, 255));
		}
	}

	// The named variant.
	{
		TStrongObjectPtr<UDreamToggle> Toggle(NewObject<UDreamToggle>(GetTransientPackage()));
		Toggle->StyleVariant = TEXT("Danger");
		Toggle->Initialize();
		TestEqual(TEXT("the variant's colour arrived"),
			Toggle->ToggleBehaviour->GetOnColor(), FColor(200, 30, 30, 255));
	}

	// A misspelled variant produces the project default -- visibly something -- not a failure.
	{
		TStrongObjectPtr<UDreamToggle> Toggle(NewObject<UDreamToggle>(GetTransientPackage()));
		Toggle->StyleVariant = TEXT("Dagner");
		Toggle->Initialize();
		TestEqual(TEXT("an unknown name falls back to the family default"),
			Toggle->ToggleBehaviour->GetOnColor(), FColor(1, 2, 3, 255));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStyleSheetOptOutTest,
	"DreamGUI.Controls.StyleSheet.InlineOptsOutAndNoSheetFallsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStyleSheetOptOutTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIStyleSheetTestLocal;

	// Inline wins over a configured sheet: the one special toggle on the one special screen.
	{
		UDreamUIStyleSheet* Sheet = NewObject<UDreamUIStyleSheet>(GetTransientPackage());
		Sheet->Toggle.TickChecked = FColor(1, 2, 3, 255);
		FScopedProjectSheet Scope(Sheet);

		TStrongObjectPtr<UDreamToggle> Toggle(NewObject<UDreamToggle>(GetTransientPackage()));
		Toggle->StyleSource = EDreamUIStyleSource::Inline;
		Toggle->Style.TickChecked = FColor(9, 9, 9, 255);
		Toggle->Initialize();
		TestEqual(TEXT("the instance's own colour arrived"),
			Toggle->ToggleBehaviour->GetOnColor(), FColor(9, 9, 9, 255));
	}

	// No sheet at all: the inline defaults ARE the theme, and a project that never makes a sheet
	// is a supported project, not a broken one.
	{
		FScopedProjectSheet Scope(nullptr);

		TStrongObjectPtr<UDreamToggle> Toggle(MakeToggle());
		if (TestNotNull(TEXT("behaviour exists"), Toggle->ToggleBehaviour.Get()))
		{
			TestEqual(TEXT("the struct's own default arrived"),
				Toggle->ToggleBehaviour->GetOnColor(), FDreamToggleStyle().TickChecked);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
