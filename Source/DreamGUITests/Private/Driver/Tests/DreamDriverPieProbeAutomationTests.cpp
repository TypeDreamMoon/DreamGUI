// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Extensions/DreamGameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "IAssetViewport.h"
#include "Interaction/UITextInput.h"
#include "LevelEditor.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Rendering/RenderingCommon.h"
#include "RenderingThread.h"
#include "RHIGlobals.h"
#include "Slate/SceneViewport.h"
#include "UnrealClient.h"
#include "Utils/DreamUIUtils.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "RHI/DreamPixelProbe.h"

/*
 * WHAT THE PLAY-IN-EDITOR LAYER CAN BE USED FOR, MEASURED.
 *
 * These are probes first and tests second. Whether a play session comes up at all in a headless
 * editor, how long it takes, what size its viewport is, and whether its picture can be read back
 * decide how much of the interaction suite can lean on this layer -- and none of it can be settled by
 * reading source, because it depends on the run: -nullrhi or a real RHI, a window or none, the
 * machine. So each probe prints its numbers with AddInfo, and asserts only the claims this plugin
 * relies on: that the session starts, that its viewport client is the one the rig asked for, that the
 * canvas sees the session's own viewport, and that stopping it leaves the process as it was found.
 *
 * Every session here starts on a blank map made for it (see FDreamDriverPieRig::Start), never on
 * the project's startup map.
 */
