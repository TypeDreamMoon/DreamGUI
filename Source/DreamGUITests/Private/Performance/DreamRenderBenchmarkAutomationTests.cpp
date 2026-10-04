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
 * Then the labels -- 13 px, small text drawn from coverage glyphs -- move the ways small text moves in a game, each way
 * timed twice in the one session: with coverage on and with it off (DreamGUI.Text.SmallTextCoverage 1 and 0), so the
 * cost of coverage under motion is read off one machine at one moment. A label slides by 0.37 of a pixel a frame (every
 * frame off the pixel grid), the labels scroll a whole pixel a frame the way a scroll view moves its content, each
 * label's scale tweens as a pressed button's does, each label turns -- once with render layers on and once with them
 * off -- the whole column zooms, and the labels' colour pulses. The side with coverage on must have drawn some coverage
 * glyphs, and the side with it off none: otherwise the pair measured nothing.
 *
 * Nothing here fails on a time. A time is only worth something next to another run's on the same machine, and a
 * test that failed on one would fail on a slow day. What the test does hold is that the scene came up and drew, and that
 * each side of a coverage pair drew what its switch says.
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
	/** The label motions: frames timed per side, and frames each side is given first to settle after its switch. */
	static constexpr int32 MotionTimedFrames = 120;
	static constexpr int32 MotionSettleFrames = 30;
	/** How far a sliding label moves a frame: never a whole pixel, so every frame lands somewhere else on the grid. */
	static constexpr double SlidePixelsPerFrame = 0.37;
	/** The labels' colours while they pulse, and at rest. */
	static const FColor LabelColour(220, 225, 240, 255);
	static const FColor PulseColour(255, 196, 80, 255);

	struct FPanel
	{
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TStrongObjectPtr<UDreamWidget> Root;
		TStrongObjectPtr<UDreamCanvas> Canvas;
		TArray<TWeakObjectPtr<UDreamWidget>> Blocks;
		TArray<FVector2D> BlockHomes;
		TArray<TWeakObjectPtr<UDreamText>> Labels;
		/** The labels' own widgets, which the label motions move, scale and turn. */
		TArray<TWeakObjectPtr<UDreamWidget>> LabelWidgets;
		/** What holds the labels, the size of the panel: what scrolls and zooms. */
		TWeakObjectPtr<UDreamWidget> LabelColumn;
		TArray<TWeakObjectPtr<UDreamWidget>> Churned;
		TWeakObjectPtr<UDreamWidget> ChurnParent;
	};

	/** How the labels move in a motion phase, frame by frame. */
	enum class ELabelMotion : uint8
	{
		SubpixelSlide,
		WholePixelScroll,
		ScaleTween,
		Rotate,
		CanvasZoom,
		ColorPulse,
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
				// the root, its backdrop, the label column, the blocks, the labels, the clip and what it holds, and the blur
				Count += 3 + Panel.Blocks.Num() + Panel.Labels.Num() + 1 + ClippedCount + Panel.Churned.Num();
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

		/** Every label as the scene was built: no render transform, at home, in its own colour. */
		void ResetLabels()
		{
			for (FPanel& Panel : Panels)
			{
				if (UDreamWidget* Column = Panel.LabelColumn.Get(); IsValid(Column))
				{
					Column->ClearRenderTransform();
					Column->SetAnchoredPosition(FVector2D::ZeroVector);
				}
				for (const TWeakObjectPtr<UDreamWidget>& LabelWidget : Panel.LabelWidgets)
				{
					if (UDreamWidget* Widget = LabelWidget.Get(); IsValid(Widget))
					{
						Widget->ClearRenderTransform();
					}
				}
				for (const TWeakObjectPtr<UDreamText>& LabelText : Panel.Labels)
				{
					if (UDreamText* Label = LabelText.Get(); IsValid(Label))
					{
						Label->SetColor(LabelColour);
					}
				}
			}
		}

		/** One frame of InMotion for every label of every panel. */
		void MoveLabels(ELabelMotion InMotion, int32 InFrame)
		{
			for (FPanel& Panel : Panels)
			{
				UDreamWidget* Column = Panel.LabelColumn.Get();
				if (InMotion == ELabelMotion::WholePixelScroll && IsValid(Column))
				{
					// Laid out a whole pixel further each frame, the way a scroll view moves its content: by position, not
					// by a render transform, so the column is never a render layer.
					Column->SetAnchoredPosition(FVector2D(0.0, static_cast<double>(InFrame % 40)));
					continue;
				}
				if (InMotion == ELabelMotion::CanvasZoom && IsValid(Column))
				{
					// The whole column zooming, every label's device scale changing every frame.
					const double Zoom = 1.0 + 0.25 * FMath::Sin(InFrame * 0.05);
					Column->SetRenderScale(FVector(1.0, Zoom, Zoom));
					continue;
				}
				for (int32 Index = 0; Index < Panel.LabelWidgets.Num(); ++Index)
				{
					UDreamWidget* Widget = Panel.LabelWidgets[Index].Get();
					if (!IsValid(Widget))
					{
						continue;
					}
					switch (InMotion)
					{
					case ELabelMotion::SubpixelSlide:
						// Back to the start every 100 frames, 37 pixels on: the slide stays inside the panel.
						Widget->SetRenderTranslation(FVector(0.0, SlidePixelsPerFrame * (InFrame % 100), 0.0));
						break;
					case ELabelMotion::ScaleTween:
					{
						const double Scale = 1.0 + 0.06 * FMath::Sin(InFrame * 0.15 + Index);
						Widget->SetRenderScale(FVector(1.0, Scale, Scale));
						break;
					}
					case ELabelMotion::Rotate:
						Widget->SetRenderRotation(FRotator(0.0, 0.0, static_cast<double>((InFrame * 2 + Index * 15) % 360)));
						break;
					case ELabelMotion::ColorPulse:
						if (Index < Panel.Labels.Num())
						{
							if (UDreamText* Label = Panel.Labels[Index].Get(); IsValid(Label))
							{
								const float Blend = 0.5f + 0.5f * FMath::Sin(InFrame * 0.2f + Index * 0.3f);
								Label->SetColor(FLinearColor::LerpUsingHSV(FLinearColor(LabelColour), FLinearColor(PulseColour), Blend).ToFColor(true));
							}
						}
						break;
					default:
						break;
					}
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

			// The labels in a column of their own, the size of the panel, so that it can scroll and zoom them all at once.
			UDreamWidget* ColumnWidget = AddWidget(TEXT("LabelColumn"), FVector2D(TargetExtent, TargetExtent), FVector2D::ZeroVector, Root);
			Panel.LabelColumn = ColumnWidget;
			for (int32 Index = 0; Index < LabelCount; ++Index)
			{
				const FVector2D Position(Index < LabelCount / 2 ? -150.0 : -30.0, -10.0 - (Index % (LabelCount / 2)) * 18.0);
				UDreamWidget* Widget = AddWidget(TEXT("Label"), FVector2D(110.0, 16.0), Position, ColumnWidget);
				if (UDreamText* Label = Widget->CreateNewVisual<UDreamText>())
				{
					Label->SetText(FText::FromString(FString::Printf(TEXT("Item %03d of panel %d"), Index, InIndex)));
					Label->SetFontSize(13.0f);
					Label->SetColor(LabelColour);
					Panel.Labels.Add(Label);
					Panel.LabelWidgets.Add(Widget);
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
		/** How the coverage switch and the render layers stood: "on", "off", or "project" for as the project has them. */
		FString SmallTextCoverage = TEXT("project");
		FString RenderLayers = TEXT("project");
		/** Coverage glyph items drawn while the phase settled after its switches were set, before it was timed; -1 when it did not settle. */
		int64 CoverageItemsWhileSettling = -1;
	};

	struct FRun
	{
		TArray<FPhase> Phases;
		double PhaseStart = 0.0;
		bool bStartedTrace = false;
		FString TracePath;
		/** What the next phase is recorded with: set while it settles, taken when it is recorded. */
		FString NextSmallTextCoverage = TEXT("project");
		FString NextRenderLayers = TEXT("project");
		int64 NextCoverageItemsWhileSettling = -1;
	};

	/** One label motion, timed with coverage on and with it off; Rotate twice, with render layers on and off. */
	struct FMotionCase
	{
		const TCHAR* Name;
		ELabelMotion Motion;
		/** r.DreamUI.RenderLayers for the pair: -1 is the value the run found, which a pair before it may have changed. */
		int32 RenderLayers;
	};
	static const FMotionCase MotionCases[] =
	{
		{ TEXT("SubpixelSlide"), ELabelMotion::SubpixelSlide, -1 },
		{ TEXT("WholePixelScroll"), ELabelMotion::WholePixelScroll, -1 },
		{ TEXT("ScaleTween"), ELabelMotion::ScaleTween, -1 },
		{ TEXT("Rotate.LayersOn"), ELabelMotion::Rotate, 1 },
		{ TEXT("Rotate.LayersOff"), ELabelMotion::Rotate, 0 },
		{ TEXT("CanvasZoom"), ELabelMotion::CanvasZoom, -1 },
		{ TEXT("ColorPulse"), ELabelMotion::ColorPulse, -1 },
	};
	/** The phases before the label motions: Static, Sparse, Animated and Churn. */
	static constexpr int32 ScenePhaseCount = 4;

	/** The console variable a coverage pair switches, and the render layers' switch. */
	static const TCHAR* const SmallTextCoverageVariable = TEXT("DreamGUI.Text.SmallTextCoverage");
	static const TCHAR* const RenderLayersVariable = TEXT("r.DreamUI.RenderLayers");

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

	void EnqueuePhase(const FStageRef& InStage, const TSharedRef<FRun>& InRun, const FString& InName, TFunction<void(int32)> InEachFrame,
		int32 InFrames = TimedFrames)
	{
		EnqueueDo([InRun, InName]()
		{
			TRACE_BEGIN_REGION(*BenchmarkRegion(InName));
			DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			InRun->PhaseStart = FPlatformTime::Seconds();
		});
		EnqueueFrames(InStage, InFrames, InEachFrame);
		EnqueueDo([InStage, InRun, InName]()
		{
			FPhase& Phase = InRun->Phases.AddDefaulted_GetRef();
			Phase.Name = InName;
			Phase.WallSeconds = FPlatformTime::Seconds() - InRun->PhaseStart;
			Phase.Stats = DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			Phase.Frames = static_cast<int32>(Phase.Stats.Frames);
			Phase.DrawCalls = InStage->CountDrawCalls();
			Phase.SmallTextCoverage = InRun->NextSmallTextCoverage;
			Phase.RenderLayers = InRun->NextRenderLayers;
			Phase.CoverageItemsWhileSettling = InRun->NextCoverageItemsWhileSettling;
			InRun->NextSmallTextCoverage = TEXT("project");
			InRun->NextRenderLayers = TEXT("project");
			InRun->NextCoverageItemsWhileSettling = -1;
			TRACE_END_REGION(*BenchmarkRegion(InName));
		});
	}

	/** An int console variable set by code, when it exists. */
	void SetConsoleInt(const TCHAR* InName, int32 InValue)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(InName))
		{
			Variable->Set(InValue, ECVF_SetByCode);
		}
	}

	/**
	 * The console variables the run sets, as it found them, put back once: by the run's last step -- or, when the framework
	 * drops the queued steps of a run that was stopped, as the last step holding this goes, so that no test after it runs
	 * with coverage or render layers switched by this one. Held by the queued steps, not by the test body, which has
	 * returned long before they run.
	 */
	class FConsoleVariablesAsFound
	{
	public:
		FConsoleVariablesAsFound() = default;
		FConsoleVariablesAsFound(const FConsoleVariablesAsFound&) = delete;
		FConsoleVariablesAsFound& operator=(const FConsoleVariablesAsFound&) = delete;
		~FConsoleVariablesAsFound()
		{
			PutBack();
		}

		/** InVariable's value now, to be put back; nothing for a variable that does not exist. */
		void Remember(IConsoleVariable* InVariable)
		{
			if (InVariable != nullptr)
			{
				Found.Emplace(InVariable, InVariable->GetInt());
			}
		}

		/** Every remembered variable back to the value it had, once. */
		void PutBack()
		{
			for (const TPair<IConsoleVariable*, int32>& Entry : Found)
			{
				Entry.Key->Set(Entry.Value, ECVF_SetByCode);
			}
			Found.Reset();
		}

	private:
		TArray<TPair<IConsoleVariable*, int32>> Found;
	};

	/**
	 * One side of a coverage pair: the labels back at rest, the coverage switch and the render layers set -- the case's
	 * value, or InLayersAsFound for a case that leaves them to the project, since an earlier pair may have turned them
	 * off -- MotionSettleFrames frames for every text to repaint as the switch asks -- what they draw counted -- then
	 * MotionTimedFrames frames of the motion, timed.
	 */
	void EnqueueMotionSide(const FStageRef& InStage, const TSharedRef<FRun>& InRun, const FMotionCase& InCase, bool bInCoverageOn,
		int32 InLayersAsFound)
	{
		const FString Name = FString::Printf(TEXT("%s.Coverage%s"), InCase.Name, bInCoverageOn ? TEXT("On") : TEXT("Off"));
		const ELabelMotion Motion = InCase.Motion;
		const int32 RenderLayers = InCase.RenderLayers;
		EnqueueDo([InStage, InRun, bInCoverageOn, RenderLayers, InLayersAsFound]()
		{
			InStage->ResetLabels();
			SetConsoleInt(SmallTextCoverageVariable, bInCoverageOn ? 1 : 0);
			SetConsoleInt(RenderLayersVariable, RenderLayers >= 0 ? RenderLayers : InLayersAsFound);
			InRun->NextSmallTextCoverage = bInCoverageOn ? TEXT("on") : TEXT("off");
			InRun->NextRenderLayers = RenderLayers < 0 ? TEXT("project") : (RenderLayers > 0 ? TEXT("on") : TEXT("off"));
			DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
		});
		EnqueueFrames(InStage, MotionSettleFrames, nullptr);
		EnqueueDo([InRun]()
		{
			const DreamUIRenderStats::FSnapshot Settling = DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			InRun->NextCoverageItemsWhileSettling = Settling.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::CoverageItemsDrawn)];
		});
		EnqueuePhase(InStage, InRun, Name, [InStage, Motion](int32 InFrame) { InStage->MoveLabels(Motion, InFrame); }, MotionTimedFrames);
	}

	TSharedRef<FJsonObject> PhaseToJson(const FPhase& InPhase)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), InPhase.Name);
		Object->SetNumberField(TEXT("frames"), InPhase.Frames);
		Object->SetNumberField(TEXT("wallMsPerFrame"), InPhase.Frames > 0 ? InPhase.WallSeconds * 1000.0 / InPhase.Frames : 0.0);
		Object->SetNumberField(TEXT("drawCalls"), InPhase.DrawCalls);
		Object->SetStringField(TEXT("smallTextCoverage"), InPhase.SmallTextCoverage);
		Object->SetStringField(TEXT("renderLayers"), InPhase.RenderLayers);
		if (InPhase.CoverageItemsWhileSettling >= 0)
		{
			Object->SetNumberField(TEXT("coverageItemsWhileSettling"), static_cast<double>(InPhase.CoverageItemsWhileSettling));
		}
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
	// The two switches the label motions flip, as they were: put back when the run ends (AsFound, below).
	IConsoleVariable* const CoverageSwitch = IConsoleManager::Get().FindConsoleVariable(SmallTextCoverageVariable);
	IConsoleVariable* const LayersSwitch = IConsoleManager::Get().FindConsoleVariable(RenderLayersVariable);
	if (!TestNotNull(*FString::Printf(TEXT("The small-text coverage switch %s exists"), SmallTextCoverageVariable), CoverageSwitch))
	{
		Stage->TearDown();
		return false;
	}
	const int32 LayersBefore = LayersSwitch != nullptr ? LayersSwitch->GetInt() : 1;
	// Held by the run's last step, which puts every switch back; dropped with the queued steps of a stopped run, it puts them
	// back as it goes.
	const TSharedRef<FConsoleVariablesAsFound> AsFound = MakeShared<FConsoleVariablesAsFound>();
	AsFound->Remember(CoverageSwitch);
	AsFound->Remember(LayersSwitch);
	// The suite has every prepare a canvas makes from its last one checked against a full one (r.DreamUI.VerifyPartialPrepare,
	// in the test host's config): what is measured here is the prepare alone. Likewise every pointer kept while the count
	// of objects gone reads the same is looked up as well (r.DreamUI.VerifyKeptPointers): measured here without the look-ups.
	IConsoleVariable* const Verify = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DreamUI.VerifyPartialPrepare"));
	AsFound->Remember(Verify);
	if (Verify != nullptr)
	{
		Verify->Set(0, ECVF_SetByCode);
	}
	IConsoleVariable* const VerifyKept = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DreamUI.VerifyKeptPointers"));
	AsFound->Remember(VerifyKept);
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
	// The labels' motions, each with coverage on and then off, in this one session.
	for (const FMotionCase& Case : MotionCases)
	{
		EnqueueMotionSide(Stage, Run, Case, /*bInCoverageOn*/ true, LayersBefore);
		EnqueueMotionSide(Stage, Run, Case, /*bInCoverageOn*/ false, LayersBefore);
	}
	EnqueueDo([Stage]()
	{
		Stage->ResetLabels();
	});
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
		const int32 MotionCaseCount = UE_ARRAY_COUNT(MotionCases);
		TestEqual(TEXT("Every stretch of frames was timed"), Run->Phases.Num(), ScenePhaseCount + 2 * MotionCaseCount);
		for (const FPhase& Phase : Run->Phases)
		{
			TestTrue(FString::Printf(TEXT("%s drew: its panels made draw calls"), *Phase.Name), Phase.DrawCalls > 0);
			TestTrue(FString::Printf(TEXT("%s recorded passes on the render thread"), *Phase.Name),
				Phase.Stats.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::BatchesRecorded)] > 0);
			// A pair measures coverage against no coverage only if each side drew what its switch says: the side with
			// coverage on some coverage glyphs, settling or timed; the side with it off none while it was timed.
			const int64 TimedCoverage = Phase.Stats.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::CoverageItemsDrawn)];
			if (Phase.SmallTextCoverage == TEXT("on"))
			{
				const int64 Drawn = FMath::Max<int64>(Phase.CoverageItemsWhileSettling, 0) + TimedCoverage;
				TestTrue(FString::Printf(TEXT("%s drew from coverage glyphs (%lld item(s) while it settled, %lld while it was timed)"),
					*Phase.Name, FMath::Max<int64>(Phase.CoverageItemsWhileSettling, 0), TimedCoverage), Drawn > 0);
			}
			else if (Phase.SmallTextCoverage == TEXT("off"))
			{
				TestEqual(FString::Printf(TEXT("%s drew nothing from coverage glyphs while it was timed"), *Phase.Name), TimedCoverage, static_cast<int64>(0));
			}
		}
	});
	EnqueueDo([Stage, AsFound]()
	{
		Stage->TearDown();
		AsFound->PutBack();
	});
	return true;
}

#endif
