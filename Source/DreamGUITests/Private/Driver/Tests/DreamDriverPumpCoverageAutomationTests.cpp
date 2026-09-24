// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIManager.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * THE PUMP TICKS WHAT A GAME WOULD TICK.
 *
 * The headless pump names what it ticks, because nothing ticks a rig's world on its own -- and a name
 * that is missing from the list is not an error anywhere, it is a feature that silently never runs.
 * That has happened: the drag-drop subsystem was left off once, and every row drag and every
 * drop-target enter was unobservable until someone noticed. These guards compare the pump's own list
 * against what the plugin declares, so the next tickable world subsystem, or the next tick group the
 * tween helper learns, is a red test with a sentence saying what to do, not a mystery.
 *
 * The last test holds the clock to the engine's rules for a paused and a dilated world, which the rest
 * of the pipeline -- long presses, click runs, tweens -- is timed against.
 */
namespace DreamDriverPumpCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** Whether a class is declared by one of the two runtime modules whose ticking the pump owns. */
	bool IsDeclaredByThePlugin(const UClass* InClass)
	{
		const FString PackageName = InClass->GetOutermost()->GetName();
		return PackageName == TEXT("/Script/DreamGUI") || PackageName == TEXT("/Script/DreamTween");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPumpSubsystemCoverageTest,
	"DreamGUI.Driver.Pump.EveryTickableWorldSubsystemThePluginDeclaresIsTickedByThePump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPumpSubsystemCoverageTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPumpCoverageTestLocal;
	const TArray<UClass*> Pumped = FDreamDriverContext::GetPumpedTickableWorldSubsystems();
	if (!TestTrue(TEXT("The pump ticks at least the UI manager"), Pumped.Contains(UDreamUIManagerWorldSubsystem::StaticClass())))
	{
		return false;
	}

	int32 Declared = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UTickableWorldSubsystem::StaticClass()))
		{
			continue;
		}
		// Abstract bases are never instanced; a class with a newer version is a reinstancing leftover.
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}
		if (!IsDeclaredByThePlugin(Class))
		{
			continue;
		}
		++Declared;
		if (!Pumped.Contains(Class))
		{
			AddError(FString::Printf(
				TEXT("%s is a tickable world subsystem, and the pump does not tick it: add it to PumpOneFrame (FDreamDriverContext::GetPumpedTickableWorldSubsystems), or say there why it should not run on the rig."),
				*Class->GetName()));
		}
	}
	TestTrue(TEXT("The plugin declares tickable world subsystems for the guard to check"), Declared > 0);

	// The other direction: a class on the list has to be one the rig's world actually has, or the
	// pump is ticking a name and the guard above is comparing against a list that means nothing.
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	for (UClass* Class : Pumped)
	{
		if (!TestNotNull(TEXT("Every class on the pump's list is a class"), Class))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s is a tickable world subsystem"), *Class->GetName()),
			Class->IsChildOf(UTickableWorldSubsystem::StaticClass()));
		TestNotNull(FString::Printf(TEXT("The rig's world has a %s for the pump to tick"), *Class->GetName()),
			Rig.GetWorld()->GetSubsystemBase(Class));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPumpTweenGroupCoverageTest,
	"DreamGUI.Driver.Pump.EveryTweenTickGroupTheHelperActorDrivesAdvancesInOnePumpedFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPumpTweenGroupCoverageTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPumpCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UWorld* World = Rig.GetWorld();
	const UEnum* TickTypes = StaticEnum<EDreamTweenTickType>();
	if (!TestNotNull(TEXT("The tween tick types are reflected"), TickTypes))
	{
		return false;
	}

	// One tween per tick type, every one of them a second long and linear, so one pumped frame moves
	// each that is ticked by exactly a sixtieth and leaves each that is not at zero. Read from the
	// enum, not written out: a tick group the helper actor is taught later is a new value here too.
	struct FProbe
	{
		EDreamTweenTickType TickType;
		FString Name;
		TSharedRef<float> Value = MakeShared<float>(0.0f);
	};
	TArray<FProbe> Probes;
	for (int32 Index = 0; Index < TickTypes->NumEnums(); ++Index)
	{
		const FString Name = TickTypes->GetNameStringByIndex(Index);
		if (Name.EndsWith(TEXT("_MAX")))
		{
			continue;
		}
		FProbe& Probe = Probes.AddDefaulted_GetRef();
		Probe.TickType = static_cast<EDreamTweenTickType>(TickTypes->GetValueByIndex(Index));
		Probe.Name = Name;
		const TSharedRef<float> Value = Probe.Value;
		UDreamTweener* Tweener = UDreamTweenManager::To(World,
			FDreamTweenFloatGetterFunction::CreateLambda([Value]() { return *Value; }),
			FDreamTweenFloatSetterFunction::CreateLambda([Value](float InValue) { *Value = InValue; }),
			1.0f, 1.0f);
		if (!TestNotNull(FString::Printf(TEXT("A %s tween can be made"), *Name), Tweener))
		{
			return false;
		}
		Tweener->SetEase(EDreamTweenEase::Linear);
		Tweener->SetTickType(Probe.TickType);
	}
	TestTrue(TEXT("There are tick types to check"), Probes.Num() > 1);

	Rig.PumpFrames(1);

	const float FrameSeconds = Rig.Context().FrameSeconds;
	for (const FProbe& Probe : Probes)
	{
		if (Probe.TickType == EDreamTweenTickType::Manual)
		{
			// Manual means "whoever owns this tween ticks it" (UDreamTweenManager::ManualTick). The
			// helper actor never does, so neither may the pump: a Manual tween that moved on its own
			// would be a pump ticking something a game does not.
			TestEqual(TEXT("A Manual tween is not advanced by the pump, as the helper actor never advances one"),
				*Probe.Value, 0.0f);
			continue;
		}
		TestNearlyEqual(FString::Printf(
			TEXT("A %s tween advanced by one frame: every tick group ADreamTweenTickHelperActor drives is driven by the pump (add a missing one to PumpOneFrame beside the others)"),
			*Probe.Name),
			*Probe.Value, FrameSeconds, 1.0e-4f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPumpClockRulesTest,
	"DreamGUI.Driver.Pump.TheClockFollowsTheEnginesRulesForAPausedAndADilatedWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPumpClockRulesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPumpCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UWorld* World = Rig.GetWorld();
	AWorldSettings* Settings = World->GetWorldSettings(false, false);
	if (!TestNotNull(TEXT("The rig's world has settings"), Settings))
	{
		return false;
	}
	const double Frame = Rig.Context().FrameSeconds;
	constexpr int32 Frames = 12;
	constexpr double Tolerance = 1.0e-5;

	// Dilated: game time runs at the dilation, real time does not. What UWorld::Tick does with
	// GetEffectiveTimeDilation before anything ticks.
	Settings->TimeDilation = 0.5f;
	{
		const double Game = World->GetTimeSeconds();
		const double Real = World->GetRealTimeSeconds();
		const double Unpaused = World->GetUnpausedTimeSeconds();
		Rig.PumpFrames(Frames);
		TestEqual(TEXT("Dilated by a half, the game clock advances half as fast"), World->GetTimeSeconds() - Game, Frames * Frame * 0.5, Tolerance);
		TestEqual(TEXT("And the unpaused clock with it"), World->GetUnpausedTimeSeconds() - Unpaused, Frames * Frame * 0.5, Tolerance);
		TestEqual(TEXT("Real time is not dilated"), World->GetRealTimeSeconds() - Real, Frames * Frame, Tolerance);
		TestEqual(TEXT("The game delta is the dilated one"), (double)World->GetDeltaSeconds(), Frame * 0.5, Tolerance);
		TestEqual(TEXT("The real delta is the frame"), (double)World->DeltaRealTimeSeconds, Frame, Tolerance);
	}
	Settings->TimeDilation = 1.0f;

	// Paused: the game clock stops, the unpaused and real ones go on. What IsPaused does to
	// UWorld::Tick's "Update time" block.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags |= RF_Transient;
	APlayerState* Pauser = World->SpawnActor<APlayerState>(SpawnParameters);
	if (!TestNotNull(TEXT("A player state can be spawned to pause with"), Pauser))
	{
		return false;
	}
	Settings->SetPauserPlayerState(Pauser);
	if (!TestTrue(TEXT("The world is paused"), World->IsPaused()))
	{
		return false;
	}
	{
		const double Game = World->GetTimeSeconds();
		const double Real = World->GetRealTimeSeconds();
		const double Unpaused = World->GetUnpausedTimeSeconds();
		Rig.PumpFrames(Frames);
		TestEqual(TEXT("Paused, the game clock stands still"), World->GetTimeSeconds() - Game, 0.0, Tolerance);
		TestEqual(TEXT("Paused, the unpaused clock goes on"), World->GetUnpausedTimeSeconds() - Unpaused, Frames * Frame, Tolerance);
		TestEqual(TEXT("Paused, real time goes on"), World->GetRealTimeSeconds() - Real, Frames * Frame, Tolerance);
	}
	Settings->SetPauserPlayerState(nullptr);
	TestFalse(TEXT("And the world can be unpaused again"), World->IsPaused());
	return true;
}

#endif
