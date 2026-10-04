// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"
#include "PixelFormat.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISettings.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A render layer drawn, in pixels, against the same widgets drawn the way they always were.
 *
 * A layer's geometry is kept relative to it and its transform is applied in the vertex shader -- DreamGUI's built-in one,
 * or the one every UI material is drawn through -- where the canvas used to transform every vertex on the CPU. The two
 * pictures of one scene have to be the same picture: the same pixels covered, and the clip rects, which are measured in
 * canvas space, cutting at the same place whether they stand outside the layer or inside it.
 *
 * Each test draws a render-target canvas in a game world of its own, which nothing but the test ticks and nothing but the
 * canvas's own drawer renders (see DreamRenderTargetDrawerAutomationTests.cpp): the one kind of world where a layer can be
 * made that a test can read pixels back from. The level editor's world never holds a layer, since its meshes carry the
 * engine's section data for the editor's hit proxies.
 */
namespace DreamRenderLayerPixelTestLocal
{
	static constexpr int32 TargetExtent = 128;
	static constexpr uint8 ColourTolerance = 8;
	/**
	 * The two pictures are one drawing through one rasterizer; the vertices only reach it through float arithmetic done in
	 * two places, the CPU's transform for one and the vertex shader's for the other. A pixel whose centre lies right on an
	 * edge may fall either way. Nothing else may differ.
	 */
	static constexpr double AllowedEdgeFraction = 0.01;
	static constexpr int32 FramesToSettle = 4;
	/** A draw through a material waits for shaders the editor compiles only once something asks to draw with them. */
	static constexpr double ShaderWaitSeconds = 90.0;
	/** Frames a picture must stay the same, with no glyph on a font's worker, before it is believed; and how long that may take. */
	static constexpr int32 StableFrames = 3;
	static constexpr double StableWaitSeconds = 30.0;
	/**
	 * A text whose render layer moved -- a promotion counts -- draws from its field until the layer has held still for a few
	 * frames, and is then repainted from coverage glyphs by the sweep: frames enough for both, after a layer is made.
	 */
	static constexpr int32 SmallTextLayerSettleFrames = 10;
	static const FColor ClearColour = FColor(0, 0, 0, 255);
	/** The card's turn: off the canvas plane, so that it is 3D, and in it. */
	static const FRotator CardTurn = FRotator(0.0, 40.0, 20.0);

	/** Through a named local: a lambda's capture list carries commas, and the macro would cut its argument at the first. */
	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	enum class EScene : uint8
	{
		/** A card turned off the canvas plane, and a pip hanging off its corner. */
		Turned,
		/** The same inside a clipping widget whose edge the card crosses, the card clipping its pip. */
		Clipped,
		/** The turned card with a label on it whose face is painted with a gradient. */
		PaintedText,
		/** A white card flat on the canvas, on whole pixels, with a 12 px black label: small text, from coverage glyphs. */
		StillSmallText,
	};

	/** A render-target canvas in a game world of its own, a card on it, and the switches a test flips, put back when it ends. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{EWorldType::Game};
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TStrongObjectPtr<UDreamWidget> Root;
		TWeakObjectPtr<UDreamCanvas> Canvas;
		TWeakObjectPtr<UDreamWidget> Card;
		/** The text on the card, in the scenes that have one. */
		TWeakObjectPtr<UDreamText> Label;
		TOptional<bool> SavedBuiltInShader;
		TOptional<bool> SavedSmallTextCoverage;
		TUniquePtr<DreamTests::Lifecycle::FScopedConsoleVariable> LayersSwitch;
		/** The picture every layer is held to: the scene drawn with layers switched off. */
		TArray<FColor> Reference;
		FIntPoint ReferenceSize = FIntPoint::ZeroValue;
		bool bTornDown = false;

		~FStage()
		{
			TearDown();
		}

