// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIInputAction.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverUntil.h"
#include "DreamNavigationTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * KEYS THROUGH SLATE, AND A LEVEL TRAVEL, IN A REAL PLAY SESSION.
 *
 * A key the platform delivers reaches the editor's FSlateApplication, which offers it to its input pre-processors and
 * then routes it along the keyboard focus: to the play session's viewport widget when that has the focus, then the
 * scene viewport, the viewport client and the player controller. KeyThroughSlate starts it at FSlateApplication, so
 * every gate after that is the engine's; the modifiers go down as their own key events, as a keyboard sends them. The
 * travel is UGameplayStatics::OpenLevel, after which the rig builds itself on the new world as it did on the first.
 */
namespace DreamDriverPieKeysTestLocal
{
	FWaitTimeout ConditionLimit()
	{
		return FWaitTimeout::InSeconds(2.0);
	}

	FWaitTimeout StepLimit()
	{
		return FWaitTimeout::InSeconds(3.0);
	}

	/** A global binding of InKey -- with Ctrl when bInRequiresCtrl -- on player 0 of InWorld, counting the times it fires. */
	struct FPieCountedBinding
	{
		TStrongObjectPtr<UDataTable> Table;
		TStrongObjectPtr<UDreamActionCallCounter> Counter;

		int32 Count() const { return Counter.IsValid() ? Counter->CallCount : -1; }
	};

	bool BindPieGlobalAction(UWorld* InWorld, const FKey& InKey, bool bInRequiresCtrl, FPieCountedBinding& OutBinding)
	{
		UDreamUIActionRouter* Router = InWorld != nullptr ? UDreamUIActionRouter::Get(InWorld) : nullptr;
		if (Router == nullptr)
		{
			return false;
		}
		const FName RowName(TEXT("PieProbe"));
		// Outside the play world, in the transient package: the end of the session must find nothing of its world held.
		OutBinding.Table.Reset(NewObject<UDataTable>(GetTransientPackage()));
		OutBinding.Table->RowStruct = FDreamUIInputActionData::StaticStruct();
		FDreamUIInputActionData Row;
		Row.DisplayName = FText::FromName(RowName);
		Row.KeyboardKey = InKey;
		Row.bRequiresCtrl = bInRequiresCtrl;
		OutBinding.Table->AddRow(RowName, Row);
		OutBinding.Counter.Reset(NewObject<UDreamActionCallCounter>());

		FDataTableRowHandle Handle;
		Handle.DataTable = OutBinding.Table.Get();
		Handle.RowName = RowName;
		FDreamUIActionExecutedDelegate Callback;
		Callback.BindUFunction(OutBinding.Counter.Get(), TEXT("Fire"));
		Router->RegisterAction(nullptr, Handle, Callback);
		return true;
	}

	/** What a travel's two callbacks saw, weakly: the world left behind is collected, and must be. */
	struct FTravelProbe
	{
		TWeakObjectPtr<UWorld> LeftWorld;
		TWeakObjectPtr<UWorld> ArrivedWorld;
		FString LeftWorldName;
		int32 BeforeCalls = 0;
		int32 AfterCalls = 0;
	};
}