namespace DreamDriverPieProbeLocal
{
	/**
	 * Queue one step that runs whatever state the rig is in. Straight into the framework's queue: the
	 * ADD_LATENT_AUTOMATION_COMMAND macro would cut a lambda's capture list at its first comma.
	 */
	void EnqueueCheck(TFunction<void()> InCheck)
	{
		TFunction<bool()> Step = [InCheck]() -> bool
		{
			InCheck();
			return true;
		};
		FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>(MoveTemp(Step)));
	}

	FString DescribeSize(const FIntPoint& InSize)
	{
		return FString::Printf(TEXT("%dx%d"), InSize.X, InSize.Y);
	}

	const TCHAR* DescribeDestination(EDreamPieViewportDestination InDestination)
	{
		return InDestination == EDreamPieViewportDestination::LevelEditorViewport
			? TEXT("the level editor's viewport")
			: TEXT("a window of its own");
	}

	/** The engine's world contexts for play worlds. Zero whenever no session is running. */
	int32 CountPlayWorldContexts()
	{
		int32 PlayContexts = 0;
		if (GEngine != nullptr)
		{
			for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
			{
				if (WorldContext.WorldType == EWorldType::PIE)
				{
					++PlayContexts;
				}
			}
		}
		return PlayContexts;
	}

	/**
	 * Per colour channel, for the read-back probe. Pure red on black survives any transfer curve --
	 * 0 and 1 are fixed points of all of them -- so this covers rounding and nothing else.
	 */
	constexpr int32 ReadBackChannelTolerance = 12;

	/**
	 * The size of the level editor viewport sessions play in. Ending a session hands the session
	 * viewport's size back to it (SLevelViewport::EndPlayInEditorSession, through
	 * FSceneViewport::OnPlayWorldViewportSwapped) and the next session starts from whatever it holds, so a
	 * size one session was given and nobody took back shows up here. (-1, -1) when the editor has no level
	 * viewport.
	 */
	FIntPoint CurrentLevelViewportSize()
	{
		if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
		{
			const TSharedPtr<IAssetViewport> LevelViewport = LevelEditor->GetFirstActiveViewport();
			if (const FViewport* Viewport = LevelViewport.IsValid() ? LevelViewport->GetActiveViewport() : nullptr)
			{
				return Viewport->GetSizeXY();
			}
		}
		return FIntPoint(-1, -1);
	}

	/** What the process looked like before a probe started any session, for "left as it was found". */
	struct FProcessBaseline
	{
		UClass* ViewportClientClass = nullptr;
		/** Compared, never dereferenced: an editor with no session running has none, and must have none again. */
		const UGameViewportClient* GameViewport = nullptr;
		bool bHostDeliversCharacterEvents = false;
		int32 WorldContextCount = 0;
		FIntPoint LevelViewportSize = FIntPoint(-1, -1);
	};

	FProcessBaseline TakeBaseline()
	{
		FProcessBaseline Baseline;
		Baseline.ViewportClientClass = GEngine != nullptr ? GEngine->GameViewportClientClass.Get() : nullptr;
		Baseline.GameViewport = GEngine != nullptr ? GEngine->GameViewport.Get() : nullptr;
		Baseline.bHostDeliversCharacterEvents = UUITextInput::IsHostDeliveringCharacterEvents();
		Baseline.WorldContextCount = GEngine != nullptr ? GEngine->GetWorldContexts().Num() : 0;
		Baseline.LevelViewportSize = CurrentLevelViewportSize();
		return Baseline;
	}

	/**
	 * Everything a finished play session could leave behind that a later test would feel: the session
	 * itself, its world context, the game viewport, the viewport client class the rig swapped, the size
	 * of the level editor viewport the next session will start from, and the two pieces of process-wide
	 * text-input state a typing session moves.
	 */
	void CheckNothingLeftBehind(FAutomationTestBase& InTest, const FProcessBaseline& InBaseline, const TCHAR* InWhen)
	{
		const FIntPoint LevelViewportNow = CurrentLevelViewportSize();
		// Component-wise: the framework has no TestEqual for FIntPoint.
		InTest.TestEqual(FString::Printf(TEXT("%s, the level editor viewport is as wide as it was before the session (%s now, %s before) -- the next session starts at its size"),
			InWhen, *DescribeSize(LevelViewportNow), *DescribeSize(InBaseline.LevelViewportSize)),
			LevelViewportNow.X, InBaseline.LevelViewportSize.X);
		InTest.TestEqual(FString::Printf(TEXT("%s, ...and as tall"), InWhen), LevelViewportNow.Y, InBaseline.LevelViewportSize.Y);
		InTest.TestTrue(FString::Printf(TEXT("%s, no play session is running or queued"), InWhen),
			GEditor != nullptr && GEditor->PlayWorld == nullptr && !GEditor->IsPlaySessionInProgress());
		InTest.TestEqual(FString::Printf(TEXT("%s, the engine has no play world context"), InWhen), CountPlayWorldContexts(), 0);
		InTest.TestEqual(FString::Printf(TEXT("%s, the engine has as many world contexts as before the session"), InWhen),
			GEngine != nullptr ? GEngine->GetWorldContexts().Num() : -1, InBaseline.WorldContextCount);
		InTest.TestTrue(FString::Printf(TEXT("%s, the engine's game viewport is what it was before the session (none, in an editor)"), InWhen),
			GEngine != nullptr && GEngine->GameViewport.Get() == InBaseline.GameViewport);
		InTest.TestTrue(FString::Printf(TEXT("%s, the engine's viewport client class is the project's again"), InWhen),
			GEngine != nullptr && GEngine->GameViewportClientClass.Get() == InBaseline.ViewportClientClass);
		InTest.TestTrue(FString::Printf(TEXT("%s, the \"a host delivers characters\" switch is as it was found"), InWhen),
			UUITextInput::IsHostDeliveringCharacterEvents() == InBaseline.bHostDeliversCharacterEvents);
		InTest.TestNull(FString::Printf(TEXT("%s, no text field is being edited"), InWhen), UUITextInput::GetActiveTextInput());
	}

	/** One line with every number a session reported, so a run's log answers "what was this PIE like". */
	void ReportSession(FAutomationTestBase& InTest, const TCHAR* InWhich, const FDreamPieRigReport& InReport)
	{
		const FString EditorViewport = InReport.DestinationUsed == EDreamPieViewportDestination::LevelEditorViewport
			? FString::Printf(TEXT("the editor's level viewport was %s before the session"), *DescribeSize(InReport.EditorViewportSizeAtStart))
			: FString(TEXT("the editor's level viewport was not played in"));
		InTest.AddInfo(FString::Printf(
			TEXT("%s: up in %.2f s (%d engine frames) in %s; viewport came up %s and is %s%s; the screen canvas reads %s; %s; viewport client %s, game mode %s, player controller %s; keyboard focus %s the viewport as the session came up%s, and %s it once the rig was up; a frame is %.4f s; rendering %s."),
			InWhich,
			InReport.SecondsToStart, InReport.FramesToStart,
			DescribeDestination(InReport.DestinationUsed),
			*DescribeSize(InReport.ViewportSizeAsStarted), *DescribeSize(InReport.ViewportSize),
			InReport.bViewportSizeWasGiven ? TEXT(" (given by the rig, because it had none)") : TEXT(" (its own)"),
			*DescribeSize(InReport.CanvasViewportSize),
			*EditorViewport,
			*InReport.ViewportClientClass, *InReport.GameModeClass, *InReport.PlayerControllerClass,
			InReport.bKeyboardFocusOnViewport ? TEXT("was on") : TEXT("was not on"),
			InReport.bKeyboardFocusGiven ? TEXT(" (the rig handed it over)") : TEXT(""),
			InReport.bKeyboardFocusOnViewportWhenUp ? TEXT("was on") : TEXT("was not on"),
			InReport.FrameSecondsAtStart,
			GUsingNullRHI ? TEXT("through the null RHI") : TEXT("through a real RHI")));
	}
}

