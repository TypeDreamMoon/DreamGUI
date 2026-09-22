// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "PixelFormat.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"

/*
 * The only tests in this module that look at pixels a GPU produced.
 *
 * WHERE THE PICTURE COMES FROM, because it decides everything else here. A RenderTarget canvas is
 * drawn by FDreamUIRenderer, which is a SCENE VIEW EXTENSION: the canvas pushes its target to the
 * render thread from the UI manager's tick, and the pixels are written in the extension's
 * PostRenderView_RenderThread -- inside somebody else's scene render, gated on that render being of
 * this canvas's world. Nothing in that chain can be called directly, and the last step belongs to a
 * view that a test does not own.
 *
 * WHICH WORLD. That is why these tests use the EDITOR's world rather than a world of their own. A
 * UWorld::CreateWorld world -- the one every other fixture in this module uses -- is never rendered
 * by anything: it has no viewport, so no view family is ever built for it, so the extension's
 * IsActiveThisFrame_Internal never matches and PostRenderView never runs. It would tick, lay out,
 * build draw calls and produce an empty target forever. The editor's world is rendered by the level
 * viewport, and its UI manager is driven by the ticker the subsystem installs for editor worlds, so
 * both halves of the chain run without this fixture arranging either. The cost is that the world is
 * shared: everything built here is transient and is destroyed again at the end of the test.
 *
 * WHY FRAMES. The work is spread across a tick and a scene render and an async draw-call builder, so
 * a target read in the same frame it was changed shows the frame before. Pumping is what closes that,
 * and the read itself needs no extra frame beyond the render-thread flush inside the probe.
 *
 * Every test here carries NonNullRHI. Under -nullrhi there is no render target resource to read and
 * the framework skips them, which is what keeps the ordinary headless suite honest about what it
 * covers.
 */

namespace DreamRenderTargetPixelTestsLocal
{
	/**
	 * Big enough to leave clear margin on every side of the block and small enough that reading the
	 * whole thing back on every check costs nothing worth measuring.
	 */
	static constexpr int32 TargetExtent = 256;
	/** A block big enough that a wrong-by-a-few-pixels answer still lands inside it. */
	static constexpr int32 BlockExtent = 100;
	/**
	 * Per channel. Pure red on black survives any gamma the path applies -- 0 and 1 are fixed points
	 * of every transfer function -- so this covers rounding and nothing else.
	 */
	static constexpr uint8 ColorTolerance = 8;
	/**
	 * Frames to let pass after changing something before believing the target.
	 *
	 * One frame is not enough by construction: the canvas hands its prepared draw-call data to a
	 * worker thread and picks the result up on a later tick, and the scene render that consumes the
	 * target trails the tick that pushed it. Three is the smallest count that covers both hops with
	 * one to spare, and it is the one number to raise if a whole file's worth of these ever fails
	 * with an empty or stale target.
	 */
	static constexpr int32 FramesToSettle = 3;

	/** Opaque, so a pixel that was never written and one that was cleared are distinguishable. */
	static const FColor ClearColour = FColor(0, 0, 0, 255);
	static const FColor BlockColour = FColor(255, 0, 0, 255);

	/**
	 * A root canvas rendering to a 256x256 target, with one solid red 100x100 block in the middle.
	 *
	 * Built in the editor's world and taken down again at the end, whatever happened in between --
	 * a leaked widget tree would keep ticking, keep its canvas registered with the renderer, and be
	 * found by the next test that walks the manager's lists.
	 *
	 * Held by shared reference because the steps that use it run one per frame, long after RunTest
	 * has returned; the strong object pointers inside are what keeps the tree from being collected
	 * between those frames.
	 */
	class FRenderTargetStage
	{
	public:
		explicit FRenderTargetStage(FAutomationTestBase& InTest);
		~FRenderTargetStage();

		FRenderTargetStage(const FRenderTargetStage&) = delete;
		FRenderTargetStage& operator=(const FRenderTargetStage&) = delete;

