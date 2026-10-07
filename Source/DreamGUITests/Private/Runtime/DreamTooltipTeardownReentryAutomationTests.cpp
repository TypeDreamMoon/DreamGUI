// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamTooltipTeardownReentryTestTypes.h"
#include "Driver/DreamDriverRig.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
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
	OutBeautifiedNames.Add(TEXT("A replacement survives other users being added in the callback"));
	OutTestCommands.Add(TEXT("rehash"));
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
		Tooltip->HideTooltip();
		// A red baseline loses the new holder from its state. Dispose of the observed orphan
		// ourselves so it cannot pollute another test or the world's teardown.
		if (UDreamWidget* Orphan = NewestBubble.Get())Orphan->DestroyWidget();
	};
	Custom->DestructCallback = [&]()
	{
		++DestructCalls;
		if (Parameters == TEXT("rehash"))
		{
			UDreamUIInputSubsystem* Input = Rig.GetWorld()->GetSubsystem<UDreamUIInputSubsystem>();
			if (TestNotNull(TEXT("the real input service exists for the other users"), Input))
			{
				for (int32 UserIndex = 1; UserIndex <= 16; ++UserIndex)
				{
					UDreamUIInputUser* User = Input->GetOrCreateUser(UserIndex);
					if (!TestNotNull(TEXT("another input user exists"), User))continue;
					UDreamPointerEventData* Pointer = User->GetPointerEventData(DreamUIPointerIds::Mouse, true);
					if (!TestNotNull(TEXT("the new user's pointer exists"), Pointer))continue;
					Pointer->EnterWidget = NewestSource;
					// Use the real pipeline's public dispatch: each enter arms that user's tooltip
					// state while the first user's old holder is still running Destruct.
					User->CallOnPointerEnter(NewestSource, Pointer);
					TestEqual(TEXT("the enter was dispatched for its own user"), Pointer->UserIndex, UserIndex);
				}
			}
		}
		if (Parameters != TEXT("normal"))
		{
			Tooltip->ShowTooltipFor(NewestSource);
			NewestBubble = Tooltip->GetBubbleForUser(0);
		}
	};
	if (Parameters == TEXT("replace") || Parameters == TEXT("rehash"))Tooltip->ShowTooltipFor(ReplacingSource);
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

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamTooltipShowCancellationTest,
	"DreamGUI.Tooltip.AShowCancelledFromContentCallbacksDoesNotResume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamTooltipShowCancellationTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("The content provider hides the pending tooltip"));
	OutTestCommands.Add(TEXT("provider"));
	OutBeautifiedNames.Add(TEXT("The custom tooltip construct callback hides it"));
	OutTestCommands.Add(TEXT("construct"));
	OutBeautifiedNames.Add(TEXT("The custom tooltip initialize callback cancels before parenting"));
	OutTestCommands.Add(TEXT("initialize"));
	OutBeautifiedNames.Add(TEXT("The holder's dimension callback hides the custom tooltip"));
	OutTestCommands.Add(TEXT("dimensions"));
}

