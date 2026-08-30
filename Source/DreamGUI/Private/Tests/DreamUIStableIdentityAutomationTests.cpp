// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/StrongObjectPtr.h"

/*
 * A .dui widget's identity -- object FName and WidgetGuid -- derives from its node id. The property
 * under test is DETERMINISM: build the same file twice and every widget comes out with the same
 * name and the same guid, because designer state is keyed by the name and preview pairing by the
 * guid, and identities that change on every compile reset both while churning the .uasset.
 *
 * The counter-property matters too: the guid is salted with the class path, so the same id in two
 * different assets must NOT collide -- per-asset-colliding ids are the exact bug guids were
 * introduced to fix.
 */

namespace DreamUIStableIdentityTestLocal
{
	FString MakeSource(const FString& InClassPath)
	{
		return FString::Join(TArray<FString>{
			FString::Printf(TEXT("class %s"), *InClassPath),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("    }"),
			TEXT("    Image Icon {"),
			TEXT("    }"),
			TEXT("    Widget Row {"),
			TEXT("        Text Line {"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}")}, TEXT("\n"));
	}

	UDreamWidgetTree* BuildFrom(const FString& InSource, FAutomationTestBase& InTest)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		if (!InTest.TestTrue(TEXT("Fixture parses"), FDreamUISourceFile::Parse(InSource, TEXT("Identity.dui"), Ast, Diagnostics)))
		{
			return nullptr;
		}
		TArray<FDreamWidgetPropertyBinding> Bindings;
		UDreamWidgetTree* Tree = FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings);
		InTest.TestTrue(TEXT("Fixture builds"), Tree != nullptr);
		return Tree;
	}

	void CollectIdentities(UDreamWidgetTree* InTree, TMap<FString, TPair<FName, FGuid>>& OutByDisplayName)
	{
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(InTree->RootWidget, Widgets, /*IncludeTarget*/true);
		for (UDreamWidget* Widget : Widgets)
		{
			OutByDisplayName.Add(Widget->GetDisplayName(), TPair<FName, FGuid>(Widget->GetFName(), Widget->GetWidgetGuid()));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStableIdentityDeterminismTest,
	"DreamGUI.Text.Identity.SameFileBirthsTheSameIdentitiesTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStableIdentityDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIStableIdentityTestLocal;

	const FString Source = MakeSource(TEXT("/Game/UI/WBP_IdentityFixture"));
	TStrongObjectPtr<UDreamWidgetTree> First(BuildFrom(Source, *this));
	TStrongObjectPtr<UDreamWidgetTree> Second(BuildFrom(Source, *this));
	if (!First.IsValid() || !Second.IsValid())
	{
		return false;
	}

	TMap<FString, TPair<FName, FGuid>> FirstIdentities;
	TMap<FString, TPair<FName, FGuid>> SecondIdentities;
	CollectIdentities(First.Get(), FirstIdentities);
	CollectIdentities(Second.Get(), SecondIdentities);

	TestEqual(TEXT("Both builds produce the same widget count"), FirstIdentities.Num(), SecondIdentities.Num());
	TestEqual(TEXT("The fixture has its five widgets"), FirstIdentities.Num(), 5);

	for (const TPair<FString, TPair<FName, FGuid>>& Entry : FirstIdentities)
	{
		const TPair<FName, FGuid>* Other = SecondIdentities.Find(Entry.Key);
		if (!TestTrue(FString::Printf(TEXT("'%s' exists in both builds"), *Entry.Key), Other != nullptr))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("'%s' has a valid guid"), *Entry.Key), Entry.Value.Value.IsValid());
		TestEqual(FString::Printf(TEXT("'%s' keeps its object name across builds"), *Entry.Key),
			Entry.Value.Key, Other->Key);
		TestEqual(FString::Printf(TEXT("'%s' keeps its guid across builds"), *Entry.Key),
			Entry.Value.Value, Other->Value);
		// The name IS the id: this is what keys the designer's hidden/locked/collapsed sets.
		TestEqual(FString::Printf(TEXT("'%s' is named by its id"), *Entry.Key),
			Entry.Value.Key.ToString(), Entry.Key);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStableIdentityCrossAssetTest,
	"DreamGUI.Text.Identity.SameIdInTwoAssetsDoesNotCollide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStableIdentityCrossAssetTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIStableIdentityTestLocal;

	TStrongObjectPtr<UDreamWidgetTree> AssetA(BuildFrom(MakeSource(TEXT("/Game/UI/WBP_AssetA")), *this));
	TStrongObjectPtr<UDreamWidgetTree> AssetB(BuildFrom(MakeSource(TEXT("/Game/UI/WBP_AssetB")), *this));
	if (!AssetA.IsValid() || !AssetB.IsValid())
	{
		return false;
	}

	TMap<FString, TPair<FName, FGuid>> IdentitiesA;
	TMap<FString, TPair<FName, FGuid>> IdentitiesB;
	CollectIdentities(AssetA.Get(), IdentitiesA);
	CollectIdentities(AssetB.Get(), IdentitiesB);

	for (const TPair<FString, TPair<FName, FGuid>>& Entry : IdentitiesA)
	{
		const TPair<FName, FGuid>* Other = IdentitiesB.Find(Entry.Key);
		if (Other == nullptr)
		{
			continue;
		}
		// Two instances of two assets sharing an id inside one preview world was the original
		// cross-asset pairing bug; a guid derived from the bare id would faithfully recreate it.
		TestNotEqual(FString::Printf(TEXT("'%s' has a different guid in a different asset"), *Entry.Key),
			Entry.Value.Value, Other->Value);
	}
	return true;
}

#endif
