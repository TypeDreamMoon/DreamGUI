// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamNiagaraSystem.h"
#include "DreamUIBPLibrary.h"
#include "DreamUICaptureLibrary.h"
#include "DreamWidgetBlueprint.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "Slate/SceneViewport.h"
#include "UnrealClient.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverSequence.h"
#include "RHI/DreamPixelProbe.h"

/*
 * The shipped Niagara sample, played as a person plays it: a widget Blueprint of its class, made in a play session the way a
 * level Blueprint makes one (Create Dream Widget, Add to Viewport, the cursor shown, game and UI input), in a viewport the size
 * of a new play window, and each card's effect asked for with the pointer -- moved, pressed and let go of through the play
 * session's player controller, as a mouse button reaches it. Each effect is held to the play session's own picture: the
 * card's pixels after the gesture against its pixels before, and how far apart the ones that changed are. After a burst,
 * frame by frame, the scene round the UI is held to what it was: the components that simulate the systems are DreamGUI's to
 * draw, and once drew their first frame in the world as well, behind the cards.
 *
 * In a play session the world ticks the systems' components and the screen-space canvas draws them into the scene's view;
 * in the designer and the pixel tests the element steps the systems and a stage's target takes the picture. This is the one
 * test of the first. Its sprites once drew in the second and not here: the pixel shader read their frame blend off a
 * value the view's interpolation had left a hair under its whole step.
 *
 * The pointer's position goes in through the driver's module, as every play-session test's does: an event put into Slate
 * moves Slate's cursor for a frame, and the platform's cursor -- which a test may not move -- takes it back on the next.
 *
 * On a real RHI only: under -nullrhi Niagara does not run (FApp::CanEverRender) and a viewport has no pixels to read.
 */
namespace DreamPieNiagaraTestLocal
{
	const TCHAR* const SamplePackage = TEXT("/Game/UI/WBP_NiagaraEffects");
	const TCHAR* const SampleObject = TEXT("/Game/UI/WBP_NiagaraEffects.WBP_NiagaraEffects");
	/** A new play window's size, the editor's default one. */
	const FIntPoint PlayWindowSize(1280, 720);

