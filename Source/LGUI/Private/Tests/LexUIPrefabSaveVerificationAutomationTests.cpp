// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabSaveVerification.h"
#include "PrefabSystem/WidgetSerializer.h"

namespace LexUIPrefabSaveVerificationTestLocal
{
	struct FSavedPrefabFixture
	{
		UWorld* World = nullptr;
		ULexUIPrefab* Prefab = nullptr;
		ULexWidget* Root = nullptr;
		ULexWidget* Child = nullptr;

		bool BuildAndSave()
		{
			World = UWorld::CreateWorld(EWorldType::None, false);
			if (!World)
			{
				return false;
			}
			Prefab = NewObject<ULexUIPrefab>();
			Root = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
			Child = NewObject<ULexWidget>(Root, NAME_None, RF_Public | RF_Transactional);
			Root->SetDisplayName(TEXT("VerifyRoot"));
			Child->SetDisplayName(TEXT("VerifyChild"));
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			Child->SetWidth(120.0f);
			Child->SetHeight(80.0f);
			if (!Child->TrySetParent(Root, false))
			{
				return false;
			}
			ULexLayoutContainerHorizontalBox* Horizontal = Root->CreateNewLayoutContainer<ULexLayoutContainerHorizontalBox>();
			ULexPanelSlot* Slot = Child->GetPanelSlot();
			if (!Horizontal || !Slot)
			{
				return false;
			}
			Horizontal->SetSpacing(9.0f);
			Slot->SetSizeRule(ELexPanelSizeRule::Fill);
			Slot->SetFillWeight(2.0f);
			Slot->SetPadding(FMargin(1.0f, 2.0f, 3.0f, 4.0f));

			TMap<UObject*, FGuid> ObjectToGuid;
			ObjectToGuid.Add(Root, FGuid::NewGuid());
			TMap<TObjectPtr<ULexWidget>, FLexUISubPrefabData> EmptySubPrefabs;
			return LexUIPrefabSystem::WidgetSerializer::SavePrefab(Root, Prefab, ObjectToGuid, EmptySubPrefabs, true);
		}

		~FSavedPrefabFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPrefabSaveVerificationHealthyRoundTripTest,
	"LGUI.Prefab.SaveVerification.HealthyRoundTripVerifies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPrefabSaveVerificationHealthyRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSaveVerificationTestLocal;
	FSavedPrefabFixture Fixture;
	TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave());

	const LexUIPrefabSystem::FLexUIPrefabSaveVerificationResult Result =
		LexUIPrefabSystem::VerifyPrefabSaveRoundTrip(Fixture.Prefab, Fixture.Root);
	TestTrue(TEXT("A healthy save round-trips structurally"), Result.bStructureMatches);
	for (const FString& Difference : Result.StructuralDifferences)
	{
		AddError(FString::Printf(TEXT("Unexpected structural difference: %s"), *Difference));
	}
	// Canary: a fresh, untouched hierarchy must verify with zero property drift, otherwise every real
	// save would drown in false positives and the report becomes noise nobody reads.
	for (const FString& Difference : Result.PropertyDifferences)
	{
		AddError(FString::Printf(TEXT("Unexpected property drift on a healthy round trip: %s"), *Difference));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPrefabSaveVerificationDetectsStalePayloadTest,
	"LGUI.Prefab.SaveVerification.DetectsStalePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPrefabSaveVerificationDetectsStalePayloadTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSaveVerificationTestLocal;
	FSavedPrefabFixture Fixture;
	TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave());

	// Grow the live hierarchy after the save: the payload no longer represents it, exactly the state a
	// serializer bug (dropped nested object) would produce in the other direction.
	ULexWidget* LateChild = NewObject<ULexWidget>(Fixture.Root, NAME_None, RF_Public | RF_Transactional);
	LateChild->SetDisplayName(TEXT("LostByTheSave"));
	TestTrue(TEXT("Late child joins the root"), LateChild->TrySetParent(Fixture.Root, false));

	const LexUIPrefabSystem::FLexUIPrefabSaveVerificationResult Result =
		LexUIPrefabSystem::VerifyPrefabSaveRoundTrip(Fixture.Prefab, Fixture.Root);
	TestFalse(TEXT("A payload that lost a widget fails structural verification"), Result.bStructureMatches);
	bool bMentionsLostWidget = false;
	for (const FString& Difference : Result.StructuralDifferences)
	{
		bMentionsLostWidget |= Difference.Contains(TEXT("LostByTheSave"));
	}
	TestTrue(TEXT("The report names the lost widget"), bMentionsLostWidget);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexPrefabSaveVerificationReportsPropertyDriftTest,
	"LGUI.Prefab.SaveVerification.ReportsPropertyDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexPrefabSaveVerificationReportsPropertyDriftTest::RunTest(const FString& Parameters)
{
	using namespace LexUIPrefabSaveVerificationTestLocal;
	FSavedPrefabFixture Fixture;
	TestTrue(TEXT("Fixture prefab saves"), Fixture.BuildAndSave());

	// Drift a value after the save: the payload will load back with the old width.
	Fixture.Child->SetWidth(777.0f);

	const LexUIPrefabSystem::FLexUIPrefabSaveVerificationResult Result =
		LexUIPrefabSystem::VerifyPrefabSaveRoundTrip(Fixture.Prefab, Fixture.Root);
	TestTrue(TEXT("Value drift alone is not a structural failure"), Result.bStructureMatches);
	TestTrue(TEXT("Value drift is reported"), Result.PropertyDifferences.Num() > 0);
	bool bMentionsDriftedChild = false;
	for (const FString& Difference : Result.PropertyDifferences)
	{
		bMentionsDriftedChild |= Difference.Contains(TEXT("VerifyChild"));
	}
	TestTrue(TEXT("The report points at the drifted widget"), bMentionsDriftedChild);
	return true;
}

#endif