		/** Whether the whole stage came up. A step should say so and stop rather than act on half of one. */
		bool IsUsable() const;
		/** Why it did not, in a sentence that names the missing piece. */
		const FString& GetFailure() const { return Failure; }

		/** Destroy the tree and let the target go. Idempotent; the destructor calls it too. */
		void TearDown();

		/**
		 * Ask for one more scene render of this world.
		 *
		 * An editor viewport only redraws when something invalidates it, and a headless run has
		 * nobody moving a mouse over one. Without this the world is ticked, the canvas pushes its
		 * target every frame, and no view is ever built to consume it -- the target stays exactly as
		 * it was when the editor last had a reason to draw.
		 */
		void RequestRedraw() const;

		bool ReadBack(TArray<FColor>& OutPixels, FIntPoint& OutSize) const;

		/**
		 * Where the block's centre is on the target right now, in pixels with (0,0) at the top left.
		 *
		 * Through the canvas's own two conversions rather than arithmetic of this file's own: the
		 * projection and the canvas-to-viewport flip are what the runtime uses to decide where a
		 * point on screen is, so an expectation built from them is the renderer's own claim about
		 * where it put the block, and a test that disagrees disagrees about the picture rather than
		 * about a convention.
		 */
		FIntPoint BlockCentrePixel() const;

		/** The middle of the target, which is where a centred block is expected to be. */
		static FIntPoint TargetCentrePixel() { return FIntPoint(TargetExtent / 2, TargetExtent / 2); }
		/** The whole image, for a count that is about the picture rather than a corner of it. */
		static FIntRect WholeTarget() { return FIntRect(0, 0, TargetExtent, TargetExtent); }

		FAutomationTestBase& GetTest() const { return Test; }
		UDreamWidget* GetBlock() const { return BlockWidget.Get(); }
		UDreamCanvas* GetCanvas() const { return CanvasComponent.Get(); }
		UTextureRenderTarget2D* GetTarget() const { return TargetTexture.Get(); }

	private:
		FAutomationTestBase& Test;
		FString Failure;
		UWorld* EditorWorld = nullptr;
		TStrongObjectPtr<UTextureRenderTarget2D> TargetTexture;
		TStrongObjectPtr<UDreamWidget> RootWidget;
		TStrongObjectPtr<UDreamWidget> BlockWidget;
		TStrongObjectPtr<UDreamCanvas> CanvasComponent;
		bool bTornDown = false;
	};