/*
 * A key through Slate reaches the player: the F9 key down and up through FSlateApplication, with the keyboard focus on the
 * session's viewport where the rig put it, fires the player's binding -- the scene viewport handing the key to the
 * viewport client, the client to the player controller, the preset's binding to the action router. Ctrl+F9 fires the
 * binding that asks for Ctrl, which it can only see if Ctrl went down as a key of its own in the player's input. And
 * the focus is still on the viewport itself afterwards.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieKeyThroughSlateTest,
	"DreamGUI.Pie.AKeyPressedThroughSlateReachesThePlayersBindingAndLeavesTheFocusOnTheViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieKeyThroughSlateTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieKeysTestLocal;
	const TSharedRef<FPieCountedBinding> PlainF9 = MakeShared<FPieCountedBinding>();
	const TSharedRef<FPieCountedBinding> CtrlF10 = MakeShared<FPieCountedBinding>();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	const TWeakPtr<FDreamDriverPieRig> WeakRig = Rig;
	Rig->Start();
	Rig->WhenReady([this, PlainF9, CtrlF10](FDreamDriverPieRig& InRig)
	{
		TestTrue(TEXT("The F9 key is bound"), BindPieGlobalAction(InRig.GetWorld(), EKeys::F9, /*bInRequiresCtrl*/ false, *PlainF9));
		TestTrue(TEXT("Ctrl+F10 is bound"), BindPieGlobalAction(InRig.GetWorld(), EKeys::F10, /*bInRequiresCtrl*/ true, *CtrlF10));
	});

	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Then([this, WeakRig](FDreamDriverContext&)
	{
		const TSharedPtr<FDreamDriverPieRig> Pinned = WeakRig.Pin();
		TestTrue(TEXT("The keyboard focus is on the session's viewport before any key"), Pinned.IsValid() && Pinned->IsKeyboardFocusOnViewport());
	});
	FDreamDriverPieRig::KeyThroughSlate(Steps, EKeys::F9);
	Steps.Wait(FDreamUntil::Condition([PlainF9]() { return PlainF9->Count() > 0; }, ConditionLimit()),
		StepLimit(), TEXT("the F9 key through Slate to fire the player's binding"));
	FDreamDriverPieRig::KeyThroughSlate(Steps, EKeys::F10);
	FDreamDriverPieRig::KeyThroughSlate(Steps, EKeys::F10, EDreamDriverModifierKeys::Ctrl);
	Steps.Wait(FDreamUntil::Condition([CtrlF10]() { return CtrlF10->Count() > 0; }, ConditionLimit()),
		StepLimit(), TEXT("Ctrl+F10 through Slate to fire the binding that asks for Ctrl"));
	Steps.Then([this, WeakRig, PlainF9, CtrlF10](FDreamDriverContext& InContext)
	{
		TestEqual(TEXT("The F9 key fired its binding once"), PlainF9->Count(), 1);
		TestEqual(TEXT("The F10 key alone did not fire the Ctrl binding, and Ctrl+F10 fired it once"), CtrlF10->Count(), 1);
		TestFalse(TEXT("Every key went up again in the player's input"),
			InContext.PlayerController != nullptr && (InContext.PlayerController->IsInputKeyDown(EKeys::F10) || InContext.PlayerController->IsInputKeyDown(EKeys::LeftControl)));
		const TSharedPtr<FDreamDriverPieRig> Pinned = WeakRig.Pin();
		TestTrue(TEXT("The keyboard focus is still on the session's viewport"), Pinned.IsValid() && Pinned->IsKeyboardFocusOnViewport());
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

/*
 * A level travel in a play session, as a game changes level: the rig lets go of the first world before it goes, the
 * travel's callbacks see the world left and the world arrived in, and the rig built on the new world drives it -- a
 * button made there after the travel is clicked through the new player controller.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieTravelTest,
	"DreamGUI.Pie.AfterALevelTravelTheRigIsBuiltOnTheNewWorldAndDrivesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieTravelTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieKeysTestLocal;
	const TSharedRef<FTravelProbe> Probe = MakeShared<FTravelProbe>();
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	const TWeakPtr<FDreamDriverPieRig> WeakRig = Rig;
	Rig->Start();
	Rig->Travel(TEXT("/Engine/Maps/Entry"),
		[Probe](UWorld& InWorld)
		{
			Probe->LeftWorld = &InWorld;
			Probe->LeftWorldName = InWorld.GetName();
			++Probe->BeforeCalls;
		},
		[Probe](UWorld& InWorld)
		{
			Probe->ArrivedWorld = &InWorld;
			++Probe->AfterCalls;
		});
	Rig->WhenReady([Listener](FDreamDriverPieRig& InRig)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0));
		if (Button != nullptr)
		{
			Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
		}
	});
	Rig->Sequence()
		.Then([this, WeakRig, Probe](FDreamDriverContext& InContext)
		{
			const TSharedPtr<FDreamDriverPieRig> Pinned = WeakRig.Pin();
			TestTrue(TEXT("The travel arrived once"), Pinned.IsValid() && Pinned->GetTravelCount() == 1);
			TestEqual(TEXT("The callback before it ran once"), Probe->BeforeCalls, 1);
			TestEqual(TEXT("...and the one after it once"), Probe->AfterCalls, 1);
			UWorld* Arrived = Probe->ArrivedWorld.Get();
			TestTrue(FString::Printf(TEXT("The world arrived in is another world than %s"), *Probe->LeftWorldName),
				Arrived != nullptr && Arrived != Probe->LeftWorld.Get());
			TestTrue(TEXT("The rig's context is on the world arrived in"), Arrived != nullptr && InContext.World == Arrived);
			TestTrue(TEXT("...with its own player controller"), InContext.PlayerController != nullptr && InContext.PlayerController->GetWorld() == Arrived);
		})
		.MoveTo(FDreamBy::Name(TEXT("Play")))
		.Press()
		.Release()
		.Wait(FDreamUntil::Condition([Listener]() { return Listener->ClickedCount > 0; }, ConditionLimit()),
			StepLimit(), TEXT("the click on the new world's button"))
		.Then([this, Listener](FDreamDriverContext&)
		{
			TestEqual(TEXT("The button made on the new world was clicked once"), Listener->ClickedCount, 1);
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