bool FDreamTooltipShowCancellationTest::RunTest(const FString& Parameters)
{
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("the real playing world rig is usable"), Rig.IsUsable()))return false;
	UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld());
	UDreamWidget* Source = Rig.MakeWidget(TEXT("CancelledTooltipSource"), nullptr, FVector2D(120.0, 40.0));
	if (!TestTrue(TEXT("the service and source exist"), IsValid(Tooltip) && IsValid(Source)))return false;
	Source->SetToolTipText(FText::FromString(TEXT("Help after the cancellation")));
	int32 CallbackCalls = 0;
	TWeakObjectPtr<UDreamWidget> CancelledHolder;
	TWeakObjectPtr<UDreamTooltipTeardownReentryFixture> CancelledCustom;
	auto Cancel = [&]()
	{
		++CallbackCalls;
		CancelledHolder = Tooltip->GetBubbleForUser(0);
		if ((Parameters == TEXT("construct") || Parameters == TEXT("dimensions")) && CancelledHolder.IsValid())
		{
			for (UDreamWidget* Child : CancelledHolder->GetChildren())
			{
				if (UDreamTooltipTeardownReentryFixture* Custom = Cast<UDreamTooltipTeardownReentryFixture>(Child))CancelledCustom = Custom;
			}
			TestTrue(TEXT("the callback belongs to an actual custom child of the holder"), CancelledCustom.IsValid());
		}
		Tooltip->HideTooltipForUser(0);
	};
	ON_SCOPE_EXIT
	{
		UDreamTooltipTeardownReentryFixture::ConstructCallback = nullptr;
		UDreamTooltipTeardownReentryFixture::InitializedCallback = nullptr;
		Tooltip->HideTooltip();
		if (UDreamWidget* Orphan = CancelledHolder.Get())Orphan->DestroyWidget();
		if (UDreamTooltipTeardownReentryFixture* Orphan = CancelledCustom.Get())Orphan->DestroyWidget();
	};
	if (Parameters == TEXT("provider"))
	{
		UDreamTooltipSourceReentryFixture* Provider = Cast<UDreamTooltipSourceReentryFixture>(
			Source->AddComponent(UDreamTooltipSourceReentryFixture::StaticClass()));
		if (!TestNotNull(TEXT("the real tooltip provider component was added"), Provider))return false;
		Provider->ProviderCallback = Cancel;
	}
	else
	{
		Source->SetToolTipWidgetClass(UDreamTooltipTeardownReentryFixture::StaticClass());
		if (Parameters == TEXT("initialize"))
		{
			UDreamTooltipTeardownReentryFixture::InitializedCallback = [&](UDreamTooltipTeardownReentryFixture* Custom)
			{
				CancelledCustom = Custom;
				TestTrue(TEXT("the initialized callback receives a real custom instance"), CancelledCustom.IsValid());
				TestNull(TEXT("initialization runs before the custom widget is parented"), Custom->GetParent());
				Cancel();
			};
		}
		else if (Parameters == TEXT("dimensions"))
		{
			UDreamTooltipTeardownReentryFixture::ConstructCallback = [&]()
			{
				UDreamWidget* Holder = Tooltip->GetBubbleForUser(0);
				if (!TestNotNull(TEXT("the constructing tooltip has its real holder"), Holder))return;
				for (UDreamWidget* Child : Holder->GetChildren())
				{
					if (UDreamTooltipTeardownReentryFixture* Custom = Cast<UDreamTooltipTeardownReentryFixture>(Child))
					{
						Custom->SetSizeDelta(FVector2D(230.0, 80.0));
					}
				}
				Holder->GetDimensionChangedEvent().AddLambda([&](bool, bool, bool)
				{
					if (CallbackCalls == 0)Cancel();
				});
			};
		}
		else
		{
			UDreamTooltipTeardownReentryFixture::ConstructCallback = Cancel;
		}
	}
	Tooltip->ShowTooltipFor(Source);
	TestEqual(TEXT("the actual content callback cancelled once"), CallbackCalls, 1);
	TestNull(TEXT("the cancelled show does not publish its source afterwards"), Tooltip->GetShownForUser(0));
	TestNull(TEXT("the cancelled show leaves no owned holder"), Tooltip->GetBubbleForUser(0));
	TestFalse(TEXT("any holder present during cancellation was destroyed"), CancelledHolder.IsValid());
	TestFalse(TEXT("the cancelled custom tooltip was destroyed with its holder"), CancelledCustom.IsValid());

	// The callbacks are one-shot: cancellation must not permanently disable later requests.
	Tooltip->ShowTooltipFor(Source);
	TestEqual(TEXT("a later ordinary request can show the same source"), Tooltip->GetShownForUser(0), Source);
	TestNotNull(TEXT("the later request creates a real bubble"), Tooltip->GetBubbleForUser(0));
	Tooltip->HideTooltipForUser(0);
	TestNull(TEXT("the later bubble can hide normally"), Tooltip->GetBubbleForUser(0));
	return true;
}

#endif