	FRenderTargetStage::FRenderTargetStage(FAutomationTestBase& InTest)
		: Test(InTest)
	{
		if (GEditor == nullptr)
		{
			Failure = TEXT("there is no editor engine, so nothing renders this world");
			return;
		}
		EditorWorld = GEditor->GetEditorWorldContext().World();
		if (EditorWorld == nullptr)
		{
			Failure = TEXT("the editor has no world to build the canvas in");
			return;
		}
		if (GEditor->GetAllViewportClients().Num() == 0)
		{
			// Said here rather than discovered four pixel mismatches later. With no viewport there is
			// no view family, so the canvas's view extension is never asked to draw and the target
			// stays whatever it was -- which from the game thread looks exactly like a renderer that
			// produced nothing.
			Failure = TEXT("the editor has no viewport, so nothing ever renders its world");
			return;
		}

		// The target first: a canvas switched to RenderTarget mode derives its viewport size, and
		// therefore its rect and its projection, from the texture, so it has to exist before the
		// canvas is asked what size it is.
		UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
		Target->AddressX = TextureAddress::TA_Clamp;
		Target->AddressY = TextureAddress::TA_Clamp;
		Target->ClearColor = FLinearColor::Black;
		// The same format the canvas would have chosen had it made the target itself, so nothing here
		// provokes the re-initialisation path.
		Target->InitCustomFormat(static_cast<uint32>(TargetExtent), static_cast<uint32>(TargetExtent), EPixelFormat::PF_B8G8R8A8, false);
		Target->UpdateResourceImmediate(true);
		TargetTexture.Reset(Target);

		// Transient, and never transactional: this tree is built in the world the editor would save,
		// and an object that is neither is one the map can carry away with it.
		UDreamWidget* Root = NewObject<UDreamWidget>(EditorWorld, NAME_None, RF_Transient);
		Root->SetDisplayName(TEXT("DreamPixelProbeRoot"));
		Root->SetWidth(static_cast<float>(TargetExtent));
		Root->SetHeight(static_cast<float>(TargetExtent));
		Root->OnRegister();
		RootWidget.Reset(Root);

		UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
		if (Canvas == nullptr)
		{
			Failure = TEXT("the root widget would not take a canvas");
			return;
		}
		CanvasComponent.Reset(Canvas);
		Canvas->SetRenderMode(EDreamRenderMode::RenderTarget);
		Canvas->SetRenderTargetClearColor(ClearColour);
		Canvas->SetRenderTargetResolutionScale(1.0f);
		// CanvasFitToRenderTarget, so the texture is the fixed thing and the canvas rect follows it.
		// The other way round the canvas would resize the texture from its own width, and a test that
		// moves a widget could end up changing the size of the image it is measuring.
		Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
		// Always, rather than the default Automatic: Automatic pushes the target to the render thread
		// only on the frames the canvas thinks something changed, and the extension draws nothing on a
		// frame it was not pushed -- leaving whatever the last drawn frame left. Always makes every
		// frame's picture the current one, which is the only way "wait three frames and look" means
		// what it says.
		Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
		Canvas->SetRenderTarget(Target);

		UDreamWidget* Block = NewObject<UDreamWidget>(EditorWorld, NAME_None, RF_Transient);
		Block->SetDisplayName(TEXT("DreamPixelProbeBlock"));
		Block->SetWidth(static_cast<float>(BlockExtent));
		Block->SetHeight(static_cast<float>(BlockExtent));
		Block->OnRegister();
		Block->TrySetParent(Root, false);
		// Centred: the default anchor is the middle of the parent, so zero is the middle of the canvas.
		Block->SetAnchoredPosition(FVector2D::ZeroVector);
		BlockWidget.Reset(Block);

		// Last, once the widget has a parent and therefore a canvas to be enrolled with.
		UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>();
		if (Visual == nullptr)
		{
			Failure = TEXT("the block widget would not take a texture visual");
			return;
		}
		// A white texture tinted red is the shortest way to a solid colour that needs no content of
		// its own -- it is the same texture the visual falls back to when it is given none, named
		// here so the test says what it is drawing.
		Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
		Visual->SetColor(BlockColour);
	}

	FRenderTargetStage::~FRenderTargetStage()
	{
		TearDown();
	}

	bool FRenderTargetStage::IsUsable() const
	{
		return Failure.IsEmpty()
			&& EditorWorld != nullptr
			&& IsValid(TargetTexture.Get())
			&& IsValid(RootWidget.Get())
			&& IsValid(CanvasComponent.Get())
			&& IsValid(BlockWidget.Get());
	}

	void FRenderTargetStage::TearDown()
	{
		if (bTornDown)
		{
			return;
		}
		bTornDown = true;
		// The tree through its own verb and while the world is still whole: destroying the root
		// unregisters the canvas from the renderer and the widgets from the manager's lists, which is
		// where they expect to be told. Dropping the references without it would leave both.
		if (UDreamWidget* Root = RootWidget.Get(); IsValid(Root))
		{
			Root->DestroyWidget();
		}
		BlockWidget.Reset();
		CanvasComponent.Reset();
		RootWidget.Reset();
		TargetTexture.Reset();
		EditorWorld = nullptr;
	}

	void FRenderTargetStage::RequestRedraw() const
	{
		if (GEditor != nullptr)
		{
			// Hit proxies are not wanted: nothing here picks anything, and building them is the
			// expensive half of an editor redraw.
			GEditor->RedrawAllViewports(false);
		}
	}