		bool Build(FAutomationTestBase& InTest, bool bInBuiltInShader, EScene InScene)
		{
			if (!InTest.TestNotNull(TEXT("a game world of its own"), World.World))
			{
				return false;
			}
			SavedBuiltInShader = GetDefault<UDreamUISettings>()->bUseBuiltInUIShader;
			GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader = bInBuiltInShader;
			// Small text from coverage glyphs, as the project's default has it, whatever the host's config says.
			SavedSmallTextCoverage = GetDefault<UDreamGUISettings>()->bSmallTextCoverage;
			GetMutableDefault<UDreamGUISettings>()->bSmallTextCoverage = true;
			SwitchLayers(false);

			UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			NewTarget->ClearColor = FLinearColor::Black;
			NewTarget->InitCustomFormat(TargetExtent, TargetExtent, EPixelFormat::PF_B8G8R8A8, false);
			NewTarget->UpdateResourceImmediate(true);
			Target.Reset(NewTarget);

			UDreamWidget* NewRoot = MakeWidget(nullptr, FVector2D(TargetExtent, TargetExtent), FVector2D::ZeroVector);
			Root.Reset(NewRoot);
			UDreamCanvas* NewCanvas = NewRoot->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("a canvas on the root"), NewCanvas))
			{
				return false;
			}
			Canvas = NewCanvas;
			NewCanvas->SetRenderMode(EDreamRenderMode::RenderTarget);
			NewCanvas->SetRenderTargetClearColor(ClearColour);
			NewCanvas->SetRenderTargetResolutionScale(1.0f);
			NewCanvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
			NewCanvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
			NewCanvas->SetRenderTarget(NewTarget);

			UDreamWidget* CardParent = NewRoot;
			if (InScene == EScene::Clipped)
			{
				// A clip outside the layer: the card crosses its right edge.
				UDreamWidget* Clipper = MakeWidget(NewRoot, FVector2D(64.0, 64.0), FVector2D(-10.0, 0.0));
				Clipper->SetClipping(EDreamWidgetClipping::ClipToBounds);
				CardParent = Clipper;
			}
			if (InScene == EScene::StillSmallText)
			{
				// Flat and on whole pixels -- its edges at (46, 40) and (106, 80) of the target -- so that small text on it
				// is drawn from coverage glyphs, which only a still, unturned layer keeps.
				UDreamWidget* FlatCard = MakeBlock(CardParent, FVector2D(60.0, 40.0), FVector2D(12.0, 4.0), FColor::White);
				Label = MakeLabel(FlatCard, FVector2D(56.0, 24.0), TEXT("Aa 12px"), 12.0f, FColor::Black);
				Card = FlatCard;
				return true;
			}
			UDreamWidget* NewCard = MakeBlock(CardParent, FVector2D(60.0, 40.0), FVector2D(12.0, 4.0), FColor::Red);
			NewCard->SetRenderRotation(CardTurn);
			// Hanging off the card's corner, past its edge.
			MakeBlock(NewCard, FVector2D(30.0, 20.0), FVector2D(22.0, 12.0), FColor::Green);
			if (InScene == EScene::Clipped)
			{
				// And a clip inside it: the card cuts off what hangs off it.
				NewCard->SetClipping(EDreamWidgetClipping::ClipToBounds);
			}
			if (InScene == EScene::PaintedText)
			{
				// A paint is vertex data (the quads' UV4) and rows the shader reads: the layer's transform moves the quads
				// and leaves both as they are.
				Label = MakeLabel(NewCard, FVector2D(56.0, 30.0), TEXT("Paint"), 20.0f, FColor::White);
				if (UDreamText* PaintedLabel = Label.Get())
				{
					FDreamTextPaint Paint;
					Paint.bEnabled = true;
					InTest.TestTrue(TEXT("the label's gradient reads"),
						FDreamGradient::ParseCss(TEXT("linear-gradient(90deg, #FFCC00, #34C759 50%, #0A84FF)"), Paint.Gradient));
					PaintedLabel->SetFacePaint(Paint);
				}
			}
			Card = NewCard;
			return true;
		}

		/** A centred one-line label of InSize on InParent, its text InText at InFontSize in InColour. */
		UDreamText* MakeLabel(UDreamWidget* InParent, const FVector2D& InSize, const TCHAR* InText, float InFontSize, const FColor& InColour)
		{
			UDreamWidget* Widget = MakeWidget(InParent, InSize, FVector2D::ZeroVector);
			UDreamText* Text = Widget->CreateNewVisual<UDreamText>();
			if (Text != nullptr)
			{
				Text->SetText(FText::FromString(InText));
				Text->SetFontSize(InFontSize);
				Text->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
				Text->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
				Text->SetOverflowType(EDreamUITextOverflowType::HorizontalOverflow);
				Text->SetColor(InColour);
			}
			return Text;
		}

		/** Glyphs the stage's texts have on their fonts' workers, each such font first made to finish them. */
		int32 FinishPendingGlyphs() const
		{
			int32 Pending = 0;
			UDreamWidget* RootWidget = Root.Get();
			if (!IsValid(RootWidget))
			{
				return Pending;
			}
			TArray<UDreamWidget*> Widgets;
			UDreamWidget::CollectChildrenWidgets(RootWidget, Widgets, true);
			for (UDreamWidget* Widget : Widgets)
			{
				const UDreamText* Text = IsValid(Widget) ? Cast<UDreamText>(Widget->GetVisual()) : nullptr;
				UDreamUIFontData_FreeTypeRender* Font = Text != nullptr ? Cast<UDreamUIFontData_FreeTypeRender>(Text->GetFont()) : nullptr;
				if (Font != nullptr && Font->GetPendingAsyncGlyphCount() > 0)
				{
					Font->WaitForAsyncGlyphs();
					Pending += Font->GetPendingAsyncGlyphCount();
				}
			}
			return Pending;
		}

		UDreamWidget* MakeWidget(UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InPosition)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			Widget->SetWidth(static_cast<float>(InSize.X));
			Widget->SetHeight(static_cast<float>(InSize.Y));
			Widget->OnRegister();
			if (InParent != nullptr)
			{
				Widget->TrySetParent(InParent, false);
				Widget->SetAnchoredPosition(InPosition);
			}
			return Widget;
		}

		UDreamWidget* MakeBlock(UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InPosition, const FColor& InColour)
		{
			UDreamWidget* Widget = MakeWidget(InParent, InSize, InPosition);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetColor(InColour);
			}
			return Widget;
		}

		/** r.DreamUI.RenderLayers on or off from here, and back to what it was when the stage is torn down. */
		void SwitchLayers(bool bInOn)
		{
			LayersSwitch.Reset();
			LayersSwitch = MakeUnique<DreamTests::Lifecycle::FScopedConsoleVariable>(TEXT("r.DreamUI.RenderLayers"), bInOn ? 1 : 0);
		}

		/** One frame of this world alone: its UI manager ticked, its end-of-frame updates sent, its target drawn. */
		void Frame()
		{
			if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World))
			{
				Manager->Tick(1.0f / 30.0f);
			}
			World.World->SendAllEndOfFrameUpdates();
		}

		/** The pixel (0,0 at the top left) that shows InLocal, a point in InWidget's own plane. */
		FIntPoint PixelOf(const UDreamWidget* InWidget, FVector2D InLocal) const
		{
			const UDreamCanvas* RootCanvas = Canvas.Get();
			if (RootCanvas == nullptr || InWidget == nullptr)
			{
				return FIntPoint(-1, -1);
			}
			const FVector World3D = InWidget->GetWorldTransform().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y));
			FVector2D CanvasPoint = FVector2D::ZeroVector;
			FVector2D ViewportPoint = FVector2D::ZeroVector;
			if (!RootCanvas->Project3DToScreen(World3D, CanvasPoint) || !RootCanvas->ConvertPositionFromCanvasToViewport(CanvasPoint, ViewportPoint))
			{
				return FIntPoint(-1, -1);
			}
			return ViewportPoint.IntPoint();
		}

		bool Read(TArray<FColor>& OutPixels, FIntPoint& OutSize) const
		{
			return FDreamPixelProbe::ReadBack(Target.Get(), OutPixels, OutSize);
		}

		/** Whether the card's middle shows anything but the clear colour yet. */
		bool CardShows() const
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			const FIntPoint Middle = PixelOf(Card.Get(), FVector2D::ZeroVector);
			if (!Read(Pixels, Size) || Middle.X < 0 || Middle.Y < 0 || Middle.X >= Size.X || Middle.Y >= Size.Y)
			{
				return false;
			}
			return !FDreamPixelProbe::IsNear(Pixels[Middle.Y * Size.X + Middle.X], ClearColour, ColourTolerance);
		}

		/** Idempotent; the destructor calls it too. */
		void TearDown()
		{
			if (bTornDown)
			{
				return;
			}
			bTornDown = true;
			if (UDreamWidget* RootWidget = Root.Get(); IsValid(RootWidget))
			{
				RootWidget->DestroyWidget();
			}
			Root.Reset();
			Target.Reset();
			LayersSwitch.Reset();
			if (SavedBuiltInShader.IsSet())
			{
				GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader = SavedBuiltInShader.GetValue();
			}
			if (SavedSmallTextCoverage.IsSet())
			{
				GetMutableDefault<UDreamGUISettings>()->bSmallTextCoverage = SavedSmallTextCoverage.GetValue();
			}
		}
	};

	using FStageRef = TSharedRef<FStage>;

	/** How a comparison is held: the frames a layer is given to settle, and how near the two pictures must be. */
	struct FComparison
	{
		int32 LayerSettleFrames = FramesToSettle;
		uint8 Tolerance = ColourTolerance;
		double AllowedFraction = AllowedEdgeFraction;
		/**
		 * The card's label is drawn from coverage glyphs in both pictures, as its own last paint says: the gate's answer and
		 * the items the painter drew from coverage (UDreamText::GetSmallTextState, GetSmallTextReport).
		 */
		bool bExpectCoverage = false;
	};

	/** Whether the stage's label was last painted from coverage glyphs, and a few words on what it was painted from. */
	bool IsLabelOnCoverage(const FStage& InStage, FString& OutWhat)
	{
		const UDreamText* Text = InStage.Label.Get();
		if (Text == nullptr)
		{
			OutWhat = TEXT("there is no label");
			return false;
		}
		const EDreamTextSmallTextGate Gate = Text->GetSmallTextState().Gate;
		const int32 CoverageItems = Text->GetSmallTextReport().CoverageItems;
		OutWhat = FString::Printf(TEXT("gate %d (%d is coverage), %d item(s) from coverage glyphs"), static_cast<int32>(Gate),
			static_cast<int32>(EDreamTextSmallTextGate::Coverage), CoverageItems);
		return Gate == EDreamTextSmallTextGate::Coverage && CoverageItems > 0;
	}

	/**
	 * Frames until the picture has been the same StableFrames times running with no glyph pending on a font's worker, or
	 * StableWaitSeconds: glyphs rasterise on workers and land a few frames after the text first asks for them.
	 */
	void EnqueueUntilStable(const FStageRef& InStage)
	{
		struct FStableState
		{
			double Deadline = 0.0;
			int32 Same = 0;
			TArray<FColor> Last;
		};
		const TSharedRef<FStableState> State = MakeShared<FStableState>();
		EnqueueStep([InStage, State]()
		{
			if (State->Deadline == 0.0)
			{
				State->Deadline = FPlatformTime::Seconds() + StableWaitSeconds;
			}
			InStage->Frame();
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			const bool bRead = InStage->Read(Pixels, Size);
			const bool bPending = InStage->FinishPendingGlyphs() > 0;
			State->Same = bRead && !bPending && Pixels == State->Last ? State->Same + 1 : 0;
			State->Last = MoveTemp(Pixels);
			return State->Same >= StableFrames || FPlatformTime::Seconds() > State->Deadline;
		});
	}

	/**
	 * What every test here does. The stage drawn with layers switched off -- once the card shows, which a material draw
	 * waits for, and the picture holds still -- is the reference. Then InToLayer makes the card a layer, once per frame
	 * for InLayerFrames frames with the frame's index, the frames settle, and the picture is held to the reference.
	 */
	void EnqueueComparison(FAutomationTestBase* InTest, const FStageRef& InStage, const FString& InName,
		TFunction<void(FStage&, int32)> InToLayer, int32 InLayerFrames, const FComparison& InComparison = FComparison())
	{
		const TSharedRef<double> Deadline = MakeShared<double>(0.0);
		EnqueueStep([InStage, Deadline]()
		{
			if (*Deadline == 0.0)
			{
				*Deadline = FPlatformTime::Seconds() + ShaderWaitSeconds;
			}
			InStage->Frame();
			return InStage->CardShows() || FPlatformTime::Seconds() > *Deadline;
		});
		const TSharedRef<int32> Settle = MakeShared<int32>(FramesToSettle);
		EnqueueStep([InStage, Settle]()
		{
			InStage->Frame();
			return --(*Settle) <= 0;
		});
		EnqueueUntilStable(InStage);
		EnqueueStep([InTest, InStage, InName, InComparison]()
		{
			if (InTest->TestTrue(FString::Printf(TEXT("%s: the canvas drawn on the CPU path reads back"), *InName), InStage->Read(InStage->Reference, InStage->ReferenceSize)))
			{
				FDreamPixelProbe::SaveCapture(InStage->Reference, InStage->ReferenceSize, InName + TEXT("_Cpu"));
				InTest->TestTrue(FString::Printf(TEXT("%s: the card is drawn at all"), *InName), InStage->CardShows());
				InTest->TestFalse(FString::Printf(TEXT("%s: the card is no layer while layers are off"), *InName),
					InStage->Card.IsValid() && InStage->Card->IsRenderLayer());
			}
			if (InComparison.bExpectCoverage)
			{
				FString What;
				const bool bCoverage = IsLabelOnCoverage(*InStage, What);
				InTest->TestTrue(FString::Printf(TEXT("%s: with no layer, the label is drawn from coverage glyphs (%s)"), *InName, *What), bCoverage);
			}
			InStage->SwitchLayers(true);
			DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			return true;
		});
		const TSharedRef<int32> LayerFrame = MakeShared<int32>(0);
		EnqueueStep([InStage, LayerFrame, InToLayer, InLayerFrames]()
		{
			// One call and one frame per engine frame: the frame counter a layer is made by moves on between them.
			InToLayer(*InStage, *LayerFrame);
			InStage->Frame();
			return ++(*LayerFrame) >= InLayerFrames;
		});
		const TSharedRef<int32> Settled = MakeShared<int32>(FMath::Max(InComparison.LayerSettleFrames, 1));
		EnqueueStep([InStage, Settled]()
		{
			InStage->Frame();
			return --(*Settled) <= 0;
		});
		EnqueueUntilStable(InStage);
		EnqueueStep([InTest, InStage, InName, InComparison]()
		{
			const DreamUIRenderStats::FSnapshot Counted = DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
			InTest->TestTrue(FString::Printf(TEXT("%s: the card is a layer"), *InName), InStage->Card.IsValid() && InStage->Card->IsRenderLayer());
			InTest->TestTrue(FString::Printf(TEXT("%s: a layer was made"), *InName),
				Counted.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::RenderLayerPromotions)] > 0);
			if (InComparison.bExpectCoverage)
			{
				// The label's own last paint, not the counters: a promotion that leaves the layer where its widget was may
				// keep the label's coverage quads (nothing to repaint), or repaint it from its field and then the sweep from
				// coverage again; either way it ends on coverage glyphs.
				FString What;
				const bool bCoverage = IsLabelOnCoverage(*InStage, What);
				const int64 CoverageItems = Counted.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::CoverageItemsDrawn)];
				InTest->TestTrue(FString::Printf(TEXT("%s: once the layer held still, its label is drawn from coverage glyphs (%s; %lld item(s) drawn from coverage since the layer was made)"),
					*InName, *What, CoverageItems), bCoverage);
			}
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (InTest->TestTrue(FString::Printf(TEXT("%s: the canvas drawn with its layer reads back"), *InName), InStage->Read(Pixels, Size)))
			{
				FDreamPixelProbe::SaveCapture(Pixels, Size, InName + TEXT("_Layer"));
				FDreamPixelProbe::ExpectPicturesMatch(*InTest, Pixels, Size, InStage->Reference, InStage->ReferenceSize, InName,
					InComparison.Tolerance, InComparison.AllowedFraction);
			}
			InStage->TearDown();
			return true;
		});
	}

	/** Made a layer at once, standing still. */
	void MakeCardALayer(FStage& InStage, int32 InFrame)
	{
		if (InFrame == 0 && InStage.Card.IsValid())
		{
			InStage.Card->SetRenderLayerMode(EDreamWidgetRenderLayer::Always);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRenderLayerBuiltInTest,
	"DreamGUI.RHI.ALayerDrawsATurnedCardOnThePixelsTheCpuPathDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRenderLayerBuiltInTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerPixelTestLocal;
	const FStageRef Stage = MakeShared<FStage>();
	if (!Stage->Build(*this, /*bInBuiltInShader*/ true, EScene::Turned))
	{
		Stage->TearDown();
		return false;
	}
	EnqueueComparison(this, Stage, TEXT("RenderLayer_BuiltIn"), &MakeCardALayer, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRenderLayerMaterialTest,
	"DreamGUI.RHI.ALayerDrawnThroughAMaterialCoversThePixelsTheCpuPathDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRenderLayerMaterialTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerPixelTestLocal;
	// The canvas's default material, drawn through the vertex shader every UI material is drawn through: the layer's
	// transform is applied there, ahead of the primitive's, which stays the canvas's.
	const FStageRef Stage = MakeShared<FStage>();
	if (!Stage->Build(*this, /*bInBuiltInShader*/ false, EScene::Turned))
	{
		Stage->TearDown();
		return false;
	}
	EnqueueComparison(this, Stage, TEXT("RenderLayer_Material"), &MakeCardALayer, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRenderLayerTurnedFrameByFrameTest,
	"DreamGUI.RHI.ALayerTurnedFrameByFrameEndsOnThePixelsTheCpuPathDraws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRenderLayerTurnedFrameByFrameTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerPixelTestLocal;
	// Made a layer by turning, as an animation makes one, and turned on after that: the last turns only move its sections,
	// and where they end up has to be where the CPU path draws the card at the same turn.
	const FStageRef Stage = MakeShared<FStage>();
	if (!Stage->Build(*this, /*bInBuiltInShader*/ true, EScene::Turned))
	{
		Stage->TearDown();
		return false;
	}
	constexpr int32 Turns = 5;
	EnqueueComparison(this, Stage, TEXT("RenderLayer_Turned"), [](FStage& InStage, int32 InFrame)
	{
		if (UDreamWidget* Card = InStage.Card.Get())
		{
			// Towards the reference's turn, one step a frame, ending on it.
			const double Remaining = static_cast<double>(Turns - 1 - InFrame);
			Card->SetRenderRotation(FRotator(CardTurn.Pitch, CardTurn.Yaw - 8.0 * Remaining, CardTurn.Roll));
		}
	}, Turns);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRenderLayerClippedTest,
	"DreamGUI.RHI.ALayersContentIsClippedWhereTheCpuPathClipsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRenderLayerClippedTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerPixelTestLocal;
	// The clip rects are measured in canvas space, so a layer's vertices are taken to canvas space before the clip is read:
	// both the clip of a widget outside the layer and that of one inside it cut where they always did.
	const FStageRef Stage = MakeShared<FStage>();
	if (!Stage->Build(*this, /*bInBuiltInShader*/ true, EScene::Clipped))
	{
		Stage->TearDown();
		return false;
	}
	EnqueueComparison(this, Stage, TEXT("RenderLayer_Clipped"), &MakeCardALayer, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRenderLayerPaintedTextTest,
	"DreamGUI.RHI.ALayerPaintsTextOnThePixelsTheCpuPathDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRenderLayerPaintedTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerPixelTestLocal;
	// The turned card with a label painted with a gradient, drawn by the built-in shader. Where a quad's gradient stands is
	// its UV4, written in the text's own space before any transform, so the layer moving the quads in the vertex shader
	// paints them exactly where and as the CPU path does.
	const FStageRef Stage = MakeShared<FStage>();
	if (!Stage->Build(*this, /*bInBuiltInShader*/ true, EScene::PaintedText))
	{
		Stage->TearDown();
		return false;
	}
	EnqueueComparison(this, Stage, TEXT("RenderLayer_PaintedText"), &MakeCardALayer, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRenderLayerPaintedTextMaterialTest,
	"DreamGUI.RHI.ALayerDrawnThroughAMaterialPaintsTextOnThePixelsTheCpuPathDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRenderLayerPaintedTextMaterialTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerPixelTestLocal;
	// The same through the canvas's default material: UV4 reaches it as TexCoord(4), the paint rows as its paint texture --
	// or, for a material without them, the paint is in the vertex colours. Either way, both paths draw the same.
	const FStageRef Stage = MakeShared<FStage>();
	if (!Stage->Build(*this, /*bInBuiltInShader*/ false, EScene::PaintedText))
	{
		Stage->TearDown();
		return false;
	}
	// A default material made before MF_DreamUI_Shade had the paint rows has no shading marker: the label is then painted in
	// its vertex colours on both paths, which holds them alike all the same but not through the paint rows. Said, so that
	// the run reads as what it was.
	const UDreamCanvas* StageCanvas = Stage->Canvas.Get();
	const UMaterialInterface* DefaultMaterial = StageCanvas != nullptr ? StageCanvas->GetDefaultMaterial() : nullptr;
	float Marker = 0.0f;
	if (DefaultMaterial != nullptr
		&& !(DefaultMaterial->GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(DreamUIShadeMaterial::ShadeMarkerParameter), Marker) && Marker > 0.5f))
	{
		AddInfo(FString::Printf(TEXT("RenderLayer_PaintedTextMaterial: the default UI material %s has no %s parameter, so the label's paint is in its vertex colours on both paths, not read from the paint rows; regenerate MF_DreamUI_Shade and DreamUI_ImageAndFont from DShader/ to test those."),
			*DefaultMaterial->GetPathName(), *DreamUIShadeMaterial::ShadeMarkerParameter.ToString()));
	}
	EnqueueComparison(this, Stage, TEXT("RenderLayer_PaintedTextMaterial"), &MakeCardALayer, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRenderLayerStillSmallTextTest,
	"DreamGUI.RHI.SmallTextInAStillLayerIsThePixelsOfTheSameTextOutsideALayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRenderLayerStillSmallTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderLayerPixelTestLocal;
	// A flat card with 12 px text, drawn from coverage glyphs, made a layer at once and then held still. Being made a layer
	// counts as a move: the text draws from its field until the layer has been still a few frames, and is then painted
	// from coverage glyphs again, measured on the device's pixel grid through the layer's place -- which, the layer being
	// flat and on whole pixels, is where the CPU path put the glyphs. The two pictures are the same, to a code.
	const FStageRef Stage = MakeShared<FStage>();
	if (!Stage->Build(*this, /*bInBuiltInShader*/ true, EScene::StillSmallText))
	{
		Stage->TearDown();
		return false;
	}
	FComparison Comparison;
	Comparison.LayerSettleFrames = SmallTextLayerSettleFrames;
	Comparison.Tolerance = 1;
	Comparison.AllowedFraction = 0.0;
	Comparison.bExpectCoverage = true;
	EnqueueComparison(this, Stage, TEXT("RenderLayer_StillSmallText"), &MakeCardALayer, 1, Comparison);
	return true;
}

#endif
