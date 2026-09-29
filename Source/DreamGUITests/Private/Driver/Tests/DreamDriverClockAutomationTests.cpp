// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/UITextInput.h"
#include "UObject/UnrealType.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "DreamPointerEventTestTypes.h"

/*
 * ONE CLOCK FOR EVERY GESTURE THAT IS TIMED.
 *
 * A text field opens its edit menu when a press on it is held -- touch's right click. It used to time
 * that hold on the wall clock (FPlatformTime::Seconds) while everything else about a pointer is timed
 * on the world's: the pointer module's own long press, a click run, navigation repeat. Nothing that
 * advances a world frame by frame -- this driver, a replay, a fixed-step capture -- could make the
 * hold mature, and the field ignored the world's pause and dilation rules entirely.
 *
 * The field now reads the world's REAL time, because an edit menu has to open in a paused game's
 * menus too (Slate times its UI in real time for the same reason). The last two tests hold the pointer
 * module to the same standard -- a paused game's menus are exactly where a long press and a double
 * click still have to mean what they mean. It times both on UDreamEventSystem::GetPointerClockSeconds,
 * the world's real time, which a pause does not stop. Timed on GetTimeSeconds, as it used to be, a
 * hold in a paused game never matured and any two clicks on one widget were a double click.
 */
namespace DreamDriverClockTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/**
	 * The field's hold threshold, read off the behaviour. It is a protected property with no getter,
	 * so it is read the way the details panel reads it -- by reflection -- rather than restated here.
	 */
	TOptional<float> ReadContextMenuLongPressTime(const UUITextInput* InBehaviour)
	{
		const FFloatProperty* Property = FindFProperty<FFloatProperty>(UUITextInput::StaticClass(), TEXT("ContextMenuLongPressTime"));
		if (Property == nullptr || InBehaviour == nullptr)
		{
			return TOptional<float>();
		}
		return Property->GetPropertyValue_InContainer(InBehaviour);
	}

	/**
	 * Pause the world the way a game's pause leaves it: a pauser recorded on the world settings, which
	 * is what AGameModeBase::SetPause writes and what UWorld::IsPaused asks. A rig has no game mode to
	 * go through, and none is needed to be in the state.
	 */
	bool PauseWorld(UWorld* InWorld)
	{
		AWorldSettings* Settings = InWorld != nullptr ? InWorld->GetWorldSettings(false, false) : nullptr;
		if (Settings == nullptr)
		{
			return false;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		APlayerState* Pauser = InWorld->SpawnActor<APlayerState>(SpawnParameters);
		if (Pauser == nullptr)
		{
			return false;
		}
		Settings->SetPauserPlayerState(Pauser);
		return InWorld->IsPaused();
	}

	/** A text field on the rig with text in it and an edit in progress, so its menu has something to offer (Select All at least). */
	UDreamTextInput* MakeFieldBeingEdited(FAutomationTestBase& InTest, FDreamDriverRig& InRig, FDreamElementRef& OutElement)
	{
		UDreamTextInput* Field = InRig.MakeControl<UDreamTextInput>(TEXT("Notes"), nullptr, FVector2D(320.0, 40.0));
		if (Field == nullptr || Field->InputBehaviour == nullptr)
		{
			return nullptr;
		}
		InRig.PumpFrames(1);
		OutElement = InRig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
		// Typed text, because ShowContextMenu offers only what can act and opens nothing when nothing
		// can: with text in the field, Select All always can.
		InTest.TestTrue(TEXT("Clicking into the field and typing completes"), OutElement->Type(TEXT("abc")));
		InTest.TestTrue(TEXT("The field is being edited"), Field->InputBehaviour->IsInputActive());
		// Past the double-click time before anything else presses the field. The next press would
		// otherwise continue the click run the typing's click began, and the second press of a run is
		// delivered as a double click INSTEAD of a down -- which never starts a hold at all.
		const float DoubleClickTime = InRig.EventSystem() != nullptr ? InRig.EventSystem()->GetDoubleClickTime() : 0.0f;
		InTest.TestTrue(TEXT("Waiting out the double-click time completes"),
			InRig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform());
		return Field;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverClockFieldHoldTest,
	"DreamGUI.Driver.Clock.HoldingAPressOnAFieldBeingEditedOpensItsMenuAtTheHoldTimeAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverClockFieldHoldTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverClockTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	UDreamTextInput* Field = MakeFieldBeingEdited(*this, Rig, FieldElement);
	if (!TestNotNull(TEXT("A text field can be made and edited"), Field))
	{
		return false;
	}
	UUITextInput* Behaviour = Field->InputBehaviour;
	const TOptional<float> HoldTime = ReadContextMenuLongPressTime(Behaviour);
	if (!TestTrue(TEXT("The field's hold threshold can be read"), HoldTime.IsSet())
		|| !TestTrue(TEXT("The field offers a context menu at all"), Behaviour->GetAllowContextMenu())
		|| !TestTrue(TEXT("The hold threshold is longer than a couple of frames"), HoldTime.GetValue() > 3.0f * Rig.Context().FrameSeconds))
	{
		return false;
	}
	TestFalse(TEXT("No menu is open to begin with"), Behaviour->IsContextMenuOpen());

	// Released well before the threshold: an ordinary press, which must not open anything however much
	// time passes afterwards.
	TestTrue(TEXT("A short press completes"), FieldElement->LongPress(HoldTime.GetValue() * 0.4f));
	TestTrue(TEXT("Time passing after the release completes"), Rig.Driver()->Sequence().WaitSeconds(HoldTime.GetValue()).Perform());
	TestFalse(TEXT("A press released before the hold time opens no menu"), Behaviour->IsContextMenuOpen());

	// Held: short of the threshold nothing yet, past it the menu. On the wall clock the second half
	// never happens inside a test -- a few hundred pumped frames take milliseconds.
	TestTrue(TEXT("Holding the press completes"), FieldElement->Hold(HoldTime.GetValue() * 0.5f));
	TestFalse(TEXT("Half way through the hold there is no menu yet"), Behaviour->IsContextMenuOpen());
	TestTrue(TEXT("Holding on past the threshold completes"),
		Rig.Driver()->Sequence().WaitSeconds(HoldTime.GetValue() * 0.5f + 2.0f * Rig.Context().FrameSeconds).Perform());
	TestTrue(TEXT("Held past the hold time, the field opens its context menu"), Behaviour->IsContextMenuOpen());

	TestTrue(TEXT("Letting go completes"), Rig.Driver()->Sequence().Release().Perform());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverClockFieldHoldPausedTest,
	"DreamGUI.Driver.Clock.AFieldsHeldPressStillOpensItsMenuWhileTheGameIsPaused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverClockFieldHoldPausedTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverClockTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	FDreamElementRef FieldElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Notes")));
	UDreamTextInput* Field = MakeFieldBeingEdited(*this, Rig, FieldElement);
	if (!TestNotNull(TEXT("A text field can be made and edited"), Field))
	{
		return false;
	}
	UUITextInput* Behaviour = Field->InputBehaviour;
	const TOptional<float> HoldTime = ReadContextMenuLongPressTime(Behaviour);
	if (!TestTrue(TEXT("The field's hold threshold can be read"), HoldTime.IsSet()))
	{
		return false;
	}

	UWorld* World = Rig.GetWorld();
	if (!TestTrue(TEXT("The world can be paused"), PauseWorld(World)))
	{
		return false;
	}
	const double GameTimeAtPause = World->GetTimeSeconds();
	const double RealTimeAtPause = World->GetRealTimeSeconds();

	TestTrue(TEXT("Holding the press past the threshold completes"),
		FieldElement->Hold(HoldTime.GetValue() + 3.0f * Rig.Context().FrameSeconds));
	// The pause is real: the game clock stood still, the real one did not. Without this the test
	// below would pass for the wrong reason on a pump that ignored the pause.
	TestEqual(TEXT("While paused, the game clock does not move"), World->GetTimeSeconds(), GameTimeAtPause);
	TestTrue(TEXT("While paused, real time goes on"), World->GetRealTimeSeconds() > RealTimeAtPause + HoldTime.GetValue());
	TestTrue(TEXT("A paused game's text field still opens its menu on a held press"), Behaviour->IsContextMenuOpen());

	TestTrue(TEXT("Letting go completes"), Rig.Driver()->Sequence().Release().Perform());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverClockPointerLongPressTest,
	"DreamGUI.Driver.Clock.APointerLongPressFiresOnceTheHoldReachesTheLongPressTimeAndNotBefore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverClockPointerLongPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverClockTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Pad = Rig.MakeWidget(TEXT("Pad"), nullptr, FVector2D(200.0, 120.0));
	UDreamLongPressCounter* Counter = Pad != nullptr ? Pad->AddComponent<UDreamLongPressCounter>() : nullptr;
	if (!TestNotNull(TEXT("A widget that counts long presses can be made"), Counter))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const float LongPressTime = Rig.EventSystem()->GetLongPressTime();
	const float DoubleClickTime = Rig.EventSystem()->GetDoubleClickTime();
	if (!TestTrue(TEXT("The event system has a long press time"), LongPressTime > 0.0f))
	{
		return false;
	}

	// The baseline the paused variant below is measured against: on a running game's clock the
	// pointer module does report a held press, and only once it has been held long enough.
	FDreamElementRef PadElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Pad")));
	TestTrue(TEXT("A press held for half the long press time completes"), PadElement->LongPress(LongPressTime * 0.5f));
	TestEqual(TEXT("Released before the long press time, it is no long press"), Counter->LongPressCount, 0);
	// Out of the double-click window, so the next press is a press and not the second half of a pair.
	TestTrue(TEXT("Waiting out the double-click time completes"),
		Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform());
	TestTrue(TEXT("A press held past the long press time completes"), PadElement->LongPress(LongPressTime + 0.1f));
	TestEqual(TEXT("Held past the long press time, it is exactly one long press"), Counter->LongPressCount, 1);
	TestEqual(TEXT("Carried by the mouse's pointer"), Counter->LastPointerID, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverClockDoubleClickIntervalTest,
	"DreamGUI.Driver.Clock.TwoClicksInsideTheDoubleClickTimeAreADoubleClickAndTwoOutsideItAreNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverClockDoubleClickIntervalTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverClockTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Pad = Rig.MakeWidget(TEXT("Pad"), nullptr, FVector2D(200.0, 120.0));
	UDreamDoubleClickCounter* Counter = Pad != nullptr ? Pad->AddComponent<UDreamDoubleClickCounter>() : nullptr;
	if (!TestNotNull(TEXT("A widget that counts double clicks can be made"), Counter))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const float DoubleClickTime = Rig.EventSystem()->GetDoubleClickTime();
	const float FrameSeconds = Rig.Context().FrameSeconds;
	if (!TestTrue(TEXT("The event system has a double-click time of several frames"), DoubleClickTime > 4.0f * FrameSeconds))
	{
		return false;
	}
	FDreamElementRef PadElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Pad")));

	// No frames between: exactly the default DoubleClick, well inside the window.
	TestTrue(TEXT("A quick pair of clicks completes"), PadElement->DoubleClick(EDreamUIMouseButtonType::Left, 0));
	TestEqual(TEXT("Two clicks inside the double-click time are one double click"), Counter->DoubleClickCount, 1);

	// Out of the window before the next pair, so the pair below starts a run of its own.
	TestTrue(TEXT("Waiting out the double-click time completes"),
		Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime + 0.1f).Perform());

	// The same pair with the gap stretched past the window: two single clicks.
	const int32 FramesOutsideTheWindow = FMath::CeilToInt(DoubleClickTime / FrameSeconds) + 2;
	TestTrue(TEXT("A slow pair of clicks completes"), PadElement->DoubleClick(EDreamUIMouseButtonType::Left, FramesOutsideTheWindow));
	TestEqual(TEXT("Two clicks further apart than the double-click time are not a double click"), Counter->DoubleClickCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverClockPointerLongPressPausedTest,
	"DreamGUI.Driver.Clock.APointerLongPressStillFiresWhileTheGameIsPaused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverClockPointerLongPressPausedTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverClockTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Pad = Rig.MakeWidget(TEXT("Pad"), nullptr, FVector2D(200.0, 120.0));
	UDreamLongPressCounter* Counter = Pad != nullptr ? Pad->AddComponent<UDreamLongPressCounter>() : nullptr;
	if (!TestNotNull(TEXT("A widget that counts long presses can be made"), Counter))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const float LongPressTime = Rig.EventSystem()->GetLongPressTime();
	if (!TestTrue(TEXT("The event system has a long press time"), LongPressTime > 0.0f)
		|| !TestTrue(TEXT("The world can be paused"), PauseWorld(Rig.GetWorld())))
	{
		return false;
	}

	FDreamElementRef PadElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Pad")));
	TestTrue(TEXT("A press held well past the long press time completes"),
		PadElement->LongPress(LongPressTime + 0.2f));
	// The UI keeps working while the game is paused -- that is what the event system's
	// bTickEvenWhenPaused and the UI manager's IsTickableWhenPaused are for -- so a held press in a
	// pause menu has to be the long press it is anywhere else. The pointer module times it on the
	// pointer clock, the world's real time; on GetTimeSeconds, which a pause stops, it never matured.
	TestEqual(TEXT("A paused game still reports a long press once the hold has lasted long enough"),
		Counter->LongPressCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverClockDoubleClickPausedTest,
	"DreamGUI.Driver.Clock.TwoClicksASecondApartAreNotADoubleClickWhileTheGameIsPaused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverClockDoubleClickPausedTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverClockTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Pad = Rig.MakeWidget(TEXT("Pad"), nullptr, FVector2D(200.0, 120.0));
	UDreamDoubleClickCounter* Counter = Pad != nullptr ? Pad->AddComponent<UDreamDoubleClickCounter>() : nullptr;
	if (!TestNotNull(TEXT("A widget that counts double clicks can be made"), Counter))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const float DoubleClickTime = Rig.EventSystem()->GetDoubleClickTime();
	if (!TestTrue(TEXT("The event system has a double-click time"), DoubleClickTime > 0.0f)
		|| !TestTrue(TEXT("The world can be paused"), PauseWorld(Rig.GetWorld())))
	{
		return false;
	}

	FDreamElementRef PadElement = Rig.Driver()->Find(FDreamBy::Name(TEXT("Pad")));
	TestTrue(TEXT("The first click completes"), PadElement->Click());
	TestTrue(TEXT("Waiting several double-click times completes"),
		Rig.Driver()->Sequence().WaitSeconds(DoubleClickTime * 3.0f + 0.1f).Perform());
	TestTrue(TEXT("The second click completes"), PadElement->Click());
	// Seen from the player, a second apart is two clicks whether the game is paused or not. The pointer
	// module measures the gap on the pointer clock, the world's real time. Measured on GetTimeSeconds,
	// which a pause freezes, every pair of clicks on one widget in a pause menu was a gap of zero -- a
	// double click, however slowly it was made.
	TestEqual(TEXT("Two clicks well over the double-click time apart are not a double click, paused or not"),
		Counter->DoubleClickCount, 0);
	return true;
}

#endif