	bool FRenderTargetStage::ReadBack(TArray<FColor>& OutPixels, FIntPoint& OutSize) const
	{
		return FDreamPixelProbe::ReadBack(TargetTexture.Get(), OutPixels, OutSize);
	}

	FIntPoint FRenderTargetStage::BlockCentrePixel() const
	{
		UDreamCanvas* Canvas = CanvasComponent.Get();
		UDreamWidget* Block = BlockWidget.Get();
		if (!IsValid(Canvas) || !IsValid(Block))
		{
			return FIntPoint(-1, -1);
		}
		FVector2D CanvasPoint = FVector2D::ZeroVector;
		if (!Canvas->Project3DToScreen(Block->GetWorldLocation(), CanvasPoint))
		{
			return FIntPoint(-1, -1);
		}
		FVector2D ViewportPoint = FVector2D::ZeroVector;
		if (!Canvas->ConvertPositionFromCanvasToViewport(CanvasPoint, ViewportPoint))
		{
			return FIntPoint(-1, -1);
		}
		return ViewportPoint.IntPoint();
	}

	using FStageRef = TSharedRef<FRenderTargetStage>;

	/**
	 * Queue one step. Through a named local rather than straight into the macro: a lambda's capture
	 * list carries commas, and the preprocessor would cut the macro argument at the first one.
	 */
	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	/**
	 * Let FramesToSettle frames pass, asking for a scene render on each of them.
	 *
	 * One latent Update is one engine frame, so counting Updates is counting frames. The redraw goes
	 * out on every one of them rather than only the first: the canvas pushes its target to the render
	 * thread once per tick and the push is consumed by the next render of this world, so a single
	 * redraw at the start would consume a target prepared before the change this is waiting on.
	 *
	 * The last redraw still YIELDS rather than finishing the step, and that is the whole reason this
	 * counts to zero on one Update and reports done on the next. A latent command that returns true
	 * is dequeued and the following command runs in the SAME tick, which for the last frame would put
	 * the assertion before the redraw it just asked for had been painted -- a wait of three frames
	 * that was really a wait of two.
	 */
	void EnqueueSettledFrames(const FStageRef& InStage)
	{
		TSharedRef<int32> Remaining = MakeShared<int32>(FramesToSettle);
		TFunction<bool()> Step = [InStage, Remaining]()
		{
			if (*Remaining <= 0)
			{
				return true;
			}
			InStage->RequestRedraw();
			--(*Remaining);
			return false;
		};
		EnqueueStep(Step);
	}

	/** Queue an assertion to run once, a frame after whatever came before it. */
	void EnqueueCheck(TFunction<void()> InCheck)
	{
		TFunction<bool()> Step = [InCheck]()
		{
			InCheck();
			return true;
		};
		EnqueueStep(Step);
	}

	/**
	 * Take the stage down, whatever the steps before did.
	 *
	 * Queued rather than left to the shared reference going out of scope: a failed check does not
	 * stop the queue, and the editor's world must not be left carrying a widget tree either way.
	 */
	void EnqueueTearDown(const FStageRef& InStage)
	{
		TFunction<bool()> Step = [InStage]()
		{
			InStage->TearDown();
			return true;
		};
		EnqueueStep(Step);
	}

	/**
	 * Start a test: build the stage and report if it did not come up.
	 *
	 * Returns an unusable stage rather than null so a caller can still tear it down -- half a stage
	 * is still a widget tree in the editor's world.
	 */
	FStageRef BeginStage(FAutomationTestBase& InTest)
	{
		FStageRef Stage = MakeShared<FRenderTargetStage>(InTest);
		if (!Stage->IsUsable())
		{
			InTest.AddError(FString::Printf(TEXT("The render-target stage did not come up: %s."), *Stage->GetFailure()));
		}
		return Stage;
	}

