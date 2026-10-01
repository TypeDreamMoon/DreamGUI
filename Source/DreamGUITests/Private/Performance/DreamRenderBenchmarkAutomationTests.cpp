// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PixelFormat.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUITextData.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "Utils/DreamUIUtils.h"

#include "RHI/DreamPixelProbe.h"

/*
 * What a fixed, busy scene costs DreamGUI a frame, stage by stage, so that two builds can be compared on the same
 * machine.
 *
 * The scene is several RenderTarget canvases in the editor's world, each a panel of the kind a game has: a rect block
 * behind, a grid of tinted blocks, a column of labels, a rounded clip holding more blocks, and on every other panel a
 * background blur. Four stretches of frames are timed -- the scene standing still, one block of each panel moving and
 * the rest still (what most of a real UI does most of the time), the scene animating (blocks moving and changing
 * colour, labels changing text), and the scene churning (widgets destroyed and made every frame) -- and for each the
 * per-stage costs DreamUIRenderStats counts are written to Saved/DreamGUITests/Perf/Benchmark.json, with a
 * CPU trace of the whole run beside it for Unreal Insights, each stretch a region of its own. Tools/Tests/perf_report.py
 * reads both and compares two runs.
 *
 * Nothing here fails on a number. A time is only worth something next to another run's on the same machine, and a
 * test that failed on one would fail on a slow day. What the test does hold is that the scene came up and drew.
 */
namespace DreamRenderBenchmarkTestLocal
{
	static constexpr int32 CanvasCount = 6;
	static constexpr int32 TargetExtent = 512;
	static constexpr int32 GridColumns = 16;
	static constexpr int32 GridRows = 10;
	static constexpr int32 LabelCount = 24;
	static constexpr int32 ClippedCount = 24;
	static constexpr int32 WarmUpFrames = 90;
	static constexpr int32 TimedFrames = 150;
	/** Widgets destroyed and made again, per canvas per frame, while the scene churns. */
	static constexpr int32 ChurnPerFrame = 4;

