// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIWidgetRegistry.h"
#include "Core/Components/DreamCustomMesh.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/UObjectIterator.h"

/*
 * Which visuals the language can spell, asked of the classes rather than of a list.
 *
 * The `.dui` tag table used to be ten lines inside the builder -- nine visuals and `Widget` --
 * while the plugin ships nineteen concrete visuals. The ten that were missing were not chosen;
 * they were the ones nobody remembered to type, and every one of them was already in the palette.
 * Nothing anywhere held the two in agreement, and nothing anywhere reported the disagreement.
 *
 * The sweep below is the thing that would have. It fails on a visual with no tag, so the decision
 * to leave one unspellable has to be made rather than defaulted into -- by putting the class in
 * the list of exceptions right here, with the reason.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVisualTagCoverageTest,
	"DreamGUI.Text.EveryVisualIsSpellableOrIsDeliberatelyNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVisualTagCoverageTest::RunTest(const FString& Parameters)
{
	// The exceptions, each of which is a decision rather than an oversight.
	//
	// One entry, and the shortness is the finding: every other visual that has no tag turned out to
	// be Abstract already -- UDreamSpriteBase, UDreamTextureBase, UDream2DLineRendererBase all carry
	// it as a later specifier (`UCLASS(ClassGroup = (DreamGUI), Abstract, ...)`), which is easy to
	// miss when grepping for `UCLASS(Abstract`. They never reach this sweep, so listing them here
	// would be three assertions that can never run.
	const TSet<UClass*> Untagged =
	{
		// Draws whatever a UDreamUICustomMeshSource hands it, and the language cannot hand it one, so
		// the tag would only ever produce a widget that draws nothing and no message saying why.
		// UDreamUMGWidget derives from this and IS tagged: what it hosts is a property.
		UDreamCustomMesh::StaticClass(),
	};

	int32 Tagged = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UDreamVisual::StaticClass()) || Class == UDreamVisual::StaticClass())
		{
			continue;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists
			| CLASS_CompiledFromBlueprint))
		{
			continue;
		}
		if (Untagged.Contains(Class))
		{
			// Held to the decision in both directions: a class listed above that acquires a tag
			// leaves this list stale, and a stale list is how the sweep stops meaning anything.
			TestTrue(*FString::Printf(TEXT("%s is listed as untagged and has no tag"), *Class->GetName()),
				FDreamUIWidgetRegistry::FindTagForClass(Class).IsEmpty());
			continue;
		}

		const FString Tag = FDreamUIWidgetRegistry::FindTagForClass(Class);
		if (!TestFalse(*FString::Printf(
			TEXT("%s has a .dui tag (declare one with DECLARE_DREAM_GUI_VISUAL, or list it above with the reason)"),
			*Class->GetName()), Tag.IsEmpty()))
		{
			continue;
		}
		// Round trip, because the two directions are separate lookups over the same entries and a
		// tag that resolves to a different class is worse than one that resolves to nothing.
		bool bIsKnown = false;
		TestEqual(*FString::Printf(TEXT("'%s' resolves back to %s"), *Tag, *Class->GetName()),
			FDreamUIWidgetRegistry::ResolveVisual(FName(*Tag), bIsKnown), Class);
		TestTrue(*FString::Printf(TEXT("'%s' is a known tag"), *Tag), bIsKnown);
		++Tagged;
	}

	// The nine the old table had, plus the ten it did not.
	TestTrue(TEXT("the sweep found the whole library"), Tagged >= 19);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamVisualTagEnumerationTest,
	"DreamGUI.Text.TheTagListIsSortedAndLeadsWithWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamVisualTagEnumerationTest::RunTest(const FString& Parameters)
{
	TArray<TPair<FName, UClass*>> Entries;
	FDreamUIWidgetRegistry::GetVisualEntries(Entries);
	if (!TestTrue(TEXT("there are tags"), Entries.Num() > 1))
	{
		return false;
	}

	// `Widget` is a known tag whose answer is NO VISUAL, which is a different fact from an unknown
	// tag, and it is the only entry that cannot be declared beside a class.
	TestEqual(TEXT("Widget leads"), Entries[0].Key, FName(TEXT("Widget")));
	TestNull(TEXT("and names no visual"), Entries[0].Value);

	// Sorted, and this is the assertion that matters. Registration happens during static
	// initialisation, so the natural order is link order -- and the diagnostic that suggests a tag
	// for a mistyped property takes the FIRST match. Unsorted, it would suggest `Sprite` on one
	// build and `Texture` on the next, from the same source.
	for (int32 Index = 2; Index < Entries.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("'%s' sorts before '%s'"),
			*Entries[Index - 1].Key.ToString(), *Entries[Index].Key.ToString()),
			Entries[Index - 1].Key.LexicalLess(Entries[Index].Key));
	}

	// Every entry after the first names a visual: a second null would be a second `Widget`, and the
	// resolver answers "known tag, no visual" for it -- a node that silently draws nothing.
	for (int32 Index = 1; Index < Entries.Num(); ++Index)
	{
		TestNotNull(*FString::Printf(TEXT("'%s' names a class"), *Entries[Index].Key.ToString()),
			Entries[Index].Value);
	}

	// The builder's own view of the same data, since that is what the compiler actually calls.
	TArray<TPair<FString, UClass*>> BuilderTags;
	FDreamUITextBuilder::GetVisualTags(BuilderTags);
	TestEqual(TEXT("the builder sees every tag"), BuilderTags.Num(), Entries.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTagForClassTest,
	"DreamGUI.Text.AClassCanBeSpelledBackAsTheTagThatMakesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTagForClassTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("a visual spells as its bare tag"),
		FDreamUIWidgetRegistry::FindTagForClass(UDreamImage::StaticClass()), FString(TEXT("Image")));

	// A scoped tag keeps its scope, because that is how a .dui spells it. The two kinds share one
	// registry and this is the only place the difference shows.
	UClass* Toggle = FDreamUIWidgetRegistry::Resolve(TEXT("Native"), TEXT("Toggle"));
	if (TestNotNull(TEXT("Native.Toggle is registered"), Toggle))
	{
		TestEqual(TEXT("and spells back with its scope"),
			FDreamUIWidgetRegistry::FindTagForClass(Toggle), FString(TEXT("Native.Toggle")));
	}

	// EXACT, not IsChildOf. UDreamPolygon derives from UDreamImage and has its own tag; a lookup
	// that walked up the chain would answer "Image" for anything that inherits one, and a write-back
	// using that answer would emit a node that rebuilds as the wrong class -- lossy, and it compiles.
	TestEqual(TEXT("a widget class has no visual tag"),
		FDreamUIWidgetRegistry::FindTagForClass(UDreamWidget::StaticClass()), FString());
	TestEqual(TEXT("and nothing spells as nothing"),
		FDreamUIWidgetRegistry::FindTagForClass(nullptr), FString());
	return true;
}

#endif