	/** Read the target and assert one pixel of it, naming what that pixel is supposed to be. */
	void CheckPixel(const FStageRef& InStage, FIntPoint InPixel, FColor InExpected, const TCHAR* InWhat)
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!InStage->ReadBack(Pixels, Size))
		{
			InStage->GetTest().AddError(FString::Printf(
				TEXT("%s: the render target could not be read back at all."), InWhat));
			return;
		}
		FDreamPixelProbe::ExpectColorAt(InStage->GetTest(), Pixels, Size, InPixel, InExpected, ColorTolerance, InWhat);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiSolidRedWidgetRendersTest,
	"DreamGUI.RHI.ASolidRedWidgetRendersRedAtItsCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiSolidRedWidgetRendersTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetPixelTestsLocal;
	// The claim the whole file rests on: a widget with a colour and a size becomes pixels of that
	// colour, at the place the canvas says it is. Everything below assumes it, so it is asserted on
	// its own first -- with the corners, because a target filled entirely red would satisfy the
	// centre alone and would mean the clear, not the block, painted the picture.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		return false;
	}

	EnqueueSettledFrames(Stage);
	TFunction<void()> Check = [Stage]()
	{
		if (!Stage->IsUsable())
		{
			return;
		}
		CheckPixel(Stage, FRenderTargetStage::TargetCentrePixel(), BlockColour, TEXT("the centre of the red block"));
		CheckPixel(Stage, FIntPoint(0, 0), ClearColour, TEXT("the top-left corner, away from the block"));
		CheckPixel(Stage, FIntPoint(TargetExtent - 1, 0), ClearColour, TEXT("the top-right corner, away from the block"));
		CheckPixel(Stage, FIntPoint(0, TargetExtent - 1), ClearColour, TEXT("the bottom-left corner, away from the block"));
		CheckPixel(Stage, FIntPoint(TargetExtent - 1, TargetExtent - 1), ClearColour, TEXT("the bottom-right corner, away from the block"));
	};
	EnqueueCheck(Check);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiMovingTheWidgetMovesItsPixelsTest,
	"DreamGUI.RHI.MovingTheWidgetMovesItsPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiMovingTheWidgetMovesItsPixelsTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetPixelTestsLocal;
	// That the picture FOLLOWS the widget, which a fixture that drew the block once and cached it
	// would still pass the first test with. The new centre is asked of the canvas rather than worked
	// out here, so this fails if the pixels stay where they were OR if they move somewhere the
	// canvas does not think the widget is -- and it cannot pass by the widget never moving, because
	// then the new centre and the old are the same pixel and the two assertions contradict.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		return false;
	}

	EnqueueSettledFrames(Stage);

	TFunction<void()> MoveToCorner = [Stage]()
	{
		if (!Stage->IsUsable())
		{
			return;
		}
		// Flush against one corner of the canvas: half the canvas less half the block, on both axes.
		// Which corner depends on the sign convention of the anchor space, and this test deliberately
		// does not care -- it asks the canvas afterwards where the block ended up.
		const double Offset = (TargetExtent - BlockExtent) * 0.5;
		Stage->GetBlock()->SetAnchoredPosition(FVector2D(-Offset, Offset));
	};
	EnqueueCheck(MoveToCorner);
	EnqueueSettledFrames(Stage);

	TFunction<void()> Check = [Stage]()
	{
		if (!Stage->IsUsable())
		{
			return;
		}
		CheckPixel(Stage, Stage->BlockCentrePixel(), BlockColour, TEXT("the centre of the moved block"));
		CheckPixel(Stage, FRenderTargetStage::TargetCentrePixel(), ClearColour, TEXT("the middle of the target, which the block has left"));
	};
	EnqueueCheck(Check);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiHiddenWidgetLeavesTargetClearTest,
	"DreamGUI.RHI.AHiddenWidgetLeavesTheTargetClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiHiddenWidgetLeavesTargetClearTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetPixelTestsLocal;
	// Hiding has to reach the picture, and the picture has to be REDRAWN without the block rather
	// than simply left alone -- a renderer that skipped the frame would leave the last red image in
	// the target and look identical from the game thread. The red is asserted first so that "no red
	// anywhere" cannot pass on a stage that never drew anything in the first place, which is the way
	// an absence test usually goes quietly wrong.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		return false;
	}

	EnqueueSettledFrames(Stage);

	TFunction<void()> CheckVisibleFirst = [Stage]()
	{
		if (!Stage->IsUsable())
		{
			return;
		}
		CheckPixel(Stage, FRenderTargetStage::TargetCentrePixel(), BlockColour, TEXT("the block before it is hidden"));
		// Hidden rather than inactive or collapsed: it is the one that leaves the widget in the
		// layout and takes away only the drawing, which is the narrowest change that should empty
		// the target.
		Stage->GetBlock()->SetVisibility(EDreamWidgetVisibility::Hidden);
	};
	EnqueueCheck(CheckVisibleFirst);
	EnqueueSettledFrames(Stage);

	TFunction<void()> CheckGone = [Stage]()
	{
		if (!Stage->IsUsable())
		{
			return;
		}
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!Stage->ReadBack(Pixels, Size))
		{
			Stage->GetTest().AddError(TEXT("The render target could not be read back after hiding the block."));
			return;
		}
		const int32 RedPixels = FDreamPixelProbe::CountColor(Pixels, Size, FRenderTargetStage::WholeTarget(), BlockColour, ColorTolerance);
		Stage->GetTest().TestEqual(TEXT("A hidden widget leaves no pixels of its colour behind"), RedPixels, 0);
	};
	EnqueueCheck(CheckGone);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiRedBlockAreaTest,
	"DreamGUI.RHI.TheRedBlockCoversAboutTenThousandPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiRedBlockAreaTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderTargetPixelTestsLocal;
	// The scale of the picture, which no single pixel can say anything about. A 100x100 widget on a
	// canvas whose rect is the target's own size must cover 100x100 pixels; if the canvas took its
	// size from anything else -- the 2x2 fallback, an editor viewport, a resolution scale nobody
	// asked for -- the block is still red in the middle and still moves when it is moved, and only
	// its area gives that away. The viewport size is asserted alongside it so a failure says which
	// of the two went wrong.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		return false;
	}

	EnqueueSettledFrames(Stage);

	TFunction<void()> Check = [Stage]()
	{
		if (!Stage->IsUsable())
		{
			return;
		}
		FAutomationTestBase& CurrentTest = Stage->GetTest();
		CurrentTest.TestEqual(TEXT("The canvas's viewport is the render target's own size"),
			Stage->GetCanvas()->GetViewportSize(), FIntPoint(TargetExtent, TargetExtent));

		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!Stage->ReadBack(Pixels, Size))
		{
			CurrentTest.AddError(TEXT("The render target could not be read back."));
			return;
		}
		const int32 RedPixels = FDreamPixelProbe::CountColor(Pixels, Size, FRenderTargetStage::WholeTarget(), BlockColour, ColorTolerance);
		// A tenth either way. The edges of the block are the renderer's business -- a half-covered
		// boundary pixel is not a defect -- but a block drawn at the wrong scale misses this by a
		// factor, not by a percent.
		const int32 ExpectedArea = BlockExtent * BlockExtent;
		const int32 Tolerance = ExpectedArea / 10;
		const bool bAreaIsRight = FMath::Abs(RedPixels - ExpectedArea) <= Tolerance;
		if (!CurrentTest.TestTrue(TEXT("A 100x100 widget covers about ten thousand pixels"), bAreaIsRight))
		{
			CurrentTest.AddError(FString::Printf(
				TEXT("The red block covers %d pixels; expected %d give or take %d."), RedPixels, ExpectedArea, Tolerance));
		}
	};
	EnqueueCheck(Check);
	EnqueueTearDown(Stage);
	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