	struct FPanel
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TStrongObjectPtr<UDreamWidget> Root;
		TStrongObjectPtr<UDreamCanvas> Canvas;
		TArray<TWeakObjectPtr<UDreamWidget>> Blocks;
		TArray<FVector2D> BlockHomes;
		TArray<TWeakObjectPtr<UDreamText>> Labels;
		TArray<TWeakObjectPtr<UDreamWidget>> Churned;
		TWeakObjectPtr<UDreamWidget> ChurnParent;
	};

	class FBenchmarkStage
	{
	public:
		explicit FBenchmarkStage(FAutomationTestBase& InTest)
			: Test(InTest)
		{
			if (GEditor == nullptr || GEditor->GetEditorWorldContext().World() == nullptr || GEditor->GetAllViewportClients().Num() == 0)
			{
				Failure = TEXT("there is no editor world with a viewport to render it");
				return;
			}
			World = GEditor->GetEditorWorldContext().World();
			for (int32 Index = 0; Index < CanvasCount; ++Index)
			{
				BuildPanel(Index);
			}
		}

		~FBenchmarkStage()
		{
			TearDown();
		}

		FBenchmarkStage(const FBenchmarkStage&) = delete;
		FBenchmarkStage& operator=(const FBenchmarkStage&) = delete;

		bool IsUsable() const
		{
			if (!Failure.IsEmpty() || World == nullptr || Panels.Num() != CanvasCount)
			{
				return false;
			}
			for (const FPanel& Panel : Panels)
			{
				if (!IsValid(Panel.Canvas.Get()) || !IsValid(Panel.Target.Get()))
				{
					return false;
				}
			}
			return true;
		}
		const FString& GetFailure() const { return Failure; }
		FAutomationTestBase& GetTest() const { return Test; }

		int32 CountWidgets() const
		{
			int32 Count = 0;
			for (const FPanel& Panel : Panels)
			{
				// the root, its backdrop, the blocks, the labels, the clip and what it holds, and the blur
				Count += 2 + Panel.Blocks.Num() + Panel.Labels.Num() + 1 + ClippedCount + Panel.Churned.Num();
			}
			return Count;
		}

		int32 CountDrawCalls() const
		{
			int32 Count = 0;
			for (const FPanel& Panel : Panels)
			{
				if (const UDreamCanvas* Canvas = Panel.Canvas.Get(); IsValid(Canvas))
				{
					Count += Canvas->GetDrawCallCount();
				}
			}
			return Count;
		}

		/** Blocks drift on circles and a few change colour; four labels a panel count the frames. */
		void Animate(int32 InFrame)
		{
			for (FPanel& Panel : Panels)
			{
				for (int32 Index = 0; Index < Panel.Blocks.Num(); ++Index)
				{
					UDreamWidget* Block = Panel.Blocks[Index].Get();
					if (!IsValid(Block))
					{
						continue;
					}
					if (Index % 4 == InFrame % 4)
					{
						const double Angle = (InFrame + Index) * 0.2;
						Block->SetAnchoredPosition(Panel.BlockHomes[Index] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 4.0);
					}
					if (Index % 8 == InFrame % 8)
					{
						if (UDreamTexture* Visual = Cast<UDreamTexture>(Block->GetVisual()))
						{
							Visual->SetColor(FColor::MakeRedToGreenColorFromScalar(FMath::Frac((InFrame + Index) * 0.05f)));
						}
					}
				}
				for (int32 Index = 0; Index < 4 && Index < Panel.Labels.Num(); ++Index)
				{
					if (UDreamText* Label = Panel.Labels[(InFrame + Index * 6) % Panel.Labels.Num()].Get(); IsValid(Label))
					{
						Label->SetText(FText::FromString(FString::Printf(TEXT("Frame %05d"), InFrame)));
					}
				}
			}
		}

		/** One block a panel drifts a little, a different one each frame; everything else holds still. */
		void Nudge(int32 InFrame)
		{
			for (FPanel& Panel : Panels)
			{
				if (Panel.Blocks.Num() == 0)
				{
					continue;
				}
				const int32 Index = InFrame % Panel.Blocks.Num();
				if (UDreamWidget* Block = Panel.Blocks[Index].Get(); IsValid(Block))
				{
					const double Angle = InFrame * 0.2;
					Block->SetAnchoredPosition(Panel.BlockHomes[Index] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * 4.0);
				}
			}
		}

		/** ChurnPerFrame widgets a panel go, and as many new ones come. */
		void Churn(int32 InFrame)
		{
			for (FPanel& Panel : Panels)
			{
				for (int32 Index = 0; Index < ChurnPerFrame && Panel.Churned.Num() > 0; ++Index)
				{
					if (UDreamWidget* Old = Panel.Churned[0].Get(); IsValid(Old))
					{
						Old->DestroyWidget();
					}
					Panel.Churned.RemoveAt(0);
				}
				for (int32 Index = 0; Index < ChurnPerFrame; ++Index)
				{
					const int32 Slot = (InFrame * ChurnPerFrame + Index) % 32;
					const FVector2D Position(-180.0 + (Slot % 8) * 48.0, -200.0 + (Slot / 8) * 20.0);
					Panel.Churned.Add(AddBlock(TEXT("Churned"), FVector2D(40.0, 14.0), Position, FColor(120, 200, 255, 255), Panel.ChurnParent.Get()));
				}
			}
		}

		void RequestRedraw() const
		{
			if (GEditor != nullptr)
			{
				GEditor->RedrawAllViewports(false);
			}
		}

		bool ReadFirstPanel(TArray<FColor>& OutPixels, FIntPoint& OutSize) const
		{
			return Panels.Num() > 0 && FDreamPixelProbe::ReadBack(Panels[0].Target.Get(), OutPixels, OutSize);
		}

		/** Idempotent; the destructor calls it too. */
		void TearDown()
		{
			for (FPanel& Panel : Panels)
			{
				if (UDreamWidget* Root = Panel.Root.Get(); IsValid(Root))
				{
					Root->DestroyWidget();
				}
			}
			Panels.Reset();
			World = nullptr;
		}

	private:
		UDreamWidget* AddWidget(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, UDreamWidget* InParent)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
			Widget->SetDisplayName(InName);
			Widget->SetWidth(static_cast<float>(InSize.X));
			Widget->SetHeight(static_cast<float>(InSize.Y));
			Widget->OnRegister();
			Widget->TrySetParent(InParent, false);
			Widget->SetAnchoredPosition(InPosition);
			return Widget;
		}

		UDreamWidget* AddBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColour, UDreamWidget* InParent)
		{
			UDreamWidget* Widget = AddWidget(InName, InSize, InPosition, InParent);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetColor(InColour);
			}
			return Widget;
		}

		void BuildPanel(int32 InIndex)
		{
			FPanel& Panel = Panels.AddDefaulted_GetRef();
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			Target->ClearColor = FLinearColor::Black;
			Target->InitCustomFormat(static_cast<uint32>(TargetExtent), static_cast<uint32>(TargetExtent), EPixelFormat::PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate(true);
			Panel.Target.Reset(Target);

			UDreamWidget* Root = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
			Root->SetDisplayName(*FString::Printf(TEXT("BenchmarkPanel%d"), InIndex));
			Root->SetWidth(static_cast<float>(TargetExtent));
			Root->SetHeight(static_cast<float>(TargetExtent));
			Root->OnRegister();
			Panel.Root.Reset(Root);
			UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas == nullptr)
			{
				Failure = TEXT("a panel's root widget would not take a canvas");
				return;
			}
			Panel.Canvas.Reset(Canvas);
			Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
			Canvas->SetRenderTargetClearColor(FColor(20, 22, 30, 255));
			Canvas->SetRenderTargetResolutionScale(1.0f);
			Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
			Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
			Canvas->SetRenderTarget(Target);

			UDreamWidget* Backdrop = AddWidget(TEXT("Backdrop"), FVector2D(TargetExtent - 16.0, TargetExtent - 16.0), FVector2D::ZeroVector, Root);
			if (UDreamRectBlock* Block = Backdrop->CreateNewVisual<UDreamRectBlock>())
			{
				Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetBorderWidthUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetBodyColor(FColor(40, 44, 60, 255));
				Block->SetCornerRadius(FVector4(16.0, 16.0, 16.0, 16.0));
				Block->SetEnableBorder(true);
				Block->SetBorderWidth(2.0f);
				Block->SetBorderColor(FColor(120, 130, 160, 255));
			}

			for (int32 Row = 0; Row < GridRows; ++Row)
			{
				for (int32 Column = 0; Column < GridColumns; ++Column)
				{
					const FVector2D Home(-225.0 + Column * 30.0, 230.0 - Row * 22.0);
					const FColor Colour = FColor::MakeRedToGreenColorFromScalar(static_cast<float>(Row * GridColumns + Column) / (GridRows * GridColumns));
					Panel.Blocks.Add(AddBlock(TEXT("Cell"), FVector2D(24.0, 16.0), Home, Colour, Root));
					Panel.BlockHomes.Add(Home);
				}
			}

			for (int32 Index = 0; Index < LabelCount; ++Index)
			{
				const FVector2D Position(Index < LabelCount / 2 ? -150.0 : -30.0, -10.0 - (Index % (LabelCount / 2)) * 18.0);
				UDreamWidget* Widget = AddWidget(TEXT("Label"), FVector2D(110.0, 16.0), Position, Root);
				if (UDreamText* Label = Widget->CreateNewVisual<UDreamText>())
				{
					Label->SetText(FText::FromString(FString::Printf(TEXT("Item %03d of panel %d"), Index, InIndex)));
					Label->SetFontSize(13.0f);
					Label->SetColor(FColor(220, 225, 240, 255));
					Panel.Labels.Add(Label);
				}
			}

			UDreamWidget* Clip = AddWidget(TEXT("Clip"), FVector2D(180.0, 150.0), FVector2D(140.0, -90.0), Root);
			Clip->SetClipping(EDreamWidgetClipping::ClipToBounds);
			Clip->SetClippingCornerRadius(FVector4f(14.0f, 14.0f, 14.0f, 14.0f));
			for (int32 Index = 0; Index < ClippedCount; ++Index)
			{
				const FVector2D Position(-100.0 + (Index % 6) * 40.0, 80.0 - (Index / 6) * 45.0);
				AddBlock(TEXT("Clipped"), FVector2D(36.0, 40.0), Position, FColor(255, 160, 60, 220), Clip);
			}
			Panel.ChurnParent = Root;

			if (InIndex % 2 == 0)
			{
				UDreamWidget* Frosted = AddWidget(TEXT("Blur"), FVector2D(160.0, 100.0), FVector2D(140.0, 150.0), Root);
				if (UDreamBackgroundBlur* Blur = Frosted->CreateNewVisual<UDreamBackgroundBlur>())
				{
					Blur->SetBlurStrength(0.5f);
				}
			}
		}

		FAutomationTestBase& Test;
		FString Failure;
		UWorld* World = nullptr;
		TArray<FPanel> Panels;
	};

	using FStageRef = TSharedRef<FBenchmarkStage>;

	/** One timed stretch of frames. */
	struct FPhase
	{
		FString Name;
		int32 Frames = 0;
		double WallSeconds = 0.0;
		int32 DrawCalls = 0;
		DreamUIRenderStats::FSnapshot Stats;
	};

	struct FRun
	{
		TArray<FPhase> Phases;
		double PhaseStart = 0.0;
		bool bStartedTrace = false;
		FString TracePath;
	};

	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	void EnqueueDo(TFunction<void()> InAction)
	{
		EnqueueStep([InAction]() { InAction(); return true; });
	}

	/** InFrames frames, InEachFrame run at the start of each with its index, and a redraw asked for on each. */
	void EnqueueFrames(const FStageRef& InStage, int32 InFrames, TFunction<void(int32)> InEachFrame)
	{
		TSharedRef<int32> Done = MakeShared<int32>(0);
		EnqueueStep([InStage, InFrames, InEachFrame, Done]()
		{
			if (*Done >= InFrames)
			{
				return true;
			}
			if (InEachFrame)
			{
				InEachFrame(*Done);
			}
			InStage->RequestRedraw();
			++(*Done);
			return false;
		});
	}

	FString BenchmarkRegion(const FString& InPhase)
	{
		return FString::Printf(TEXT("DreamGUI.Benchmark.%s"), *InPhase);
	}

	void EnqueuePhase(const FStageRef& InStage, const TSharedRef<FRun>& InRun, const FString& InName, TFunction<void(int32)> InEachFrame)
	{
		EnqueueDo([InRun, InName]()
		{
			TRACE_BEGIN_REGION(*BenchmarkRegion(InName));
			DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			InRun->PhaseStart = FPlatformTime::Seconds();
		});
		EnqueueFrames(InStage, TimedFrames, InEachFrame);
		EnqueueDo([InStage, InRun, InName]()
		{
			FPhase& Phase = InRun->Phases.AddDefaulted_GetRef();
			Phase.Name = InName;
			Phase.WallSeconds = FPlatformTime::Seconds() - InRun->PhaseStart;
			Phase.Stats = DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			Phase.Frames = static_cast<int32>(Phase.Stats.Frames);
			Phase.DrawCalls = InStage->CountDrawCalls();
			TRACE_END_REGION(*BenchmarkRegion(InName));
		});
	}

	TSharedRef<FJsonObject> PhaseToJson(const FPhase& InPhase)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), InPhase.Name);
		Object->SetNumberField(TEXT("frames"), InPhase.Frames);
		Object->SetNumberField(TEXT("wallMsPerFrame"), InPhase.Frames > 0 ? InPhase.WallSeconds * 1000.0 / InPhase.Frames : 0.0);
		Object->SetNumberField(TEXT("drawCalls"), InPhase.DrawCalls);
		TSharedRef<FJsonObject> Stages = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Runs = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < DreamUIRenderStats::StageCount; ++Index)
		{
			const DreamUIRenderStats::EStage Stage = static_cast<DreamUIRenderStats::EStage>(Index);
			Stages->SetNumberField(DreamUIRenderStats::GetStageName(Stage), InPhase.Stats.GetMillisecondsPerFrame(Stage));
			Runs->SetNumberField(DreamUIRenderStats::GetStageName(Stage), static_cast<double>(InPhase.Stats.Entries[Index]));
		}
		Object->SetObjectField(TEXT("msPerFrame"), Stages);
		Object->SetObjectField(TEXT("runs"), Runs);
		TSharedRef<FJsonObject> Counters = MakeShared<FJsonObject>();
		for (int32 Index = 0; Index < DreamUIRenderStats::CounterCount; ++Index)
		{
			const DreamUIRenderStats::ECounter Counter = static_cast<DreamUIRenderStats::ECounter>(Index);
			Counters->SetNumberField(DreamUIRenderStats::GetCounterName(Counter), InPhase.Stats.GetPerFrame(Counter));
		}
		Object->SetObjectField(TEXT("perFrame"), Counters);
		return Object;
	}

	FString PerfDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Perf")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRenderBenchmarkTest,
	"DreamGUI.Performance.RHI.TheBenchmarkSceneRecordsWhatEachStageOfItsFramesCosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRenderBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderBenchmarkTestLocal;
	FStageRef Stage = MakeShared<FBenchmarkStage>(*this);
	if (!Stage->IsUsable())
	{
		AddError(FString::Printf(TEXT("The benchmark scene did not come up: %s."), *Stage->GetFailure()));
		Stage->TearDown();
		return false;
	}
	// The suite has every prepare a canvas makes from its last one checked against a full one (r.DreamUI.VerifyPartialPrepare,
	// in the test host's config): what is measured here is the prepare alone. Likewise every pointer kept while the count
	// of objects gone reads the same is looked up as well (r.DreamUI.VerifyKeptPointers): measured here without the look-ups.
	IConsoleVariable* const Verify = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DreamUI.VerifyPartialPrepare"));
	const int32 VerifyBefore = Verify != nullptr ? Verify->GetInt() : 0;
	if (Verify != nullptr)
	{
		Verify->Set(0, ECVF_SetByCode);
	}
	IConsoleVariable* const VerifyKept = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DreamUI.VerifyKeptPointers"));
	const int32 VerifyKeptBefore = VerifyKept != nullptr ? VerifyKept->GetInt() : 0;
	if (VerifyKept != nullptr)
	{
		VerifyKept->Set(0, ECVF_SetByCode);
	}
	TSharedRef<FRun> Run = MakeShared<FRun>();
	Run->TracePath = FPaths::Combine(PerfDirectory(), TEXT("Benchmark.utrace"));
	IFileManager::Get().MakeDirectory(*PerfDirectory(), /*Tree*/ true);
	IFileManager::Get().Delete(*Run->TracePath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);

	// Warm up: shaders, glyphs, pools and targets come into being over the first frames and are not what is measured.
	EnqueueFrames(Stage, WarmUpFrames, nullptr);
	EnqueueDo([Run]()
	{
		// Traced from here, so the file holds the benchmark and not the editor's start. A trace someone already
		// started from the command line is left to them.
		Run->bStartedTrace = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *Run->TracePath, TEXT("cpu,frame,region,bookmark"));
	});
	EnqueuePhase(Stage, Run, TEXT("Static"), nullptr);
	EnqueuePhase(Stage, Run, TEXT("Sparse"), [Stage](int32 InFrame) { Stage->Nudge(InFrame); });
	EnqueuePhase(Stage, Run, TEXT("Animated"), [Stage](int32 InFrame) { Stage->Animate(InFrame); });
	EnqueuePhase(Stage, Run, TEXT("Churn"), [Stage](int32 InFrame) { Stage->Churn(InFrame); });
	EnqueueDo([this, Stage, Run]()
	{
		if (Run->bStartedTrace)
		{
			FTraceAuxiliary::Stop();
		}
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (TestTrue(TEXT("The first panel of the benchmark reads back"), Stage->ReadFirstPanel(Pixels, Size)))
		{
			FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Benchmark_FirstPanel"));
		}
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> Scene = MakeShared<FJsonObject>();
		Scene->SetNumberField(TEXT("canvases"), CanvasCount);
		Scene->SetNumberField(TEXT("widgets"), Stage->CountWidgets());
		Scene->SetNumberField(TEXT("targetExtent"), TargetExtent);
		Scene->SetNumberField(TEXT("timedFrames"), TimedFrames);
		Root->SetObjectField(TEXT("scene"), Scene);
		TArray<TSharedPtr<FJsonValue>> Phases;
		for (const FPhase& Phase : Run->Phases)
		{
			Phases.Add(MakeShared<FJsonValueObject>(PhaseToJson(Phase)));
			AddInfo(FString::Printf(TEXT("%s: %.2f ms/frame wall; %s"), *Phase.Name,
				Phase.Frames > 0 ? Phase.WallSeconds * 1000.0 / Phase.Frames : 0.0,
				*DreamUIRenderStats::Describe(Phase.Stats).Replace(TEXT("\n"), TEXT("; "))));
		}
		Root->SetArrayField(TEXT("phases"), Phases);
		Root->SetStringField(TEXT("trace"), Run->bStartedTrace ? Run->TracePath : FString());
		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		const FString ReportPath = FPaths::Combine(PerfDirectory(), TEXT("Benchmark.json"));
		TestTrue(FString::Printf(TEXT("The benchmark's numbers are written to %s"), *ReportPath), FFileHelper::SaveStringToFile(Json, *ReportPath));
		TestEqual(TEXT("Every stretch of frames was timed"), Run->Phases.Num(), 4);
		for (const FPhase& Phase : Run->Phases)
		{
			TestTrue(FString::Printf(TEXT("%s drew: its panels made draw calls"), *Phase.Name), Phase.DrawCalls > 0);
			TestTrue(FString::Printf(TEXT("%s recorded passes on the render thread"), *Phase.Name),
				Phase.Stats.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::BatchesRecorded)] > 0);
		}
	});
	EnqueueDo([Stage, Verify, VerifyBefore, VerifyKept, VerifyKeptBefore]()
	{
		Stage->TearDown();
		if (Verify != nullptr)
		{
			Verify->Set(VerifyBefore, ECVF_SetByCode);
		}
		if (VerifyKept != nullptr)
		{
			VerifyKept->Set(VerifyKeptBefore, ECVF_SetByCode);
		}
	});
	return true;
}

#endif
