// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIActionTypes.h"
#include "Core/DreamUIInputServices.h"
#include "Engine/DataTable.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIActionBar.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIInputAction.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "DreamNavigationTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * THE PROMPT BAR'S PROMPTS AS BUTTONS: DISABLED, AND ON A SCALED CANVAS.
 *
 * CommonUI's bound action bar builds one UCommonBoundActionButton per prompt, and a bound action button is a button: clicking
 * it, tapping it or confirming it with the pad runs the action it shows (UCommonBoundActionButton::NativeOnClicked executes
 * the binding's OnExecuteAction, CommonUI/Private/Input/CommonBoundActionButton.cpp:37-45). The bar disabled -- UWidget::
 * SetIsEnabled(false) on it -- disables every button in it: Slate's hit test cuts a click's or a finger's path there
 * (FHittestGrid::GetBubblePath) and the D-pad passes over them (FHittestGrid::FindNextFocusableWidget), so no prompt runs
 * anything; enabled again, each does. On a canvas scaled to half and to double the click, the tap and the D-pad find a prompt
 * where it is drawn.
 *
 * The bar here is built as a project builds one: the prompt entries are buttons (UDreamButton as the bar's entry class), set
 * in a row by the test once the bar has made them, and the actions are registered with the router for every screen.
 */
namespace DreamActionBarCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };
	const FVector2D EntrySize(160.0, 40.0);

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	/** One action on the bar: the name it is shown by, the keys it answers to, and what counts its firing. */
	struct FBarAction
	{
		FString Name;
		FKey KeyboardKey;
		FKey GamepadKey;
		TStrongObjectPtr<UDreamActionCallCounter> Counter;
		FDreamUIActionHandle Handle;
		/** The prompt entry the bar made for it, once it has. */
		UDreamWidget* Entry = nullptr;
	};

	/** The bar, its widget and the actions it shows, each with its entry. */
	struct FBarScene
	{
		TStrongObjectPtr<UDataTable> Table;
		TArray<FBarAction> Actions;
		UDreamWidget* BarWidget = nullptr;
		UDreamUIActionBar* Bar = nullptr;
		UDreamUIActionRouter* Router = nullptr;

		int32 TotalFired() const
		{
			int32 Total = 0;
			for (const FBarAction& Action : Actions)
			{
				Total += Action.Counter.IsValid() ? Action.Counter->CallCount : 0;
			}
			return Total;
		}

		/** Every action registered here taken off the router again, so nothing outlives the test's world. */
		void Unregister()
		{
			if (Router != nullptr)
			{
				for (const FBarAction& Action : Actions)
				{
					Router->UnregisterAction(Action.Handle);
				}
			}
		}
	};

	/**
	 * InNames as actions registered for every screen, each with a counter; a bar on a widget at InBarPosition whose entries
	 * are buttons, built from the router, and each entry set in a row in the bar and matched to its action by the prompt it
	 * shows. False, having said why, when any of it could not be made.
	 */
	bool MakeBar(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TArray<FString>& InNames, const FVector2D& InBarPosition, FBarScene& OutScene)
	{
		OutScene.Router = UDreamUIActionRouter::Get(InRig.GetWorld());
		if (!InTest.TestNotNull(TEXT("The rig's world has an action router"), OutScene.Router))
		{
			return false;
		}
		const FKey KeyboardKeys[] = { EKeys::X, EKeys::Y };
		const FKey GamepadKeys[] = { EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top };
		OutScene.Table.Reset(NewObject<UDataTable>(GetTransientPackage()));
		OutScene.Table->RowStruct = FDreamUIInputActionData::StaticStruct();
		for (int32 Index = 0; Index < InNames.Num() && Index < static_cast<int32>(UE_ARRAY_COUNT(KeyboardKeys)); ++Index)
		{
			FBarAction& Action = OutScene.Actions.AddDefaulted_GetRef();
			Action.Name = InNames[Index];
			Action.KeyboardKey = KeyboardKeys[Index];
			Action.GamepadKey = GamepadKeys[Index];
			FDreamUIInputActionData Row;
			Row.DisplayName = FText::FromString(Action.Name);
			Row.KeyboardKey = Action.KeyboardKey;
			Row.GamepadKey = Action.GamepadKey;
			OutScene.Table->AddRow(FName(*Action.Name), Row);
		}
		for (FBarAction& Action : OutScene.Actions)
		{
			Action.Counter.Reset(NewObject<UDreamActionCallCounter>());
			FDreamUIActionExecutedDelegate Fire;
			Fire.BindUFunction(Action.Counter.Get(), TEXT("Fire"));
			FDataTableRowHandle Row;
			Row.DataTable = OutScene.Table.Get();
			Row.RowName = FName(*Action.Name);
			Action.Handle = OutScene.Router->RegisterAction(nullptr, Row, Fire, 0, true);
		}

		OutScene.BarWidget = InRig.MakeWidget(TEXT("Bar"), nullptr, FVector2D(400.0, 60.0), InBarPosition);
		OutScene.Bar = OutScene.BarWidget != nullptr ? OutScene.BarWidget->AddComponent<UDreamUIActionBar>() : nullptr;
		FClassProperty* EntryClass = FindFProperty<FClassProperty>(UDreamUIActionBar::StaticClass(), TEXT("EntryClass"));
		if (!InTest.TestNotNull(TEXT("A widget carrying a prompt bar came up"), OutScene.Bar)
			|| !InTest.TestNotNull(TEXT("The bar's entry class is a property"), EntryClass))
		{
			return false;
		}
		// Set the way the details panel sets it: the bar has no setter for it.
		EntryClass->SetObjectPropertyValue_InContainer(OutScene.Bar, UDreamButton::StaticClass());
		OutScene.Bar->Rebuild();

		const TArray<FDreamUIActionBinding>& Prompts = OutScene.Bar->GetPrompts();
		const TArray<UDreamWidget*>& Entries = OutScene.Bar->GetEntryWidgets();
		if (!InTest.TestEqual(TEXT("The bar shows a prompt for each action"), Prompts.Num(), OutScene.Actions.Num())
			|| !InTest.TestEqual(TEXT("...each drawn by an entry"), Entries.Num(), Prompts.Num()))
		{
			return false;
		}
		const double RowStart = -0.5 * (Entries.Num() - 1) * (EntrySize.X + 20.0);
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			UDreamWidget* Entry = Entries[Index];
			Entry->SetWidth(static_cast<float>(EntrySize.X));
			Entry->SetHeight(static_cast<float>(EntrySize.Y));
			Entry->SetAnchoredPosition(FVector2D(RowStart + Index * (EntrySize.X + 20.0), 0.0));
			const FString Shown = Prompts[Index].DisplayName.ToString();
			FBarAction* Action = OutScene.Actions.FindByPredicate([&Shown](const FBarAction& InAction) { return InAction.Name == Shown; });
			if (!InTest.TestNotNull(*FString::Printf(TEXT("Prompt %d shows one of the actions (it shows '%s')"), Index, *Shown), Action))
			{
				return false;
			}
			Action->Entry = Entry;
		}
		InRig.PumpFrames(1);
		return true;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	bool FocusOn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamButton* InButton, const FString& InWhat)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(InWhat, Services != nullptr && InButton != nullptr && Services->FocusForNavigation(InButton->FaceNode, 0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageActionBarDisabledPressTest,
	"DreamGUI.ActionBar.APromptOnADisabledBarRunsNothingForAClickOrATapAndRunsItsActionOnceTheBarIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageActionBarDisabledPressTest, "DreamGUI.ActionBar.APromptOnADisabledBarRunsNothingForAClickOrATapAndRunsItsActionOnceTheBarIsEnabled", "[Pointer][Touch][Disabled]")

/*
 * Two prompts, Confirm and Delete. The bar disabled: a click on Confirm's prompt and a tap on Delete's run nothing. Enabled
 * again, the click runs Confirm once and the tap runs Delete once, and neither runs the other.
 */
bool FDreamCoverageActionBarDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	FBarScene Scene;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !MakeBar(*this, Rig, TArray<FString>{ TEXT("Confirm"), TEXT("Delete") }, FVector2D::ZeroVector, Scene))
	{
		Scene.Unregister();
		return false;
	}
	if (UDreamEventSystem* EventSystem = Rig.EventSystem())
	{
		EventSystem->SetDoubleClickTime(0.0f);
	}
	FBarAction& Confirm = Scene.Actions[0];
	FBarAction& Delete = Scene.Actions[1];
	Scene.BarWidget->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking Confirm's prompt on the disabled bar completes"), Rig.Driver()->Find(FDreamBy::Widget(Confirm.Entry))->Click());
	TestTrue(TEXT("A finger tapped on Delete's prompt completes"), Rig.Driver()->Find(FDreamBy::Widget(Delete.Entry))->Tap(0));
	TestEqual(TEXT("A disabled bar's prompts ran nothing"), Scene.TotalFired(), 0);

	Scene.BarWidget->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking Confirm's prompt once the bar is enabled completes"), Rig.Driver()->Find(FDreamBy::Widget(Confirm.Entry))->Click());
	TestEqual(TEXT("Enabled, the click ran Confirm once"), Confirm.Counter->CallCount, 1);
	TestEqual(TEXT("...and not Delete"), Delete.Counter->CallCount, 0);
	TestTrue(TEXT("A finger tapped on Delete's prompt completes"), Rig.Driver()->Find(FDreamBy::Widget(Delete.Entry))->Tap(1));
	TestEqual(TEXT("The tap ran Delete once"), Delete.Counter->CallCount, 1);
	TestEqual(TEXT("...and not Confirm again"), Confirm.Counter->CallCount, 1);
	Scene.Unregister();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageActionBarScaledPressTest,
	"DreamGUI.ActionBar.OnACanvasScaledToHalfAndToDoubleAClickOrATapOnAPromptRunsTheActionItShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageActionBarScaledPressTest, "DreamGUI.ActionBar.OnACanvasScaledToHalfAndToDoubleAClickOrATapOnAPromptRunsTheActionItShows", "[Pointer][Touch][Scaled]")

/* The two prompts off the canvas's centre: a click on one and a tap on the other each run the action shown, at both scales. */
bool FDreamCoverageActionBarScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		FBarScene Scene;
		if (!ComeUp(*this, Rig, Scale) || !MakeBar(*this, Rig, TArray<FString>{ TEXT("Confirm"), TEXT("Delete") }, FVector2D(40.0, -100.0), Scene))
		{
			Scene.Unregister();
			return false;
		}
		if (UDreamEventSystem* EventSystem = Rig.EventSystem())
		{
			EventSystem->SetDoubleClickTime(0.0f);
		}
		FBarAction& Confirm = Scene.Actions[0];
		FBarAction& Delete = Scene.Actions[1];

		TestTrue(At(Scale, TEXT("Clicking Confirm's prompt completes")), Rig.Driver()->Find(FDreamBy::Widget(Confirm.Entry))->Click());
		TestEqual(*At(Scale, TEXT("The click ran Confirm once")), Confirm.Counter->CallCount, 1);
		TestEqual(*At(Scale, TEXT("...and not Delete")), Delete.Counter->CallCount, 0);
		TestTrue(At(Scale, TEXT("A finger tapped on Delete's prompt completes")), Rig.Driver()->Find(FDreamBy::Widget(Delete.Entry))->Tap(0));
		TestEqual(*At(Scale, TEXT("The tap ran Delete once")), Delete.Counter->CallCount, 1);
		TestEqual(*At(Scale, TEXT("...and not Confirm again")), Confirm.Counter->CallCount, 1);
		Scene.Unregister();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageActionBarDisabledPadTest,
	"DreamGUI.ActionBar.TheDPadPassesOverThePromptOfADisabledBarAndConfirmOnItRunsItsActionOnceTheBarIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageActionBarDisabledPadTest, "DreamGUI.ActionBar.TheDPadPassesOverThePromptOfADisabledBarAndConfirmOnItRunsItsActionOnceTheBarIsEnabled", "[Nav][Disabled]")

/*
 * A button above the bar and one below it, the bar showing one prompt. Disabled, the D-pad down from the upper button passes
 * the prompt over and lands on the lower button: the confirm clicks that one and runs no action. Enabled again, the D-pad
 * down stops on the prompt and the confirm runs its action.
 */
bool FDreamCoverageActionBarDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TStrongObjectPtr<UDreamPressInteractionListener> BelowListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Above = Rig.IsUsable() ? Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, EntrySize, FVector2D(0.0, 120.0)) : nullptr;
	FBarScene Scene;
	if (!TestNotNull(TEXT("The rig and a button above the bar came up"), Above) || !MakeBar(*this, Rig, TArray<FString>{ TEXT("Confirm") }, FVector2D::ZeroVector, Scene))
	{
		Scene.Unregister();
		return false;
	}
	UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, EntrySize, FVector2D(0.0, -120.0));
	if (!TestNotNull(TEXT("A button below the bar came up"), Below))
	{
		Scene.Unregister();
		return false;
	}
	Below->OnClicked.AddDynamic(BelowListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	FBarAction& Confirm = Scene.Actions[0];
	Scene.BarWidget->SetIsEnabled(false);
	Rig.PumpFrames(1);

	if (!FocusOn(*this, Rig, Above, TEXT("Player 0's focus can be put on the upper button")))
	{
		Scene.Unregister();
		return false;
	}
	TestTrue(TEXT("The D-pad down and the pad's confirm complete"),
		Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
	TestEqual(TEXT("The D-pad passed the disabled bar's prompt over: the confirm clicked the lower button"), BelowListener->ClickedCount, 1);
	TestEqual(TEXT("...and ran no action"), Confirm.Counter->CallCount, 0);

	Scene.BarWidget->SetIsEnabled(true);
	Rig.PumpFrames(1);
	if (!FocusOn(*this, Rig, Above, TEXT("Player 0's focus can be put back on the upper button")))
	{
		Scene.Unregister();
		return false;
	}
	TestTrue(TEXT("The D-pad down completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform());
	// On the bar, not on Confirm.Entry: the first D-pad press made the pad the player's device, and the bar built its
	// prompts again for it (UDreamUIActionBar rebuilds when the player switches device, the key it names being the pad's
	// now, as CommonUI's bound action bar rebuilds for a new input method). The prompt is the bar's entry of this moment.
	TestTrue(TEXT("Enabled, the D-pad stopped on the prompt"), IsPartOf(FocusOf(Rig), Scene.BarWidget)
		&& Scene.Bar->GetEntryWidgets().ContainsByPredicate([&Rig](const UDreamWidget* InEntry) { return IsPartOf(FocusOf(Rig), InEntry); }));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
	TestEqual(TEXT("The confirm on the prompt ran its action once"), Confirm.Counter->CallCount, 1);
	TestEqual(TEXT("...and did not click the lower button again"), BelowListener->ClickedCount, 1);
	Scene.Unregister();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageActionBarScaledPadTest,
	"DreamGUI.ActionBar.OnACanvasScaledToHalfAndToDoubleTheDPadStopsOnThePromptBelowAButtonAndConfirmRunsItsAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageActionBarScaledPadTest, "DreamGUI.ActionBar.OnACanvasScaledToHalfAndToDoubleTheDPadStopsOnThePromptBelowAButtonAndConfirmRunsItsAction", "[Nav][Scaled]")

/* The same layout at half and at double: the D-pad down stops on the prompt, nearer than the lower button as drawn. */
bool FDreamCoverageActionBarScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		TStrongObjectPtr<UDreamPressInteractionListener> BelowListener(NewObject<UDreamPressInteractionListener>());
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, EntrySize, FVector2D(0.0, 120.0));
		FBarScene Scene;
		if (!TestNotNull(*At(Scale, TEXT("A button above the bar came up")), Above) || !MakeBar(*this, Rig, TArray<FString>{ TEXT("Confirm") }, FVector2D::ZeroVector, Scene))
		{
			Scene.Unregister();
			return false;
		}
		UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, EntrySize, FVector2D(0.0, -120.0));
		if (!TestNotNull(*At(Scale, TEXT("A button below the bar came up")), Below))
		{
			Scene.Unregister();
			return false;
		}
		Below->OnClicked.AddDynamic(BelowListener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Rig.PumpFrames(1);
		if (!FocusOn(*this, Rig, Above, At(Scale, TEXT("Player 0's focus can be put on the upper button"))))
		{
			Scene.Unregister();
			return false;
		}

		TestTrue(At(Scale, TEXT("The D-pad down and the pad's confirm complete")),
			Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
		TestEqual(*At(Scale, TEXT("The confirm on the prompt the D-pad stopped on ran its action once")), Scene.Actions[0].Counter->CallCount, 1);
		TestEqual(*At(Scale, TEXT("...and the lower button was not reached")), BelowListener->ClickedCount, 0);
		Scene.Unregister();
	}
	return true;
}

#endif
