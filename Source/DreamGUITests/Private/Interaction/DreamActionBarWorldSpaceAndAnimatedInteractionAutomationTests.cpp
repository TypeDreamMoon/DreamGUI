// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIActionTypes.h"
#include "Engine/DataTable.h"
#include "Event/DreamEventSystem.h"
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
#include "Driver/DreamDriverWorldSpace.h"
#include "DreamNavigationTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * THE PROMPT BAR'S PROMPTS AS BUTTONS, AS SHIPPED AND ON A PANEL IN THE LEVEL.
 *
 * CommonUI's bound action bar builds one UCommonBoundActionButton per prompt, and a bound action button is a button: clicking
 * it, tapping it or confirming it with the pad runs the action it shows (UCommonBoundActionButton::NativeOnClicked executes
 * the binding's OnExecuteAction, CommonUI/Private/Input/CommonBoundActionButton.cpp:37-45). DreamGUI's bar does the same for
 * an entry with a button in it (UDreamUIActionBar: "A prompt is also a button", through UDreamUIActionRouter::ExecuteAction).
 * The D-pad stops on a prompt like on any button, and its confirm presses it. In a widget component the click and the tap
 * come through the interaction's ray, and DreamGUI's D-pad on a panel is its directional scan among the world-space widgets:
 * the prompt runs its action either way.
 *
 * The bar here is built as a project builds one: the prompt entries are buttons (UDreamButton as the bar's entry class), set
 * in a row by the test once the bar has made them, and the actions are registered with the router for every screen.
 */
namespace DreamActionBarWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	const FVector2D EntrySize(160.0, 40.0);

	struct FBarAction
	{
		FString Name;
		FKey KeyboardKey;
		FKey GamepadKey;
		TStrongObjectPtr<UDreamActionCallCounter> Counter;
		FDreamUIActionHandle Handle;
		UDreamWidget* Entry = nullptr;
	};

	struct FBarScene
	{
		TStrongObjectPtr<UDataTable> Table;
		TArray<FBarAction> Actions;
		UDreamWidget* BarWidget = nullptr;
		UDreamUIActionBar* Bar = nullptr;
		UDreamUIActionRouter* Router = nullptr;

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

		~FBarScene()
		{
			Unregister();
			Router = nullptr;
		}
	};

	/**
	 * InNames as actions registered for every screen, each with a counter; a bar on a widget under InParent (the rig's root
	 * when null) at InBarPosition, whose entries are buttons, built from the router; each entry set in a row in the bar and
	 * matched to its action by the prompt it shows. False, having said why, when any of it could not be made.
	 */
	bool MakeBar(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InParent, const TArray<FString>& InNames,
		const FVector2D& InBarPosition, FBarScene& OutScene)
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

		OutScene.BarWidget = InRig.MakeWidget(TEXT("Bar"), InParent, FVector2D(400.0, 60.0), InBarPosition);
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
		InRig.PumpFrames(2);
		return true;
	}

	/** Two prompts, Confirm and Delete, under InParent: a click on Confirm's runs Confirm once, a tap on Delete's runs Delete once. */
	bool RunPressPrompts(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		FBarScene Scene;
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Parent = nullptr;
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, Rig);
			if (!Stage.IsReady())
			{
				return false;
			}
			Parent = Stage.Panel;
		}
		if (!MakeBar(InTest, Rig, Parent, TArray<FString>{ TEXT("Confirm"), TEXT("Delete") }, FVector2D(0.0, -60.0), Scene))
		{
			return false;
		}
		if (UDreamEventSystem* EventSystem = Rig.EventSystem())
		{
			EventSystem->SetDoubleClickTime(0.0f);
		}
		FBarAction& Confirm = Scene.Actions[0];
		FBarAction& Delete = Scene.Actions[1];
		if (!InTest.TestTrue(TEXT("Both prompts are somewhere the pointer can reach"), PixelOf(Rig, Confirm.Entry).IsSet() && PixelOf(Rig, Delete.Entry).IsSet()))
		{
			return false;
		}

		InTest.TestTrue(TEXT("Clicking Confirm's prompt completes"), Rig.Driver()->Find(FDreamBy::Widget(Confirm.Entry))->Click());
		InTest.TestEqual(TEXT("The click ran Confirm once"), Confirm.Counter->CallCount, 1);
		InTest.TestEqual(TEXT("...and not Delete"), Delete.Counter->CallCount, 0);
		InTest.TestTrue(TEXT("A finger tapped on Delete's prompt completes"), Rig.Driver()->Find(FDreamBy::Widget(Delete.Entry))->Tap(0));
		InTest.TestEqual(TEXT("The tap ran Delete once"), Delete.Counter->CallCount, 1);
		InTest.TestEqual(TEXT("...and not Confirm again"), Confirm.Counter->CallCount, 1);
		return true;
	}

	/**
	 * A button above the bar and one below it, the bar showing one prompt, all under InParent. With the focus on the upper
	 * button, the D-pad down stops on the prompt -- nearer than the lower button -- and the pad's confirm runs its action once;
	 * the lower button is not reached.
	 */
	bool RunPadOntoPrompt(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		TStrongObjectPtr<UDreamPressInteractionListener> BelowListener(NewObject<UDreamPressInteractionListener>());
		FBarScene Scene;
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Parent = nullptr;
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, Rig);
			if (!Stage.IsReady())
			{
				return false;
			}
			Parent = Stage.Panel;
		}
		UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), Parent, EntrySize, FVector2D(0.0, 110.0));
		if (!InTest.TestTrue(TEXT("A button above the bar came up"), Above != nullptr && Above->FaceNode != nullptr)
			|| !MakeBar(InTest, Rig, Parent, TArray<FString>{ TEXT("Confirm") }, FVector2D::ZeroVector, Scene))
		{
			return false;
		}
		UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), Parent, EntrySize, FVector2D(0.0, -120.0));
		if (!InTest.TestNotNull(TEXT("A button below the bar came up"), Below))
		{
			return false;
		}
		Below->OnClicked.AddDynamic(BelowListener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("Player 0's focus can be put on the upper button"), PutFocusOn(Rig, Above->FaceNode)))
		{
			return false;
		}

		InTest.TestTrue(TEXT("The D-pad down and the pad's confirm complete"),
			Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
		InTest.TestEqual(TEXT("The confirm on the prompt the D-pad stopped on ran its action once"), Scene.Actions[0].Counter->CallCount, 1);
		InTest.TestEqual(TEXT("...and the lower button was not reached"), BelowListener->ClickedCount, 0);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedActionBarPressTest,
	"DreamGUI.ActionBar.AClickOnAPromptRunsTheActionItShowsAndATapOnAnotherRunsThatOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedActionBarPressTest, "DreamGUI.ActionBar.AClickOnAPromptRunsTheActionItShowsAndATapOnAnotherRunsThatOne", "[Pointer][Touch][Animated]")

/* As shipped on the screen: each prompt is a button, and pressing it runs the action it shows and no other. */
bool FDreamAnimatedActionBarPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarWorldTestLocal;
	return RunPressPrompts(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedActionBarPadTest,
	"DreamGUI.ActionBar.TheDPadStopsOnThePromptBelowAButtonAndThePadsConfirmRunsItsAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedActionBarPadTest, "DreamGUI.ActionBar.TheDPadStopsOnThePromptBelowAButtonAndThePadsConfirmRunsItsAction", "[Nav][Animated]")

bool FDreamAnimatedActionBarPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarWorldTestLocal;
	return RunPadOntoPrompt(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldActionBarPressTest,
	"DreamGUI.ActionBar.OnAWorldPanelAClickOrATapOnAPromptRunsTheActionItShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldActionBarPressTest, "DreamGUI.ActionBar.OnAWorldPanelAClickOrATapOnAPromptRunsTheActionItShows", "[Pointer][Touch][World]")

/* The bar on a panel: the click and the tap reach the prompts through the world pointer, and each runs what it shows. */
bool FDreamWorldActionBarPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarWorldTestLocal;
	return RunPressPrompts(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldActionBarPadTest,
	"DreamGUI.ActionBar.OnAWorldPanelTheDPadStopsOnThePromptBelowAButtonAndConfirmRunsItsAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldActionBarPadTest, "DreamGUI.ActionBar.OnAWorldPanelTheDPadStopsOnThePromptBelowAButtonAndConfirmRunsItsAction", "[Nav][World]")

bool FDreamWorldActionBarPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamActionBarWorldTestLocal;
	return RunPadOntoPrompt(*this, true);
}

#endif
