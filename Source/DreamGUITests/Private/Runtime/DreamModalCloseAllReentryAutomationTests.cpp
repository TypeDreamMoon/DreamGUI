// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "DreamScopedWorld.h"
#include "Engine/World.h"
#include "Interaction/DreamUIModal.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamModalCloseAllReentryTest,
	"DreamGUI.Modal.CloseAllKeepsItsOriginalTargetsWhenAResultOpensAnotherModal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamModalCloseAllReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("A result opens a replacement during close all"));
	OutTestCommands.Add(TEXT("reopen"));
	OutBeautifiedNames.Add(TEXT("Ordinary close all still answers both callers"));
	OutTestCommands.Add(TEXT("normal"));
	OutBeautifiedNames.Add(TEXT("World teardown during the top result abandons later callbacks"));
	OutTestCommands.Add(TEXT("teardown"));
}

bool FDreamModalCloseAllReentryTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIModalSubsystem* Modals = TestWorld.World->GetSubsystem<UDreamUIModalSubsystem>();
	if (!TestNotNull(TEXT("the real game world's modal service exists"), Modals))return false;
	int32 BottomAnswers = 0;
	int32 TopAnswers = 0;
	int32 ReplacementAnswers = 0;
	bool bOpenReplacement = Parameters == TEXT("reopen");
	const bool bTeardown = Parameters == TEXT("teardown");
	UDreamUINavigationStack* Navigation = UDreamUINavigationStack::Get(TestWorld.World);
	if (bTeardown && !TestNotNull(TEXT("the navigation service exists"), Navigation))return false;
	TArray<FName> AnswerOrder;
	TWeakObjectPtr<UDreamUserWidget> Replacement;
	ON_SCOPE_EXIT
	{
		bOpenReplacement = false;
		Modals->CloseAllModals(TEXT("Cleanup"));
	};

	Modals->ShowModalNative(UDreamUserWidget::StaticClass(), [&](FName Result)
	{
		++BottomAnswers;
		AnswerOrder.Add(TEXT("Bottom"));
	});
	const TWeakObjectPtr<UDreamUserWidget> Bottom(Modals->GetActiveModalWidget());
	const TWeakObjectPtr<UDreamWidget> BottomLayer(Bottom.IsValid() ? Bottom->GetParent() : nullptr);
	Modals->ShowModalNative(UDreamUserWidget::StaticClass(), [&](FName Result)
	{
		++TopAnswers;
		AnswerOrder.Add(TEXT("Top"));
		if (bTeardown)
		{
			// Navigation forgets its scopes during world teardown; closing the claimed modal batch
			// afterwards must neither deliver more results nor leave its remaining widgets orphaned.
			Navigation->TeardownForWorld(*TestWorld.World);
			Modals->TeardownForWorld(*TestWorld.World);
		}
		if (bOpenReplacement)
		{
			bOpenReplacement = false;
			Modals->ShowModalNative(UDreamUserWidget::StaticClass(), [&](FName ReplacementResult)
			{
				++ReplacementAnswers;
			});
			Replacement = Modals->GetActiveModalWidget();
		}
	});
	const TWeakObjectPtr<UDreamUserWidget> Top(Modals->GetActiveModalWidget());
	if (!TestTrue(TEXT("two distinct live dialogs are on the real modal stack"),
		Bottom.IsValid() && Top.IsValid() && Bottom != Top && Modals->GetModalDepth() == 2))return false;

	Modals->CloseAllModals(TEXT("Dismissed"));
	TestEqual(TEXT("the original top caller is answered once"), TopAnswers, 1);
	TestEqual(TEXT("the bottom result is abandoned only when the world tears down"), BottomAnswers, bTeardown ? 0 : 1);
	TestFalse(TEXT("the original top dialog was destroyed"), Top.IsValid());
	TestFalse(TEXT("the original bottom dialog was destroyed"), Bottom.IsValid());
	TestFalse(TEXT("the original bottom layer is not orphaned"), BottomLayer.IsValid());
	if (TestEqual(TEXT("only results from the live world are delivered"), AnswerOrder.Num(), bTeardown ? 1 : 2))
	{
		TestEqual(TEXT("the original top answers first"), AnswerOrder[0], FName(TEXT("Top")));
		if (!bTeardown)TestEqual(TEXT("the original bottom answers second"), AnswerOrder[1], FName(TEXT("Bottom")));
	}
	if (bTeardown)
	{
		TArray<UDreamUINavigationScope*> RemainingScopes;
		Navigation->GetScopeStack(0, RemainingScopes);
		TestEqual(TEXT("finishing the obsolete batch does not recreate navigation scopes"), RemainingScopes.Num(), 0);
		TestNull(TEXT("world teardown leaves no active navigation scope"), Navigation->GetActiveScope());
	}
	if (Parameters == TEXT("reopen"))
	{
		TestTrue(TEXT("the replacement opened by the result remains alive"), Replacement.IsValid());
		TestEqual(TEXT("the obsolete close batch has not answered the new caller"), ReplacementAnswers, 0);
		TestEqual(TEXT("only the replacement is left on the stack"), Modals->GetModalDepth(), 1);
		TestEqual(TEXT("the service tracks the replacement as its top"), Modals->GetActiveModalWidget(), Replacement.Get());
		Modals->CloseTopModal(TEXT("Confirm"));
		TestEqual(TEXT("the new caller can be answered by a later close"), ReplacementAnswers, 1);
		TestEqual(TEXT("the old bottom caller is not answered again"), BottomAnswers, 1);
	}
	TestEqual(TEXT("the modal stack can end empty"), Modals->GetModalDepth(), 0);
	return true;
}

#endif
