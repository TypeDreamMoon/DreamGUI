// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWorldWidgetActor.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DreamOnDiskFixture.h"
#include "Engine/Level.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPtr.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * THE SCREEN'S INPUT, WHILE A LEVEL STREAMS IN AND OUT UNDER IT.
 *
 * A level a game streams in brings its own world-space panels, and a panel is a reason for the player to have a world
 * pointer: a world widget component that begins play asks the input for one (UDreamUIInputSubsystem::
 * EnsureInteractionForPlayer), so streaming the level in adds a raycaster to the player's, and streaming it out takes the
 * panel's tree away from under it (DreamGUI.Pie.ALevelStreamedIntoAPlaySessionAndOutAgainTakesItsTreesWithIt). In UMG a
 * level's widget components come and go without touching the viewport's widgets, which go on being clicked through
 * Slate. So the button on the player's screen is clicked once before the level comes, once while it is in, and once
 * after it has gone -- three clicks for three, and none lost or doubled on the way.
 */
namespace DreamLifecycleStreamingInteractionTestLocal
{
	FWaitTimeout ConditionLimit()
	{
		return FWaitTimeout::InSeconds(2.0);
	}

	FWaitTimeout StepLimit()
	{
		return FWaitTimeout::InSeconds(3.0);
	}

	/** A level with one panel drawn by DreamUI's renderer, made as the level editor's New Level makes one and saved to disk. */
	bool SaveLevelWithAPanel(DreamOnDiskFixture::FScopedOnDiskPackage& InMap, UClass* InPanelClass, FString& OutError)
	{
		UWorld::InitializationValues Values;
		Values.ShouldSimulatePhysics(false).EnableTraceCollision(false).CreateNavigation(false).CreateAISystem(false).AllowAudioPlayback(false);
		UWorld* Level = UWorld::CreateWorld(EWorldType::Inactive, /*bInformEngineOfWorld*/ false, FName(*InMap.AssetName), InMap.Package,
			/*bAddToRoot*/ false, ERHIFeatureLevel::Num, &Values);
		if (Level == nullptr)
		{
			OutError = TEXT("no world was made for the level");
			return false;
		}
		ADreamWorldWidgetActor* Panel = Level->SpawnActor<ADreamWorldWidgetActor>(FVector(400.0, 0.0, 150.0), FRotator::ZeroRotator);
		bool bSaved = false;
		if (Panel != nullptr && Panel->GetWidgetComponent() != nullptr)
		{
			Panel->GetWidgetComponent()->SetWidgetClass(InPanelClass);
			Panel->GetWidgetComponent()->SetBackend(EDreamWorldWidgetBackend::DreamUIRenderer);
			bSaved = InMap.Save(Level, OutError);
		}
		else
		{
			OutError = TEXT("the panel was not placed in the level");
		}
		Level->DestroyWorld(/*bInformEngineOfWorld*/ false);
		Level->ClearFlags(RF_Standalone);
		return bSaved;
	}

	/** What the steps keep of the play world: weakly. */
	struct FStreamed
	{
		TWeakObjectPtr<ULevelStreamingDynamic> Streaming;
		TWeakObjectPtr<ULevel> Level;
	};

	/** A click on the screen's button through the player controller, and a wait for it to have been heard InCount times in all. */
	void AddClickOnPlay(FDreamDriverSequence& InSteps, const FDreamLocatorRef& InPlay,
		const TStrongObjectPtr<UDreamPressInteractionListener>& InListener, int32 InCount, const TCHAR* InWhen)
	{
		const TStrongObjectPtr<UDreamPressInteractionListener> Listener = InListener;
		InSteps.Click(InPlay)
			.Wait(FDreamUntil::Condition([Listener, InCount]() { return Listener->ClickedCount >= InCount; }, ConditionLimit()), StepLimit(), InWhen);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleStreamingInteractionPieTest,
	"DreamGUI.Pie.TheScreensButtonIsClickedOnceBeforeWhileAndAfterALevelWithAPanelIsStreamedInAndOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleStreamingInteractionPieTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleStreamingInteractionTestLocal;
	using DreamTests::Lifecycle::FScopedPanelClass;
	const TSharedRef<FScopedPanelClass> Panel = MakeShared<FScopedPanelClass>(TEXT("StreamedPanelInteractPie"), /*bInSavedToDisk*/ true);
	const TSharedRef<DreamOnDiskFixture::FScopedOnDiskPackage> Map = MakeShared<DreamOnDiskFixture::FScopedOnDiskPackage>(
		TEXT("StreamedPanelsInteractPie"), /*bInIsMap*/ true);
	if (!TestNotNull(TEXT("The panel class compiled"), Panel->GetClass()))
	{
		return false;
	}
	FString Error;
	const bool bSaved = SaveLevelWithAPanel(*Map, Panel->GetClass(), Error);
	if (!TestTrue(FString::Printf(TEXT("The level was saved to disk (%s)"), *Error), bSaved))
	{
		return false;
	}
	const TSoftObjectPtr<UWorld> LevelAsset{ FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *Map->PackageName, *Map->AssetName)) };
	const TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	const TSharedRef<FStreamed> Streamed = MakeShared<FStreamed>();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([Listener](FDreamDriverPieRig& InRig)
	{
		if (UDreamButton* Button = InRig.MakeControl<UDreamButton>(TEXT("Play"), nullptr, FVector2D(200.0, 60.0)))
		{
			Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
		}
	});
	const FDreamLocatorRef Play = Rig->Made(TEXT("Play"));
	FDreamDriverSequence Steps = Rig->Sequence();
	AddClickOnPlay(Steps, Play, Listener, 1, TEXT("the click before the level comes"));
	Steps.Then([this, Streamed, LevelAsset](FDreamDriverContext& InContext)
	{
		bool bAsked = false;
		ULevelStreamingDynamic* Streaming = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(InContext.World, LevelAsset,
			FTransform::Identity, bAsked);
		if (!TestTrue(TEXT("An instance of the level was asked for"), bAsked && Streaming != nullptr))
		{
			return;
		}
		InContext.World->FlushLevelStreaming();
		ULevel* Level = Streaming->GetLoadedLevel();
		TestTrue(TEXT("...streamed in and shown"), Level != nullptr && Level->bIsVisible);
		Streamed->Streaming = Streaming;
		Streamed->Level = Level;
	});
	Steps.WaitFrames(3);
	AddClickOnPlay(Steps, Play, Listener, 2, TEXT("the click while the level is in"));
	Steps.Then([this, Streamed](FDreamDriverContext& InContext)
	{
		if (ULevelStreamingDynamic* Streaming = Streamed->Streaming.Get())
		{
			Streaming->SetIsRequestingUnloadAndRemoval(true);
		}
		InContext.World->FlushLevelStreaming();
		TestFalse(TEXT("The level streamed out is out of the world"), Streamed->Level.IsValid() && Streamed->Level->bIsVisible);
	});
	// A level streamed out is made ready for the collector as a world tick ends.
	Steps.WaitFrames(2);
	AddClickOnPlay(Steps, Play, Listener, 3, TEXT("the click after the level has gone"));
	// The class and the map on disk are held to the end, by the step that ends it.
	Steps.Then([this, Listener, Panel, Map](FDreamDriverContext&)
	{
		TestEqual(TEXT("Three clicks on the screen's button were three clicks of it"), Listener->ClickedCount, 3);
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