	/** The sample made as its header tells a user to make it, its package kept until the test lets go; made once in an editor. */
	UClass* CompileSample(FAutomationTestBase& InTest)
	{
		UDreamWidgetBlueprint* Blueprint = FindObject<UDreamWidgetBlueprint>(nullptr, SampleObject);
		if (Blueprint == nullptr)
		{
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
			if (!InTest.TestTrue(TEXT("The plugin is installed"), Plugin.IsValid()))
			{
				return nullptr;
			}
			UPackage* Package = CreatePackage(SamplePackage);
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(TEXT("WBP_NiagaraEffects")), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (!InTest.TestNotNull(TEXT("A widget Blueprint for the sample"), Defaults))
			{
				return nullptr;
			}
			FString SamplePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetContentDir(), TEXT("Samples"), TEXT("NiagaraEffects.dui")));
			FPaths::NormalizeFilename(SamplePath);
			Defaults->SourceFile.FilePath = SamplePath;
			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			if (!InTest.TestEqual(TEXT("The sample compiles without an error"), Results.NumErrors, 0))
			{
				return nullptr;
			}
		}
		Blueprint->GetOutermost()->AddToRoot();
		return Blueprint->GeneratedClass.Get();
	}

	/** What the test keeps between its steps: the sample, weakly, and the picture before each gesture. */
	struct FState
	{
		TWeakObjectPtr<UDreamUserWidget> Sample;
		TArray<FColor> Before;
		FIntPoint BeforeSize = FIntPoint::ZeroValue;
	};

	UDreamWidget* Find(const FState& InState, const TCHAR* InName)
	{
		UDreamUserWidget* Sample = InState.Sample.Get();
		return Sample != nullptr ? Sample->FindChildByDisplayName(InName, true) : nullptr;
	}

	/** InWidget's InLocal01 (0..1 across its rect from its left, 0..1 up from its bottom) on the viewport. */
	TOptional<FVector2D> PixelIn(const UDreamWidget* InWidget, const FVector2D& InLocal01)
	{
		if (InWidget == nullptr)
		{
			return TOptional<FVector2D>();
		}
		const FVector2D Local(FMath::Lerp(InWidget->GetLocalSpaceLeft(), InWidget->GetLocalSpaceRight(), InLocal01.X),
			FMath::Lerp(InWidget->GetLocalSpaceBottom(), InWidget->GetLocalSpaceTop(), InLocal01.Y));
		return FDreamDriverProjection::WidgetLocalPointToPixel(InWidget, Local);
	}

	bool ReadViewport(const FDreamDriverContext& InContext, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		return UDreamUICaptureLibrary::ReadViewportPixels(UDreamUICaptureLibrary::FindViewportOf(InContext.World), OutPixels, OutSize);
	}

	/** The pixels of InRect whose channels moved by more than InTolerance between the pictures, and the box round them. */
	int32 CountChanged(const TArray<FColor>& InBefore, const TArray<FColor>& InAfter, FIntPoint InSize, const FBox2D& InRect, FBox2D& OutChangedBox,
		int32 InTolerance = 36)
	{
		OutChangedBox = FBox2D(ForceInit);
		if (InBefore.Num() != InAfter.Num() || InBefore.Num() != InSize.X * InSize.Y)
		{
			return -1;
		}
		const int32 MinX = FMath::Clamp(FMath::FloorToInt32(InRect.Min.X), 0, InSize.X);
		const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(InRect.Max.X), 0, InSize.X);
		const int32 MinY = FMath::Clamp(FMath::FloorToInt32(InRect.Min.Y), 0, InSize.Y);
		const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(InRect.Max.Y), 0, InSize.Y);
		int32 Changed = 0;
		for (int32 Y = MinY; Y < MaxY; ++Y)
		{
			for (int32 X = MinX; X < MaxX; ++X)
			{
				const FColor A = InBefore[Y * InSize.X + X];
				const FColor B = InAfter[Y * InSize.X + X];
				if (FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) > InTolerance)
				{
					++Changed;
					OutChangedBox += FVector2D(X, Y);
				}
			}
		}
		return Changed;
	}

	/** What a system is doing, for the log of a run whose card stayed dark. */
	FString Describe(const UDreamWidget* InWidget)
	{
		const UDreamNiagaraSystem* System = InWidget != nullptr ? Cast<UDreamNiagaraSystem>(InWidget->GetVisual()) : nullptr;
		if (System == nullptr)
		{
			return TEXT("no system");
		}
		TArray<FString> Counts;
		for (int32 Segment = 0; Segment < System->GetSegmentCount(); ++Segment)
		{
			Counts.Add(FString::FromInt(System->GetSegmentParticleCount(Segment)));
		}
		return FString::Printf(TEXT("%s: active %d, bursts %d, particles per segment [%s]"), *InWidget->GetDisplayName(),
			System->IsActive() ? 1 : 0, System->GetBurstCount(), *FString::Join(Counts, TEXT(", ")));
	}

	/** Queue: the picture as it is now, kept as the one the next gesture is measured against. */
	void Snapshot(FDreamDriverSequence& InSteps, const TSharedRef<FState>& InState)
	{
		InSteps.Then([InState](FDreamDriverContext& InContext)
		{
			ReadViewport(InContext, InState->Before, InState->BeforeSize);
		});
	}

	/** Queue: the pointer to wherever InResolver says when the step runs, then two frames. */
	void MoveToResolved(FDreamDriverSequence& InSteps, FDreamDriverSequence::FPixelResolver InResolver, const FString& InDescription)
	{
		InSteps.MoveToResolvedPixel(MoveTemp(InResolver), InDescription);
		InSteps.WaitFrames(2);
	}

	/** Queue: the pointer off every card, to the viewport's bottom right corner. */
	void MoveAway(FDreamDriverSequence& InSteps)
	{
		MoveToResolved(InSteps, [](FDreamDriverContext& InContext) -> TOptional<FVector2D>
		{
			UGameViewportClient* Client = InContext.World != nullptr ? InContext.World->GetGameViewport() : nullptr;
			const FIntPoint Size = Client != nullptr && Client->Viewport != nullptr ? Client->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
			return FVector2D(Size.X - 6, Size.Y - 6);
		}, TEXT("the viewport's bottom right corner"));
	}

	/** Queue: the pointer to InWidget's InLocal01. */
	void MoveTo(FDreamDriverSequence& InSteps, const TSharedRef<FState>& InState, const TCHAR* InWidget, FVector2D InLocal01)
	{
		const FString Name(InWidget);
		MoveToResolved(InSteps, [InState, Name, InLocal01](FDreamDriverContext&) { return PixelIn(Find(*InState, *Name), InLocal01); },
			FString::Printf(TEXT("%s at (%.2f, %.2f)"), *Name, InLocal01.X, InLocal01.Y));
	}

	/** Queue: a click at InWidget's InLocal01: the press, a frame, the release, and InFramesAfter frames. */
	void ClickOn(FDreamDriverSequence& InSteps, const TSharedRef<FState>& InState, const TCHAR* InWidget, FVector2D InLocal01, int32 InFramesAfter = 1)
	{
		MoveTo(InSteps, InState, InWidget, InLocal01);
		InSteps.Press();
		InSteps.WaitFrames(1);
		InSteps.Release();
		if (InFramesAfter > 0)
		{
			InSteps.WaitFrames(InFramesAfter);
		}
	}

	/** The systems the sample has, by their widgets' names. */
	const TCHAR* const SystemNames[] = { TEXT("Sparks"), TEXT("Trail"), TEXT("Edge"), TEXT("Burst"), TEXT("Coins") };

	/** How many of the sample's Niagara components the scene has a proxy for: each one is a system drawn in the world as well. */
	int32 CountSceneProxies(const FState& InState)
	{
		int32 Count = 0;
		for (const TCHAR* Name : SystemNames)
		{
			const UDreamWidget* Widget = Find(InState, Name);
			const UDreamNiagaraSystem* System = Widget != nullptr ? Cast<UDreamNiagaraSystem>(Widget->GetVisual()) : nullptr;
			if (System == nullptr)
			{
				continue;
			}
			TArray<UObject*> Subobjects;
			GetObjectsWithOuter(System, Subobjects, EGetObjectsFlags::None);
			for (const UObject* Object : Subobjects)
			{
				const UNiagaraComponent* Component = Cast<UNiagaraComponent>(Object);
				Count += Component != nullptr && Component->SceneProxy != nullptr ? 1 : 0;
			}
		}
		return Count;
	}

	/**
	 * Queue: InFrames frames from now on, the scene round the sample held to the snapshot -- every pixel outside the row of
	 * cards, where the UI draws nothing, as it was before the gesture -- and none of the sample's Niagara components given a
	 * proxy by the scene. A system DreamGUI draws is simulated by a component of its own, which must not be drawn in the world
	 * too: not even the frame it is activated in, before Niagara turns a component's rendering off at its first tick.
	 */
	void WatchTheScene(FAutomationTestBase& InTest, FDreamDriverSequence& InSteps, const TSharedRef<FState>& InState, const TCHAR* InEffect, int32 InFrames)
	{
		struct FWatch
		{
			int32 MostChanged = 0;
			int32 FrameOfMost = -1;
			int32 MostProxies = 0;
			int32 FramesRead = 0;
		};
		const TSharedRef<FWatch> Watch = MakeShared<FWatch>();
		for (int32 Frame = 0; Frame < InFrames; ++Frame)
		{
			InSteps.Then([InState, Watch, Frame](FDreamDriverContext& InContext)
			{
				Watch->MostProxies = FMath::Max(Watch->MostProxies, CountSceneProxies(*InState));
				TArray<FColor> Now;
				FIntPoint Size = FIntPoint::ZeroValue;
				const TOptional<FBox2D> Row = FDreamDriverProjection::WidgetToPixelRect(Find(*InState, TEXT("Row")));
				if (!ReadViewport(InContext, Now, Size) || Size != InState->BeforeSize || !Row.IsSet())
				{
					return;
				}
				++Watch->FramesRead;
				// The row, and a pixel round it for its edges' filtering, is the UI's; the rest of the viewport is the scene's.
				const FBox2D Ui(Row->Min - FVector2D(2.0, 2.0), Row->Max + FVector2D(2.0, 2.0));
				int32 Changed = 0;
				for (int32 Y = 0; Y < Size.Y; ++Y)
				{
					for (int32 X = 0; X < Size.X; ++X)
					{
						if (Ui.IsInside(FVector2D(X, Y)))
						{
							continue;
						}
						const FColor A = InState->Before[Y * Size.X + X];
						const FColor B = Now[Y * Size.X + X];
						Changed += FMath::Abs(A.R - B.R) + FMath::Abs(A.G - B.G) + FMath::Abs(A.B - B.B) > 12 ? 1 : 0;
					}
				}
				if (Changed > Watch->MostChanged)
				{
					Watch->MostChanged = Changed;
					Watch->FrameOfMost = Frame;
					FDreamPixelProbe::SaveCapture(Now, Size, TEXT("Pie_NiagaraSample_SceneChanged"));
				}
			});
			InSteps.WaitFrames(1);
		}
		FAutomationTestBase* Test = &InTest;
		const FString Effect(InEffect);
		InSteps.Then([Test, Watch, Effect, InFrames](FDreamDriverContext&)
		{
			Test->TestEqual(*FString::Printf(TEXT("%s: every frame of the %d after the gesture is read back"), *Effect, InFrames), Watch->FramesRead, InFrames);
			Test->TestEqual(*FString::Printf(TEXT("%s: the scene round the UI is untouched in every one of them (most pixels changed: %d, %d frames in)"),
				*Effect, Watch->MostChanged, Watch->FrameOfMost), Watch->MostChanged, 0);
			Test->TestEqual(*FString::Printf(TEXT("%s: ...and the scene has a proxy for none of the systems' components (most at once: %d)"), *Effect, Watch->MostProxies),
				Watch->MostProxies, 0);
		});
	}

	/** What one effect is held to. */
	struct FExpect
	{
		const TCHAR* Effect = nullptr;
		/** The card measured, and the system described in the log. */
		const TCHAR* Card = nullptr;
		const TCHAR* System = nullptr;
		/** At least so many of the card's pixels change... */
		int32 AtLeast = 0;
		/** ...and the box round them is at least this wide and tall: particles spread as the effect spreads them, not in a heap. */
		FVector2D Spread = FVector2D::ZeroVector;
		/** A widget the gesture changes itself, whose pixels are left out. */
		const TCHAR* Except = nullptr;
	};

	/** Queue: the picture now, the effect's card held to the snapshot, the system described, the picture written to Captures. */
	void ExpectDrawn(FAutomationTestBase& InTest, FDreamDriverSequence& InSteps, const TSharedRef<FState>& InState, const FExpect& InExpect)
	{
		FAutomationTestBase* Test = &InTest;
		const FString Effect(InExpect.Effect);
		const FString Card(InExpect.Card);
		const FString System(InExpect.System);
		const FString Except(InExpect.Except != nullptr ? InExpect.Except : TEXT(""));
		const int32 AtLeast = InExpect.AtLeast;
		const FVector2D Spread = InExpect.Spread;
		InSteps.Then([Test, InState, Effect, Card, System, Except, AtLeast, Spread](FDreamDriverContext& InContext)
		{
			TArray<FColor> After;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (!Test->TestTrue(TEXT("The play session's viewport reads back"), ReadViewport(InContext, After, Size)))
			{
				return;
			}
			FDreamPixelProbe::SaveCapture(After, Size, FString::Printf(TEXT("Pie_NiagaraSample_%s"), *Effect));
			Test->AddInfo(Describe(Find(*InState, *System)));
			const TOptional<FBox2D> Rect = FDreamDriverProjection::WidgetToPixelRect(Find(*InState, *Card));
			if (!Test->TestTrue(*FString::Printf(TEXT("%s has a rect on the viewport"), *Card), Rect.IsSet())
				|| !Test->TestEqual(TEXT("The pictures are the same size"), Size, InState->BeforeSize))
			{
				return;
			}
			TArray<FColor> Before = InState->Before;
			if (const TOptional<FBox2D> Left = Except.IsEmpty() ? TOptional<FBox2D>() : FDreamDriverProjection::WidgetToPixelRect(Find(*InState, *Except)))
			{
				// What the excepted widget covers counts as unchanged.
				for (int32 Y = FMath::Max(0, FMath::FloorToInt32(Left->Min.Y)); Y < FMath::Min(Size.Y, FMath::CeilToInt32(Left->Max.Y)); ++Y)
				{
					for (int32 X = FMath::Max(0, FMath::FloorToInt32(Left->Min.X)); X < FMath::Min(Size.X, FMath::CeilToInt32(Left->Max.X)); ++X)
					{
						Before[Y * Size.X + X] = After[Y * Size.X + X];
					}
				}
			}
			FBox2D ChangedBox;
			const int32 Changed = CountChanged(Before, After, Size, Rect.GetValue(), ChangedBox);
			const FVector2D Extent = ChangedBox.bIsValid ? ChangedBox.GetSize() : FVector2D::ZeroVector;
			Test->TestTrue(*FString::Printf(TEXT("%s: the particles are drawn over %s (%d pixels changed, at least %d wanted)"), *Effect, *Card, Changed, AtLeast),
				Changed >= AtLeast);
			Test->TestTrue(*FString::Printf(TEXT("%s: ...spread over %.0f by %.0f pixels (at least %.0f by %.0f wanted)"), *Effect, Extent.X, Extent.Y, Spread.X, Spread.Y),
				Extent.X >= Spread.X && Extent.Y >= Spread.Y);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverPieNiagaraSampleTest,
	"DreamGUI.Pie.RHI.TheNiagaraSamplesEffectsAreDrawnForThePointerInAPlaySession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamDriverPieNiagaraSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamPieNiagaraTestLocal;
	UClass* SampleClass = CompileSample(*this);
	if (SampleClass == nullptr)
	{
		return false;
	}
	const TSharedRef<FState> State = MakeShared<FState>();
	TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([State, SampleClass](FDreamDriverPieRig& InRig)
	{
		// The size of a new play window, which the row of cards is laid out for: the level editor's viewport the session plays
		// in may be narrower than the row. Let go of again before the session ends (below).
		if (FSceneViewport* SceneViewport = InRig.GetSceneViewport())
		{
			SceneViewport->SetFixedViewportSize(PlayWindowSize.X, PlayWindowSize.Y);
		}
		// What a level Blueprint's BeginPlay does with it: Create Dream Widget, Add to Viewport, the cursor shown, game and UI.
		APlayerController* Controller = InRig.GetPlayerController();
		UDreamUserWidget* Sample = UDreamUIBPLibrary::FinishDeferredCreateDreamWidget(
			UDreamUIBPLibrary::BeginDeferredCreateDreamWidget(InRig.GetWorld(), SampleClass, Controller));
		if (Sample != nullptr)
		{
			Sample->AddToViewport(0);
		}
		if (Controller != nullptr)
		{
			Controller->SetShowMouseCursor(true);
			Controller->SetInputMode(FInputModeGameAndUI());
		}
		State->Sample = Sample;
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.Then([this, State](FDreamDriverContext&)
	{
		TestTrue(TEXT("The sample is on the screen"), State->Sample.IsValid());
	});
	Steps.WaitFrames(4);

	// Trail: the pointer moving across the card, above its caption, lays a ribbon behind it (OnEnter activates the system).
	Snapshot(Steps, State);
	for (int32 Move = 0; Move <= 8; ++Move)
	{
		MoveTo(Steps, State, TEXT("TrailCard"), FVector2D(0.15 + 0.0875 * Move, 0.75));
	}
	ExpectDrawn(*this, Steps, State, { TEXT("Trail"), TEXT("TrailCard"), TEXT("Trail"), 40, FVector2D(60.0, 4.0) });
	MoveAway(Steps);
	Steps.WaitSeconds(1.2f);

	// Sparks: a click on the card throws a ring of sparks out from where it lands -- and nothing into the scene behind the UI.
	Snapshot(Steps, State);
	ClickOn(Steps, State, TEXT("SparksCard"), FVector2D(0.5, 0.7), 0);
	WatchTheScene(*this, Steps, State, TEXT("Sparks"), 8);
	ExpectDrawn(*this, Steps, State, { TEXT("Sparks"), TEXT("SparksCard"), TEXT("Sparks"), 150, FVector2D(20.0, 20.0) });
	MoveAway(Steps);
	Steps.WaitSeconds(1.2f);

	// Border: the pointer on the button, above its caption, runs sparks round all of its edge.
	Snapshot(Steps, State);
	MoveTo(Steps, State, TEXT("Button"), FVector2D(0.5, 0.9));
	Steps.WaitSeconds(0.3f);
	ExpectDrawn(*this, Steps, State, { TEXT("Border"), TEXT("BorderCard"), TEXT("Edge"), 150, FVector2D(100.0, 30.0) });
	MoveAway(Steps);
	Steps.WaitSeconds(1.2f);

	// Charge: four clicks fill the bar, and the full bar bursts. The bar's own pixels are left out: it fills either way.
	Snapshot(Steps, State);
	for (int32 Click = 0; Click < 3; ++Click)
	{
		ClickOn(Steps, State, TEXT("ChargeCard"), FVector2D(0.5, 0.25));
	}
	// The fourth fills the bar, and the trigger bursts at the centre (BurstAtCentre) on the frames that follow.
	ClickOn(Steps, State, TEXT("ChargeCard"), FVector2D(0.5, 0.25), 0);
	WatchTheScene(*this, Steps, State, TEXT("Charge"), 8);
	ExpectDrawn(*this, Steps, State, { TEXT("Charge"), TEXT("ChargeCard"), TEXT("Burst"), 40, FVector2D(100.0, 20.0), TEXT("Charge") });
	MoveAway(Steps);
	Steps.WaitSeconds(1.2f);

	// Coins: a click on the chest sends coins from it to the counter.
	Snapshot(Steps, State);
	ClickOn(Steps, State, TEXT("Chest"), FVector2D(0.5, 0.5), 0);
	WatchTheScene(*this, Steps, State, TEXT("Coins"), 8);
	Steps.WaitSeconds(0.25f);
	ExpectDrawn(*this, Steps, State, { TEXT("Coins"), TEXT("CoinCard"), TEXT("Coins"), 100, FVector2D(20.0, 20.0) });

	Steps.Then([Rig](FDreamDriverContext&)
	{
		// The editor's viewport takes its own size back: the session's carries over into it when the session ends.
		if (FSceneViewport* SceneViewport = Rig->GetSceneViewport())
		{
			SceneViewport->SetFixedViewportSize(0, 0);
		}
		if (UPackage* Package = FindPackage(nullptr, SamplePackage))
		{
			Package->RemoveFromRoot();
		}
	});
	Steps.WaitFrames(2);
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
