// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamControlStyles.h"
#include "Controls/DreamUIStyleSheet.h"

/*
 * A style that states only what differs.
 *
 * Changing one colour on one variant used to mean copying the whole struct, which is the fork a
 * project sheet exists to prevent -- and the copy stops tracking the theme the moment the theme
 * moves. Every style field now carries a bOverride_<field> bit beside it (UE's own idiom, the one
 * FPostProcessSettings uses, rendered as a checkbox that greys what is not in effect), and one
 * reflection pass writes the ticked ones over a base.
 *
 * The bits default to TRUE, which is the load-bearing decision here: a variant authored before any
 * of this existed has every bit set, so it is the full fork it has always been and nothing that
 * already works changes. UNTICKING is the new verb, and it means "whatever the base says".
 *
 * Two bases, one mechanism:
 *
 *   - a sheet VARIANT is written over the family default, which is inheritance with the family
 *     default as the only possible parent -- no Parent pointer to author, no cycle to check for
 *   - an INSTANCE is written over the resolved sheet style, in the third StyleSource mode
 *
 * These tests are on the merge and on the sheet, not on a control: what a control does with the
 * result is already covered, and what is new is which value it gets handed.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStyleOverrideMergeTest,
	"DreamGUI.Style.OnlyTickedFieldsAreWrittenOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStyleOverrideMergeTest::RunTest(const FString& Parameters)
{
	FDreamButtonStyle Base;
	Base.Height = 38.0f;
	Base.Normal = FColor(52, 57, 70, 255);
	Base.FontSize = 15.0f;

	FDreamButtonStyle Overrides;
	Overrides.Height = 999.0f;
	Overrides.Normal = FColor::Red;
	Overrides.FontSize = 99.0f;
	// One field ticked, the rest not. The values are all set, deliberately: the claim is that an
	// unticked field is ignored no matter what it holds, which is the only reading under which
	// "leave it alone" is safe to author.
	Overrides.bOverride_Height = false;
	Overrides.bOverride_Normal = true;
	Overrides.bOverride_FontSize = false;

	const FDreamButtonStyle Merged = DreamUI_MergeStyle(Base, Overrides);

	TestEqual(TEXT("the ticked colour came from the override"), Merged.Normal, FColor::Red);
	TestEqual(TEXT("an unticked number stayed with the base"), Merged.Height, 38.0f);
	TestEqual(TEXT("and so did the other one"), Merged.FontSize, 15.0f);

	// Nothing was written back. The merge produces a value; both sides are inputs, and an author's
	// asset is not one of them.
	TestEqual(TEXT("the base is untouched"), Base.Height, 38.0f);
	TestEqual(TEXT("the override struct is untouched"), Overrides.Height, 999.0f);

	// The default state, which is the one every existing asset is in.
	FDreamButtonStyle Fresh;
	Fresh.Normal = FColor::Green;
	TestTrue(TEXT("a fresh style states its fields"), Fresh.bOverride_Normal);
	TestEqual(TEXT("so merging it is the same as replacing"),
		DreamUI_MergeStyle(Base, Fresh).Normal, FColor::Green);

	// A nested struct field rides one bit, not one per member: a brush is a thing an author swaps
	// whole, and half a brush is not a look anyone asked for.
	FDreamButtonStyle BrushOverride;
	BrushOverride.bOverride_Height = false;
	BrushOverride.bOverride_Normal = false;
	BrushOverride.bOverride_FaceBrush = true;
	BrushOverride.FaceBrush.Tint = FColor::Blue;
	BrushOverride.FaceBrush.ImageSize = FVector2D(64.0, 64.0);
	const FDreamButtonStyle WithBrush = DreamUI_MergeStyle(Base, BrushOverride);
	TestEqual(TEXT("the whole brush came across"), WithBrush.FaceBrush.Tint, FColor::Blue);
	TestEqual(TEXT("including its size"), WithBrush.FaceBrush.ImageSize, FVector2D(64.0, 64.0));
	TestEqual(TEXT("and the untouched colour did not"), WithBrush.Normal, Base.Normal);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStyleVariantInheritanceTest,
	"DreamGUI.Style.AVariantInheritsTheFamilyDefaultForWhatItDoesNotState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStyleVariantInheritanceTest::RunTest(const FString& Parameters)
{
	UDreamUIStyleSheet* Sheet = NewObject<UDreamUIStyleSheet>(GetTransientPackage());

	Sheet->Button.Height = 44.0f;
	Sheet->Button.FontSize = 17.0f;
	Sheet->Button.Normal = FColor(10, 20, 30, 255);

	// "Danger": red, and otherwise the project's button. Two lines of intent, where before this it
	// was a copy of every field and a promise to keep the copy in step.
	FDreamButtonStyle& Danger = Sheet->ButtonVariants.Add(TEXT("Danger"));
	Danger.Normal = FColor::Red;
	Danger.bOverride_Height = false;
	Danger.bOverride_FontSize = false;

	const FDreamButtonStyle Resolved = Sheet->ButtonStyle(TEXT("Danger"));
	TestEqual(TEXT("what the variant states, it decides"), Resolved.Normal, FColor::Red);
	TestEqual(TEXT("what it does not, the family default decides"), Resolved.Height, 44.0f);
	TestEqual(TEXT("and keeps deciding"), Resolved.FontSize, 17.0f);

	// Moving the theme moves the variant with it, which is the entire reason not to fork.
	Sheet->Button.Height = 52.0f;
	TestEqual(TEXT("the variant follows the theme it did not fork"),
		Sheet->ButtonStyle(TEXT("Danger")).Height, 52.0f);

	// The fallbacks the sheet already promised, unchanged: a name nobody answers to is the family
	// default rather than a null every caller has to test for.
	TestEqual(TEXT("an unknown variant is the family default"),
		Sheet->ButtonStyle(TEXT("NoSuchVariant")).Height, 52.0f);
	TestEqual(TEXT("and so is none"), Sheet->ButtonStyle(NAME_None).Height, 52.0f);

	// A variant with every bit ticked -- which is what every variant authored before this carries --
	// is still the full fork it was.
	FDreamButtonStyle& Whole = Sheet->ButtonVariants.Add(TEXT("Whole"));
	Whole.Height = 12.0f;
	TestEqual(TEXT("an untouched variant still states everything"),
		Sheet->ButtonStyle(TEXT("Whole")).Height, 12.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
