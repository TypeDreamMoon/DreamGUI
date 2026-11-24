// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUserWidget.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Fixtures/DreamScreenReentryTestTypes.h"
#include "DreamScopedWorld.h"
#include "UObject/StrongObjectPtr.h"

namespace DreamBudgetTest
{
	struct FCase
	{
		DreamTests::FScopedGameWorld Host;
		TStrongObjectPtr<UDreamScreenCreationRequest> Open, Cancelled, Failed, Interrupted;
		bool bReachedCreation = false;
		TStrongObjectPtr<UDreamCreationCompletionProbe> Probe;
		TStrongObjectPtr<UGameInstance> Players;
		TStrongObjectPtr<UDreamScreenCreationRequest> Other;
		ULocalPlayer* Local[2] = {};
		APlayerController* Controllers[2] = {};
		TStrongObjectPtr<UDreamWidget> Reference;
		UClass* Class = nullptr;
		double Start = FPlatformTime::Seconds();
	};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamBudgetCreationTest, "DreamGUI.Screen.Budgeted.CompletionIsDeferredAndMatchesTheSynchronousTree", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FDreamCheckBudgetCreation, TSharedPtr<DreamBudgetTest::FCase>, Case, FAutomationTestBase*, Test);
bool FDreamCheckBudgetCreation::Update()
{
	auto* W = Case->Host.World;
	auto* Settings = GetMutableDefault<UDreamGUISettings>();
	TGuardValue<float> Budget(Settings->CreationBudgetMs, .1f);
	FWorldDelegates::OnWorldPostActorTick.Broadcast(W, LEVELTICK_All, 0.016f);
	if (!Case->bReachedCreation && Case->Interrupted->GetState() == EDreamScreenCreationState::Creating)
	{
		Case->bReachedCreation = true;
		Test->TestNull(TEXT("unpublished throughout creation"),Case->Interrupted->GetPage());
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		Case->Interrupted->Cancel();
	}
	if ((!Case->Open->GetPage() || !Case->Other->GetPage() || !Case->Probe->Followup || !Case->Probe->Followup->GetPage() || Case->Interrupted->GetState() == EDreamScreenCreationState::Creating) && FPlatformTime::Seconds()-Case->Start < 15.) return false;
	using E = EDreamScreenCreationState;
	Test->TestTrue(TEXT("cancel during creation after GC"),Case->bReachedCreation && Case->Interrupted->GetState()==E::Cancelled);
	Test->TestTrue(TEXT("valid request ready"), Case->Open->GetState() == E::Ready);
	Test->TestTrue(TEXT("cancel wins before visible"), Case->Cancelled->GetState() == E::Cancelled && !Case->Cancelled->GetPage());
	Test->TestTrue(TEXT("null class failed"), Case->Failed->GetState() == E::Failed && !Case->Failed->GetPage());
	auto* S = UDreamScreenUISubsystem::Get(W);
	if (auto* Page = Case->Open->GetPage())
	{
		Test->TestTrue(TEXT("published, active, owned by this world"), S->IsInViewport(Page) && Page->GetWidgetActive() && Page->GetWorld()==W);
		TArray<UDreamWidget*> A, B;
		UDreamWidget::CollectChildrenWidgets(Page, A, true);
		UDreamWidget::CollectChildrenWidgets(Case->Reference.Get(), B, true);
		Test->TestEqual(TEXT("same tree size"), A.Num(), B.Num());
		for (int32 I=0; I<FMath::Min(A.Num(),B.Num()); ++I)
		{
			Test->TestTrue(TEXT("same widget type and registration"), A[I]->GetClass()==B[I]->GetClass() && A[I]->HasRegistered());
		}
	}
	Test->TestEqual(TEXT("terminal listener called once despite reentrancy"),Case->Probe->Calls,1);
	Test->TestTrue(TEXT("listener could enqueue another screen"),Case->Probe->Followup && Case->Probe->Followup->GetPage());
	if (auto* P0=Case->Open->GetPage()) Test->TestTrue(TEXT("player zero screen"),P0->GetParent()==S->GetScreenRoot(Case->Controllers[0]));
	if (auto* P1=Case->Other->GetPage()) Test->TestTrue(TEXT("player one separate screen"),P1->GetParent()==S->GetScreenRoot(Case->Controllers[1]) && P1->GetParent()!=S->GetScreenRoot(Case->Controllers[0]));
	Test->TestNotNull(TEXT("second player completed"),Case->Other->GetPage());
	TStrongObjectPtr<UDreamScreenCreationRequest> Travel(S->OpenScreenBudgeted(Case->Class));
	S->TeardownForWorld(*W);
	Test->TestTrue(TEXT("world teardown cancels pending work"), Travel->GetState()==E::Cancelled && !Travel->GetPage());
	for (int32 I=1; I>=0; --I)
	{
		Case->Controllers[I]->Player = nullptr;
		Case->Players->RemoveLocalPlayer(Case->Local[I]);
	}
	W->SetGameInstance(nullptr);
	return true;
}
bool FDreamBudgetCreationTest::RunTest(const FString&)
{
	auto Case = MakeShared<DreamBudgetTest::FCase>();
	auto* S = UDreamScreenUISubsystem::Get(Case->Host.World);
	Case->Class = LoadClass<UDreamUserWidget>(nullptr,TEXT("/DreamGUI/Controls/BP_Button.BP_Button_C"));
	if (!TestNotNull(TEXT("screen service"),S) || !TestNotNull(TEXT("test control"),Case->Class)) return false;
	Case->Reference.Reset(S->CreateWidgetOnScreen(Case->Class,0));
	if (!TestNotNull(TEXT("synchronous reference"),Case->Reference.Get())) return false;
	Case->Players.Reset(NewObject<UGameInstance>(GEngine));
	Case->Host.World->SetGameInstance(Case->Players.Get());
	for (int32 I=0; I<2; ++I)
	{
		Case->Local[I]=NewObject<ULocalPlayer>(GEngine);
		Case->Players->AddLocalPlayer(Case->Local[I], FGenericPlatformMisc::GetPlatformUserForUserIndex(I));
		Case->Controllers[I]=Case->Host.World->SpawnActor<APlayerController>();
		if (!TestNotNull(TEXT("fixture controller"),Case->Controllers[I])) return false;
		Case->Controllers[I]->Player=Case->Local[I];
	}
	Case->Open.Reset(S->OpenScreenBudgeted(Case->Class,Case->Controllers[0]));
	Case->Other.Reset(S->OpenScreenBudgeted(Case->Class,Case->Controllers[1]));
	Case->Probe.Reset(NewObject<UDreamCreationCompletionProbe>());
	Case->Probe->Screens=S;
	Case->Open->OnFinished.AddDynamic(Case->Probe.Get(), &UDreamCreationCompletionProbe::Finished);
	Case->Interrupted.Reset(S->OpenScreenBudgeted(Case->Class));
	Case->Cancelled.Reset(S->OpenScreenBudgeted(Case->Class));
	Case->Failed.Reset(S->OpenScreenBudgeted(nullptr));
	Case->Cancelled->Cancel();
	TestTrue(TEXT("even a resident class waits for a later frame"), !Case->Open->GetPage() && Case->Open->GetState()==EDreamScreenCreationState::Queued);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	ADD_LATENT_AUTOMATION_COMMAND(FDreamCheckBudgetCreation(Case,this));
	return true;
}
#endif
