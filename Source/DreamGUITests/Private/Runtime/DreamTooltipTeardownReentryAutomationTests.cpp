// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamTooltipTeardownReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "Engine/World.h"
#include "Interaction/DreamUITooltip.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamTooltipTeardownReentryTest,
	"DreamGUI.Tooltip.TeardownKeepsTheNewestBubbleOwnedAndRemovable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamTooltipTeardownReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Hide callback opens a new tooltip"));
	OutTestCommands.Add(TEXT("hide"));
	OutBeautifiedNames.Add(TEXT("Replacement callback opens an even newer tooltip"));
	OutTestCommands.Add(TEXT("replace"));
	OutBeautifiedNames.Add(TEXT("An ordinary custom tooltip still hides completely"));
	OutTestCommands.Add(TEXT("normal"));
}

bool FDreamTooltipTeardownReentryTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the real playing world rig is usable"), Rig.IsUsable()))return false;
	UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("the world's tooltip service exists"), Tooltip))return false;
	UDreamWidget* OriginalSource = Rig.MakeWidget(TEXT("OriginalTooltipSource"), nullptr, FVector2D(120.0, 40.0));
	UDreamWidget* NewestSource = Rig.MakeWidget(TEXT("NewestTooltipSource"), nullptr, FVector2D(120.0, 40.0));
	UDreamWidget* ReplacingSource = Rig.MakeWidget(TEXT("ReplacingTooltipSource"), nullptr, FVector2D(120.0, 40.0));
	if (!TestTrue(TEXT("the three public tooltip sources exist"),
		IsValid(OriginalSource) && IsValid(NewestSource) && IsValid(ReplacingSource)))return false;
	OriginalSource->SetToolTipWidgetClass(UDreamTooltipTeardownReentryFixture::StaticClass());
	NewestSource->SetToolTipText(FText::FromString(TEXT("The callback's newest help")));
	ReplacingSource->SetToolTipText(FText::FromString(TEXT("The superseded replacement")));
	Rig.PumpFrames(2);
	Tooltip->ShowTooltipFor(OriginalSource);
	UDreamWidget* OriginalBubble = Tooltip->GetBubbleForUser(0);
	if (!TestNotNull(TEXT("the original tooltip actually opened"), OriginalBubble))return false;
	UDreamTooltipTeardownReentryFixture* Custom = nullptr;
	for (UDreamWidget* Child : OriginalBubble->GetChildren())
	{
		if (UDreamTooltipTeardownReentryFixture* Candidate = Cast<UDreamTooltipTeardownReentryFixture>(Child))
		{
			Custom = Candidate;
			break;
		}
	}
	if (!TestTrue(TEXT("the custom tooltip was instantiated and entered its real lifetime"),
		IsValid(Custom) && Custom->HasBegunPlay()))return false;
	const TWeakObjectPtr<UDreamTooltipTeardownReentryFixture> WeakCustom(Custom);
	const TWeakObjectPtr<UDreamWidget> OldBubble(OriginalBubble);
	UDreamWidget* Host = OriginalBubble->GetParent();
	int32 DestructCalls = 0;
	TWeakObjectPtr<UDreamWidget> NewestBubble;
	ON_SCOPE_EXIT
	{
		if (UDreamTooltipTeardownReentryFixture* LiveCustom = WeakCustom.Get())LiveCustom->DestructCallback = nullptr;
		Tooltip->HideTooltipForUser(0);
		// A red baseline loses the new holder from its state. Dispose of the observed orphan
		// ourselves so it cannot pollute another test or the world's teardown.
		if (UDreamWidget* Orphan = NewestBubble.Get())Orphan->DestroyWidget();
	};
	Custom->DestructCallback = [&]()
	{
		++DestructCalls;
		if (Parameters != TEXT("normal"))
		{
			Tooltip->ShowTooltipFor(NewestSource);
			NewestBubble = Tooltip->GetBubbleForUser(0);
		}
	};
	if (Parameters == TEXT("replace"))Tooltip->ShowTooltipFor(ReplacingSource);
	else Tooltip->HideTooltipForUser(0);

	TestEqual(TEXT("the actual custom tooltip destruct callback ran once"), DestructCalls, 1);
	TestFalse(TEXT("the old bubble was destroyed"), OldBubble.IsValid());
	auto CountLiveBubbles = [Host]()
	{
		int32 Count = 0;
		for (UDreamWidget* Child : Host->GetChildren())
		{
			if (IsValid(Child) && Child->GetDisplayName() == TEXT("DreamUITooltip"))++Count;
		}
		return Count;
	};
	if (Parameters != TEXT("normal"))
	{
		TestTrue(TEXT("the callback really opened a new bubble"), NewestBubble.IsValid());
		TestEqual(TEXT("the latest request still owns the visible tooltip"), Tooltip->GetShownForUser(0), NewestSource);
		TestEqual(TEXT("the service still tracks the bubble created by that request"), Tooltip->GetBubbleForUser(0), NewestBubble.Get());
		TestEqual(TEXT("only the newest bubble remains on the screen root"), CountLiveBubbles(), 1);
		Tooltip->HideTooltipForUser(0);
		TestFalse(TEXT("a later public hide destroys the newest bubble"), NewestBubble.IsValid());
	}
	TestNull(TEXT("the public hide leaves no tracked bubble"), Tooltip->GetBubbleForUser(0));
	TestNull(TEXT("the public hide leaves no shown source"), Tooltip->GetShownForUser(0));
	TestEqual(TEXT("the public hide leaves no orphan bubble on the screen"), CountLiveBubbles(), 0);
	return true;
}

#endif