/*
 * The first question the layer has to answer: does a play session come up in this run at all, and
 * what does it look like when it has.
 *
 * Asserted: the world is a play world with a local player on the first controller; the viewport
 * client is UDreamGameViewportClient (so the engine really did read GameViewportClientClass while it
 * made the session); the viewport has a size; the rig's screen-space canvas, which is given no
 * substitute viewport here, reads that same size through the player controller -- the real road a
 * screen-space canvas takes in a game, and the one no headless test can reach; and once the rig is
 * up, Slate's keyboard focus is on the session's viewport. After the session: nothing left behind,
 * the level editor viewport's size included, and the keyboard focus not left where the rig put it.
 *
 * Printed: the seconds and frames it took, the viewport as the engine made it and as the rig left
 * it, where the session drew and how big the editor's viewport was, the classes involved, where
 * Slate's keyboard focus was and whether the rig had to hand it over, the frame time, and whether the
 * RHI is null. Then the seconds it took to stop, and whether the editor's viewport had to be put back.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieProbeStartTest,
	"DreamGUI.Pie.Probe.APlaySessionStartsOnABlankMapAndReportsItsViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieProbeStartTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieProbeLocal;
	const FProcessBaseline Baseline = TakeBaseline();
	const double QueuedSeconds = FPlatformTime::Seconds();

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this](FDreamDriverPieRig& InRig)
	{
		const FDreamPieRigReport& Report = InRig.GetReport();
		ReportSession(*this, TEXT("The session"), Report);

		UWorld* PlayWorld = InRig.GetWorld();
		if (!TestNotNull(TEXT("The rig is up in a world"), PlayWorld))
		{
			return;
		}
		TestTrue(TEXT("That world is a play-in-editor world"), PlayWorld->WorldType == EWorldType::PIE);
		APlayerController* Controller = InRig.GetPlayerController();
		TestTrue(TEXT("Its first player controller has a local player, the one the engine logged in"),
			Controller != nullptr && InRig.GetLocalPlayer() != nullptr && Controller->GetLocalPlayer() == InRig.GetLocalPlayer());
		UGameViewportClient* Client = InRig.GetViewportClient();
		TestTrue(TEXT("The session's viewport client is UDreamGameViewportClient, the class the rig put in the engine's GameViewportClientClass"),
			Client != nullptr && Client->IsA<UDreamGameViewportClient>());
		TestTrue(FString::Printf(TEXT("The session's viewport has a size (%s)"), *DescribeSize(Report.ViewportSize)),
			Report.ViewportSize.X > 0 && Report.ViewportSize.Y > 0);
		// Component-wise: the framework has no TestEqual for FIntPoint.
		TestEqual(TEXT("The screen canvas reads the session's own viewport width through the player controller"),
			Report.CanvasViewportSize.X, Report.ViewportSize.X);
		TestEqual(TEXT("...and its height"), Report.CanvasViewportSize.Y, Report.ViewportSize.Y);
		// What every typing test stands on: a typed character goes where Slate's keyboard focus is, and a
		// session whose game does not take the mouse only has it once somebody hands it over.
		TestTrue(TEXT("Once the rig is up, Slate's keyboard focus is on the session's viewport, which is where a typed character goes"),
			Report.bKeyboardFocusOnViewportWhenUp);
	});
	Rig->Finish();

	EnqueueCheck([this, Rig, Baseline, QueuedSeconds]()
	{
		AddInfo(FString::Printf(TEXT("The session stopped in %.2f s; the whole probe took %.2f s; the rig %s."),
			Rig->GetReport().SecondsToStop, FPlatformTime::Seconds() - QueuedSeconds,
			Rig->GetReport().bEditorViewportSizeRestored
				? TEXT("put the editor's level viewport back to its size from before the session")
				: TEXT("did not resize the editor's level viewport")));
		CheckNothingLeftBehind(*this, Baseline, TEXT("After the session"));
		TestFalse(TEXT("After the session, Slate's keyboard focus is not left on the viewport the rig handed it to"),
			Rig->IsKeyboardFocusStillGiven());
	});
	return true;
}

/*
 * Two sessions back to back, each started and stopped by its own rig, the way two PIE tests run one
 * after the other in a suite.
 *
 * The first one edits a field through the viewport client, which moves the two pieces of
 * process-wide text-input state -- the "a host delivers characters" switch that the first character
 * turns on for good, and the pointer to the field being edited -- so that "left as found" is a claim
 * about state that really was disturbed. After each session: no session, no play world context, no
 * game viewport, the project's viewport client class, the level editor viewport at its old size, the
 * switch as it was, no field being edited, and the keyboard focus not left where the rig put it.
 * And the second session must come up just as the first did -- the same viewport as the engine made
 * it, the same size once the rig was done, the same client class -- because a first session that left
 * something behind usually shows up as a second one that differs.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieProbeTwoSessionsTest,
	"DreamGUI.Pie.Probe.TwoPlaySessionsInARowLeaveNothingBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverPieProbeTwoSessionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieProbeLocal;
	const FProcessBaseline Baseline = TakeBaseline();
	const TSharedRef<FDreamPieRigReport> FirstReport = MakeShared<FDreamPieRigReport>();
	const TSharedRef<TWeakObjectPtr<UWorld>> FirstWorld = MakeShared<TWeakObjectPtr<UWorld>>();

	TSharedRef<FDreamDriverPieRig> First = FDreamDriverPieRig::Create(*this);
	First->Start();
	First->WhenReady([FirstReport, FirstWorld](FDreamDriverPieRig& InRig)
	{
		// Taken the moment the session is up, so that the comparison with the second session and the
		// "collected" check below stand whether or not anything later in this session goes right.
		*FirstReport = InRig.GetReport();
		*FirstWorld = InRig.GetWorld();
		InRig.MakeControl<UDreamTextInput>(TEXT("Scratch"), nullptr, FVector2D(320.0, 40.0));
	});
	{
		FDreamDriverSequence Steps = First->Sequence();
		Steps.Click(FDreamBy::Name(TEXT("Scratch")));
		Steps.Wait(FDreamUntil::Condition([First]() -> bool
			{
				const UDreamTextInput* Field = Cast<UDreamTextInput>(First->FindMade(TEXT("Scratch")));
				return Field != nullptr && Field->InputBehaviour != nullptr && Field->InputBehaviour->IsInputActive();
			}, FWaitTimeout::InSeconds(2.0)),
			FWaitTimeout::InSeconds(3.0), TEXT("the scratch field to begin an edit"));
		FDreamDriverPieRig::TypeThroughViewport(Steps, TEXT("x"));
		Steps.Then([this](FDreamDriverContext&)
		{
			// The disturbance itself, asserted, so the "put back" checks after the session are not
			// vacuous: a session that never moved either piece of state would pass them for nothing.
			TestNotNull(TEXT("While the first session edits a field, a field is being edited"), UUITextInput::GetActiveTextInput());
			TestTrue(TEXT("...and the character that reached it turned the \"a host delivers characters\" switch on"),
				UUITextInput::IsHostDeliveringCharacterEvents());
		});
		Steps.PerformLatent();
	}
	First->Finish();
	EnqueueCheck([this, Baseline, First, FirstWorld]()
	{
		CheckNothingLeftBehind(*this, Baseline, TEXT("After the first session"));
		TestFalse(TEXT("After the first session, its world has been collected"), FirstWorld->IsValid());
		TestFalse(TEXT("After the first session, Slate's keyboard focus is not left on the viewport the rig handed it to"),
			First->IsKeyboardFocusStillGiven());
	});

	TSharedRef<FDreamDriverPieRig> Second = FDreamDriverPieRig::Create(*this);
	Second->Start();
	Second->WhenReady([this, FirstReport](FDreamDriverPieRig& InRig)
	{
		const FDreamPieRigReport& SecondReport = InRig.GetReport();
		ReportSession(*this, TEXT("The first session"), *FirstReport);
		ReportSession(*this, TEXT("The second session"), SecondReport);
		// As the engine made them, before the rig did anything: a size the first rig gave its session and
		// never took back comes back here, through the editor's level viewport, as the second one's "own".
		TestEqual(TEXT("The second session's viewport came up as wide as the first's"),
			SecondReport.ViewportSizeAsStarted.X, FirstReport->ViewportSizeAsStarted.X);
		TestEqual(TEXT("...and as tall"), SecondReport.ViewportSizeAsStarted.Y, FirstReport->ViewportSizeAsStarted.Y);
		TestTrue(TEXT("The rig had to give the second session a size exactly when it had to give the first one"),
			SecondReport.bViewportSizeWasGiven == FirstReport->bViewportSizeWasGiven);
		TestEqual(TEXT("The second session's viewport is as wide as the first's"), SecondReport.ViewportSize.X, FirstReport->ViewportSize.X);
		TestEqual(TEXT("...and as tall"), SecondReport.ViewportSize.Y, FirstReport->ViewportSize.Y);
		TestEqual(TEXT("The second session has the same viewport client class as the first"),
			SecondReport.ViewportClientClass, FirstReport->ViewportClientClass);
	});
	Second->Finish();
	EnqueueCheck([this, Baseline, First, Second]()
	{
		AddInfo(FString::Printf(TEXT("Stopping took %.2f s the first time and %.2f s the second."),
			First->GetReport().SecondsToStop, Second->GetReport().SecondsToStop));
		TestFalse(TEXT("The first rig came up"), First->HasFailed());
		TestFalse(TEXT("The second rig came up"), Second->HasFailed());
		CheckNothingLeftBehind(*this, Baseline, TEXT("After the second session"));
		TestFalse(TEXT("After the second session, Slate's keyboard focus is not left on the viewport the rig handed it to"),
			Second->IsKeyboardFocusStillGiven());
	});
	return true;
}

/*
 * The picture: can a screen-space overlay's pixels be read back out of the play session's viewport?
 *
 * The render-target tests read a texture the canvas draws into; a screen-space canvas draws into the
 * VIEWPORT, through the same scene view extension, and a pixel check for screen-space UI needs a way
 * to read that. This tries the plainest one -- FViewport::ReadPixels on the session's viewport after
 * the render thread has caught up -- with one red block in the middle of the screen, and says what it
 * found either way: the viewport size, how many pixels came back, whether the viewport renders into a
 * separate target (which is what makes a read after the frame was presented meaningful), and the
 * colours at the block's centre and at a corner. Alpha is not compared: what a viewport keeps in alpha
 * is the scene's business, and the question is whether the UI's colour is there.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieProbeReadPixelsTest,
	"DreamGUI.Pie.Probe.RHI.AScreenSpaceBlockReadsBackRedFromThePlaySessionViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDriverPieProbeReadPixelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverPieProbeLocal;
	/**
	 * Frames drawn after the block is made before the viewport is read. The canvas hands its draw
	 * data to a worker and picks it up on a later tick, and the viewport is drawn at the end of each
	 * editor tick; five covers both with room to spare, and is the number to raise if this reads the
	 * empty scene.
	 */
	constexpr int32 FramesToSettle = 5;

	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([](FDreamDriverPieRig& InRig)
	{
		UDreamWidget* Block = InRig.MakeWidgetWithVisual(UDreamTexture::StaticClass(), TEXT("RedBlock"), nullptr, FVector2D(200.0, 200.0));
		if (UDreamTexture* Paint = Block != nullptr ? Cast<UDreamTexture>(Block->GetVisual()) : nullptr)
		{
			// White tinted red, as the render-target tests paint theirs: a solid colour with no content.
			Paint->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			Paint->SetColor(FColor(255, 0, 0, 255));
		}
	});
	Rig->Sequence()
		.WaitFrames(FramesToSettle)
		.Then([this, Rig](FDreamDriverContext&)
		{
			UGameViewportClient* Client = Rig->GetViewportClient();
			FViewport* Viewport = Client != nullptr ? Client->Viewport : nullptr;
			if (!TestNotNull(TEXT("The play session has a viewport to read"), Viewport))
			{
				return;
			}
			// The render thread drew this; its work has to be finished before the read means anything.
			FlushRenderingCommands();
			TArray<FColor> Pixels;
			const bool bRead = Viewport->ReadPixels(Pixels);
			const FIntPoint Size = Viewport->GetSizeXY();
			const FSceneViewport* SceneViewport = Rig->GetSceneViewport();
			const bool bSeparateTarget = SceneViewport != nullptr && static_cast<const ISlateViewport*>(SceneViewport)->UseSeparateRenderTarget();
			AddInfo(FString::Printf(TEXT("ReadPixels %s: %d pixels for a %s viewport; the viewport %s into a separate render target."),
				bRead ? TEXT("succeeded") : TEXT("failed"), Pixels.Num(), *DescribeSize(Size),
				bSeparateTarget ? TEXT("renders") : TEXT("does not render")));
			if (!TestTrue(TEXT("The play session's viewport can be read back"), bRead && Size.X > 0 && Size.Y > 0 && Pixels.Num() >= Size.X * Size.Y))
			{
				return;
			}

			// Where the canvas says the block is, not where this test assumes: the pixel the driver
			// would aim a click at is the one that has to be red.
			UDreamWidget* Block = Rig->FindMade(TEXT("RedBlock"));
			const TOptional<FVector2D> CentrePixel = Block != nullptr ? FDreamDriverProjection::WidgetCentrePixel(Block) : TOptional<FVector2D>();
			if (!TestTrue(TEXT("The block has a pixel on the viewport"), CentrePixel.IsSet()))
			{
				return;
			}
			const FIntPoint Centre(FMath::Clamp(FMath::RoundToInt32(CentrePixel->X), 0, Size.X - 1),
				FMath::Clamp(FMath::RoundToInt32(CentrePixel->Y), 0, Size.Y - 1));
			const FIntPoint Corner(4, 4);
			const FColor AtCentre = Pixels[Centre.Y * Size.X + Centre.X];
			const FColor AtCorner = Pixels[Corner.Y * Size.X + Corner.X];
			AddInfo(FString::Printf(TEXT("At the block's centre (%d, %d) the viewport holds %s; at the corner (%d, %d) it holds %s."),
				Centre.X, Centre.Y, *FDreamPixelProbe::Describe(AtCentre), Corner.X, Corner.Y, *FDreamPixelProbe::Describe(AtCorner)));

			const auto IsRed = [](const FColor& InColour) -> bool
			{
				return FMath::Abs(static_cast<int32>(InColour.R) - 255) <= ReadBackChannelTolerance
					&& static_cast<int32>(InColour.G) <= ReadBackChannelTolerance
					&& static_cast<int32>(InColour.B) <= ReadBackChannelTolerance;
			};
			TestTrue(TEXT("The block's centre reads back red"), IsRed(AtCentre));
			// A viewport that read back red everywhere would pass the centre alone and mean the read
			// returned something other than the picture.
			TestFalse(TEXT("A corner of the viewport, away from the block, does not"), IsRed(AtCorner));
		})
		.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
