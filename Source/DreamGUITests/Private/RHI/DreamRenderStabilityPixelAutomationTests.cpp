// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PixelFormat.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "Extensions/Effects/DreamBackgroundPixelate.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIDataTexture.h"
#include "Core/DreamUISettings.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "Extensions/Effects/DreamPixelSort.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Extensions/DreamStaticMesh.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"

/*
 * What the render thread keeps drawing after the game thread has changed its mind, looked at in pixels.
 *
 * Each test builds a RenderTarget canvas in the editor's world -- the one world a level viewport renders,
 * so the canvas's view extension is asked to draw (see DreamRenderTargetPixelAutomationTests.cpp, which
 * this file follows) -- changes something, lets frames pass and reads the target back.
 *
 * The post-process samples at the end are the pictures the three effects make today, held as a handful
 * of pixels, so that moving their render proxies somewhere else can be checked against them.
 */
namespace DreamRenderStabilityTestLocal
{
	static constexpr int32 TargetExtent = 256;
	static constexpr uint8 ColorTolerance = 8;
	/** See DreamRenderTargetPixelAutomationTests.cpp: the canvas's worker thread and the scene render each trail the tick by a frame. */
	static constexpr int32 FramesToSettle = 3;

	static const FColor ClearColour = FColor(0, 0, 0, 255);
	static const FColor Red = FColor(255, 0, 0, 255);
	static const FColor Blue = FColor(0, 0, 255, 255);
	static const FColor Green = FColor(0, 255, 0, 255);

	/** A root canvas rendering to a TargetExtent-square target in the editor's world, and whatever a test puts under it. */
	class FStage
	{
	public:
		explicit FStage(FAutomationTestBase& InTest)
			: Test(InTest)
		{
			if (GEditor == nullptr || GEditor->GetEditorWorldContext().World() == nullptr)
			{
				Failure = TEXT("there is no editor world to render");
				return;
			}
			if (GEditor->GetAllViewportClients().Num() == 0)
			{
				Failure = TEXT("the editor has no viewport, so nothing ever renders its world");
				return;
			}
			World = GEditor->GetEditorWorldContext().World();

			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			Target->AddressX = TextureAddress::TA_Clamp;
			Target->AddressY = TextureAddress::TA_Clamp;
			Target->ClearColor = FLinearColor::Black;
			Target->InitCustomFormat(static_cast<uint32>(TargetExtent), static_cast<uint32>(TargetExtent), EPixelFormat::PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate(true);
			TargetTexture.Reset(Target);

			UDreamWidget* Root = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
			Root->SetDisplayName(TEXT("DreamRenderStabilityRoot"));
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
			Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
			Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
			Canvas->SetRenderTarget(Target);
		}

		~FStage()
		{
			TearDown();
		}

		FStage(const FStage&) = delete;
		FStage& operator=(const FStage&) = delete;

		bool IsUsable() const
		{
			return Failure.IsEmpty() && World != nullptr && IsValid(TargetTexture.Get()) && IsValid(RootWidget.Get()) && IsValid(CanvasComponent.Get());
		}
		const FString& GetFailure() const { return Failure; }
		FAutomationTestBase& GetTest() const { return Test; }
		UDreamCanvas* GetCanvas() const { return CanvasComponent.Get(); }
		UDreamWidget* GetRoot() const { return RootWidget.Get(); }

		/**
		 * A registered widget of InSize under InParent (the root by default), its centre at InPosition from
		 * the parent's centre, +Y up. Held by its parent and the manager, like any widget, and by nothing
		 * of the stage's: a test that destroys one must be able to see it collected.
		 */
		UDreamWidget* AddWidget(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, UDreamWidget* InParent = nullptr)
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
			Widget->SetDisplayName(InName);
			Widget->SetWidth(static_cast<float>(InSize.X));
			Widget->SetHeight(static_cast<float>(InSize.Y));
			Widget->OnRegister();
			Widget->TrySetParent(InParent != nullptr ? InParent : RootWidget.Get(), false);
			Widget->SetAnchoredPosition(InPosition);
			return Widget;
		}

		/** A solid block: the default white texture, tinted. */
		UDreamWidget* AddBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColour, UDreamWidget* InParent = nullptr)
		{
			UDreamWidget* Widget = AddWidget(InName, InSize, InPosition, InParent);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetColor(InColour);
			}
			return Widget;
		}

		/** The pixel (0,0 at the top left) that shows InLocal, a point in InWidget's own plane: +Y right, +Z up from its pivot. */
		FIntPoint PixelOf(const UDreamWidget* InWidget, FVector2D InLocal) const
		{
			UDreamCanvas* Canvas = CanvasComponent.Get();
			if (!IsValid(Canvas) || !IsValid(InWidget))
			{
				return FIntPoint(-1, -1);
			}
			const FVector World3D = InWidget->GetWorldTransform().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y));
			FVector2D CanvasPoint = FVector2D::ZeroVector;
			FVector2D ViewportPoint = FVector2D::ZeroVector;
			if (!Canvas->Project3DToScreen(World3D, CanvasPoint) || !Canvas->ConvertPositionFromCanvasToViewport(CanvasPoint, ViewportPoint))
			{
				return FIntPoint(-1, -1);
			}
			return ViewportPoint.IntPoint();
		}

		void RequestRedraw() const
		{
			if (GEditor != nullptr)
			{
				GEditor->RedrawAllViewports(false);
			}
		}

		bool ReadBack(TArray<FColor>& OutPixels, FIntPoint& OutSize) const
		{
			return FDreamPixelProbe::ReadBack(TargetTexture.Get(), OutPixels, OutSize);
		}

		UTextureRenderTarget2D* GetTarget() const { return TargetTexture.Get(); }

		/** A new target of the same size for the canvas; the stage lets go of the old one, which nothing else holds. */
		UTextureRenderTarget2D* ReplaceTarget()
		{
			UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			Target->AddressX = TextureAddress::TA_Clamp;
			Target->AddressY = TextureAddress::TA_Clamp;
			Target->ClearColor = FLinearColor::Black;
			Target->InitCustomFormat(static_cast<uint32>(TargetExtent), static_cast<uint32>(TargetExtent), EPixelFormat::PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate(true);
			if (UDreamCanvas* Canvas = CanvasComponent.Get())
			{
				Canvas->SetRenderTarget(Target);
			}
			TargetTexture.Reset(Target);
			return Target;
		}

		/** Idempotent; the destructor calls it too. */
		void TearDown()
		{
			if (bTornDown)
			{
				return;
			}
			bTornDown = true;
			if (UDreamWidget* Root = RootWidget.Get(); IsValid(Root))
			{
				Root->DestroyWidget();
			}
			CanvasComponent.Reset();
			RootWidget.Reset();
			TargetTexture.Reset();
			World = nullptr;
		}

	private:
		FAutomationTestBase& Test;
		FString Failure;
		UWorld* World = nullptr;
		TStrongObjectPtr<UTextureRenderTarget2D> TargetTexture;
		TStrongObjectPtr<UDreamWidget> RootWidget;
		TStrongObjectPtr<UDreamCanvas> CanvasComponent;
		bool bTornDown = false;
	};

	using FStageRef = TSharedRef<FStage>;

	FStageRef BeginStage(FAutomationTestBase& InTest)
	{
		FStageRef Stage = MakeShared<FStage>(InTest);
		if (!Stage->IsUsable())
		{
			InTest.AddError(FString::Printf(TEXT("The render-target stage did not come up: %s."), *Stage->GetFailure()));
		}
		return Stage;
	}

	/** Through a named local: a lambda's capture list carries commas, and the macro would cut its argument at the first. */
	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	void EnqueueDo(TFunction<void()> InAction)
	{
		EnqueueStep([InAction]() { InAction(); return true; });
	}

	/** FramesToSettle frames, a redraw asked for on each; the last one yields so the next step sees it painted. */
	void EnqueueSettledFrames(const FStageRef& InStage, int32 InFrames = FramesToSettle)
	{
		TSharedRef<int32> Remaining = MakeShared<int32>(InFrames);
		EnqueueStep([InStage, Remaining]()
		{
			if (*Remaining <= 0)
			{
				return true;
			}
			InStage->RequestRedraw();
			--(*Remaining);
			return false;
		});
	}

	void EnqueueTearDown(const FStageRef& InStage)
	{
		EnqueueDo([InStage]() { InStage->TearDown(); });
	}

	/** The colour at InPixel, or none when the target cannot be read or the pixel is off it. */
	TOptional<FColor> ColourAt(const FStageRef& InStage, FIntPoint InPixel)
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!InStage->ReadBack(Pixels, Size) || InPixel.X < 0 || InPixel.Y < 0 || InPixel.X >= Size.X || InPixel.Y >= Size.Y)
		{
			return {};
		}
		return Pixels[InPixel.Y * Size.X + InPixel.X];
	}

	void CheckPixel(const FStageRef& InStage, FIntPoint InPixel, FColor InExpected, const TCHAR* InWhat)
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!InStage->ReadBack(Pixels, Size))
		{
			InStage->GetTest().AddError(FString::Printf(TEXT("%s: the render target could not be read back at all."), InWhat));
			return;
		}
		FDreamPixelProbe::ExpectColorAt(InStage->GetTest(), Pixels, Size, InPixel, InExpected, ColorTolerance, InWhat);
	}

	/**
	 * Frames, a redraw asked for on each, until InPixel no longer shows InColour or InTimeoutSeconds have
	 * passed since the first of them: for a draw that waits on shaders the engine compiles only once
	 * something asks to draw with them. The check after it says which of the two it was.
	 */
	void EnqueueFramesUntilPixelChanges(const FStageRef& InStage, FIntPoint InPixel, FColor InColour, double InTimeoutSeconds)
	{
		TSharedRef<double> Deadline = MakeShared<double>(0.0);
		EnqueueStep([InStage, InPixel, InColour, InTimeoutSeconds, Deadline]()
		{
			if (*Deadline == 0.0)
			{
				*Deadline = FPlatformTime::Seconds() + InTimeoutSeconds;
			}
			const TOptional<FColor> Now = ColourAt(InStage, InPixel);
			if ((Now.IsSet() && !FDreamPixelProbe::IsNear(Now.GetValue(), InColour, ColorTolerance)) || FPlatformTime::Seconds() > *Deadline)
			{
				return true;
			}
			InStage->RequestRedraw();
			return false;
		});
	}

	/**
	 * A static-mesh cache holding a 100x100 square in the widget plane, drawn from both sides, filled in
	 * directly: the mesh a cache is normally built from is an editor-only asset reference, and what the
	 * visual draws is only ever these arrays.
	 */
	UDreamUIStaticMeshCacheData* MakeSquareMesh()
	{
		UDreamUIStaticMeshCacheData* Cache = NewObject<UDreamUIStaticMeshCacheData>(GetTransientPackage(), NAME_None, RF_Transient);
		const UClass* Class = UDreamUIStaticMeshCacheData::StaticClass();
		FArrayProperty* VertexProperty = FindFProperty<FArrayProperty>(Class, TEXT("VertexData"));
		FArrayProperty* IndexProperty = FindFProperty<FArrayProperty>(Class, TEXT("IndexData"));
		FStructProperty* BoundsProperty = FindFProperty<FStructProperty>(Class, TEXT("MeshBounds"));
		if (VertexProperty == nullptr || IndexProperty == nullptr)
		{
			return nullptr;
		}
		const FColor White = FColor::White;
		TArray<FDreamUIStaticMeshVertex>& Vertices = *VertexProperty->ContainerPtrToValuePtr<TArray<FDreamUIStaticMeshVertex>>(Cache);
		Vertices = {
			FDreamUIStaticMeshVertex(FVector(0.0, -50.0, -50.0), FVector::RightVector, FVector::ForwardVector, White, FVector2D(0.0, 1.0), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector),
			FDreamUIStaticMeshVertex(FVector(0.0, 50.0, -50.0), FVector::RightVector, FVector::ForwardVector, White, FVector2D(1.0, 1.0), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector),
			FDreamUIStaticMeshVertex(FVector(0.0, -50.0, 50.0), FVector::RightVector, FVector::ForwardVector, White, FVector2D(0.0, 0.0), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector),
			FDreamUIStaticMeshVertex(FVector(0.0, 50.0, 50.0), FVector::RightVector, FVector::ForwardVector, White, FVector2D(1.0, 0.0), FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector),
		};
		TArray<uint32>& Indices = *IndexProperty->ContainerPtrToValuePtr<TArray<uint32>>(Cache);
		Indices = { 0, 1, 2, 1, 3, 2, 0, 2, 1, 1, 2, 3 };
		if (BoundsProperty != nullptr)
		{
			*BoundsProperty->ContainerPtrToValuePtr<FBox>(Cache) = FBox(FVector(0.0, -50.0, -50.0), FVector(0.0, 50.0, 50.0));
		}
		return Cache;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiHiddenStaticMeshStopsDrawingTest,
	"DreamGUI.RHI.AStaticMeshThatIsHiddenStopsDrawingAndStaysGoneOnceDestroyed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiHiddenStaticMeshStopsDrawingTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	// A static mesh draws through a section of its own, which the canvas pools with the others when it
	// stops being drawn. The pooled section's proxy used to stay enabled on the render thread -- with the
	// visual's material -- so a hidden mesh went on drawing, and a destroyed one drew through a material
	// that had been collected.
	//
	// A mesh section takes its material as it is, without the textures and parameters a canvas gives its
	// own draw calls, so the UI materials draw nothing useful there. The material here is the engine's
	// unlit vertex-colour one, made into an instance the visual owns: it is collected with the visual,
	// which is what a destroyed mesh's section used to go on drawing through. It is drawn by its own
	// shaders, not the built-in ones every other visual here uses. The renderer skips a section whose
	// shaders are not ready, and the engine compiles these only once something asks to draw with them, so
	// the test waits -- a bounded while -- for the mesh to show before looking.
	//
	// Cleared to green, which nothing here draws, so that "drawn" is simply "covers the clear colour".
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->GetCanvas()->SetRenderTargetClearColor(Green);
	UDreamWidget* MeshWidget = Stage->AddWidget(TEXT("Mesh"), FVector2D(100.0, 100.0), FVector2D::ZeroVector);
	UDreamStaticMesh* Mesh = MeshWidget->CreateNewVisual<UDreamStaticMesh>();
	UDreamUIStaticMeshCacheData* Cache = MakeSquareMesh();
	if (!TestNotNull(TEXT("a static mesh visual"), Mesh) || !TestNotNull(TEXT("with a square to draw"), Cache))
	{
		Stage->TearDown();
		return false;
	}
	const TStrongObjectPtr<UDreamUIStaticMeshCacheData> KeepCache(Cache);
	Mesh->SetMesh(Cache);
	Mesh->SetVertexColorType(EDreamStaticMeshVertexColorType::ReplaceByUIColor);
	Mesh->SetColor(Red);
	Mesh->SetReplaceMaterial(GEngine != nullptr ? GEngine->VertexColorViewModeMaterial_ColorOnly : nullptr);
	if (!TestNotNull(TEXT("an instance of the engine's unlit vertex-colour material"), Mesh->GetOrCreateDynamicMaterialInstance()))
	{
		Stage->TearDown();
		return false;
	}
	const FIntPoint Centre = Stage->PixelOf(MeshWidget, FVector2D::ZeroVector);

	EnqueueSettledFrames(Stage);
	EnqueueFramesUntilPixelChanges(Stage, Centre, Green, 120.0);
	EnqueueSettledFrames(Stage);
	EnqueueDo([this, Stage, Centre]()
	{
		const TOptional<FColor> Shown = ColourAt(Stage, Centre);
		if (TestTrue(TEXT("the target reads back where the static mesh is"), Shown.IsSet()))
		{
			TestFalse(FString::Printf(TEXT("the static mesh, while it is shown, covers the clear colour (it drew %s)"), *FDreamPixelProbe::Describe(Shown.GetValue())),
				FDreamPixelProbe::IsNear(Shown.GetValue(), Green, ColorTolerance));
		}
	});
	EnqueueDo([MeshWidget]() { MeshWidget->SetWidgetActive(false); });
	EnqueueSettledFrames(Stage);
	EnqueueDo([Stage, Centre]()
	{
		CheckPixel(Stage, Centre, Green, TEXT("where the static mesh was, once it is hidden"));
	});
	const TWeakObjectPtr<UDreamWidget> WeakMeshWidget(MeshWidget);
	EnqueueDo([WeakMeshWidget]()
	{
		if (UDreamWidget* Widget = WeakMeshWidget.Get())
		{
			Widget->DestroyWidget();
		}
		FlushRenderingCommands();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
		FlushRenderingCommands();
	});
	EnqueueSettledFrames(Stage);
	EnqueueDo([Stage, Centre]()
	{
		CheckPixel(Stage, Centre, Green, TEXT("where the static mesh was, once it is destroyed and collected"));
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiChildCanvasSwitchedWithParentTest,
	"DreamGUI.RHI.AChildCanvasSwitchedBackAndForthInTheFrameItsParentRebuildsKeepsDrawing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiChildCanvasSwitchedWithParentTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	// A parent canvas and a child canvas rebuilding their render state in the same frame: the child's
	// mesh hands its sections to the parent's proxy through a render command, and when those updates ran
	// on worker threads the command could reach a parent proxy that had been deleted. Switched back and
	// forth several times, each switch in the frame the parent rebuilds; the picture has to come out
	// whole at the end.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UDreamWidget* Left = Stage->AddBlock(TEXT("Left"), FVector2D(60.0, 60.0), FVector2D(-60.0, 0.0), Blue);
	UDreamWidget* Panel = Stage->AddWidget(TEXT("ChildCanvasPanel"), FVector2D(100.0, 100.0), FVector2D(60.0, 0.0));
	UDreamCanvas* ChildCanvas = Panel->AddComponent<UDreamCanvas>();
	UDreamWidget* Right = Stage->AddBlock(TEXT("Right"), FVector2D(60.0, 60.0), FVector2D::ZeroVector, Red, Panel);
	if (!TestNotNull(TEXT("a child canvas"), ChildCanvas))
	{
		Stage->TearDown();
		return false;
	}
	const FIntPoint LeftCentre = Stage->PixelOf(Left, FVector2D::ZeroVector);
	const FIntPoint RightCentre = Stage->PixelOf(Right, FVector2D::ZeroVector);

	EnqueueSettledFrames(Stage);
	for (int32 Switch = 0; Switch < 6; ++Switch)
	{
		EnqueueDo([Stage, ChildCanvas, Switch]()
		{
			ChildCanvas->SetOverrideSorting(Switch % 2 == 0);
			ChildCanvas->SetSortOrder(Switch);
			Stage->GetCanvas()->MarkCanvasUpdate(true);
		});
		EnqueueSettledFrames(Stage, 1);
	}
	EnqueueSettledFrames(Stage);
	EnqueueDo([Stage, LeftCentre, RightCentre]()
	{
		CheckPixel(Stage, LeftCentre, Blue, TEXT("the block drawn by the parent canvas"));
		CheckPixel(Stage, RightCentre, Red, TEXT("the block drawn by the child canvas"));
	});
	EnqueueTearDown(Stage);
	return true;
}

namespace DreamRenderStabilityTestLocal
{
	/** The target split down the middle: red on the left, blue on the right. */
	void AddSplitBackground(const FStageRef& InStage)
	{
		InStage->AddBlock(TEXT("LeftHalf"), FVector2D(TargetExtent / 2.0, TargetExtent), FVector2D(-TargetExtent / 4.0, 0.0), Red);
		InStage->AddBlock(TEXT("RightHalf"), FVector2D(TargetExtent / 2.0, TargetExtent), FVector2D(TargetExtent / 4.0, 0.0), Blue);
	}

	/** Neither pure red nor pure blue: some of each, the way a pixel that averages across the split is. */
	bool IsAMixOfBoth(const FColor& InColour)
	{
		return InColour.R > 24 && InColour.B > 24;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiPixelateSampleTest,
	"DreamGUI.RHI.PixelateAtFullStrengthTurnsItsWholeRectOneColourAndLeavesTheRestAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiPixelateSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	// At full strength a pixelate cell is as wide as its rect, so the rect straddling the split becomes one
	// colour -- the cell's average, half red and half blue, taken in linear space (so each channel reads
	// about 187, not 128) -- and nothing outside the rect changes.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	AddSplitBackground(Stage);
	UDreamWidget* Effect = Stage->AddWidget(TEXT("Pixelate"), FVector2D(128.0, 128.0), FVector2D::ZeroVector);
	UDreamBackgroundPixelate* Pixelate = Effect->CreateNewVisual<UDreamBackgroundPixelate>();
	if (!TestNotNull(TEXT("a pixelate visual"), Pixelate))
	{
		Stage->TearDown();
		return false;
	}
	Pixelate->SetPixelateStrength(100.0f);
	const FIntPoint InsideLeft = Stage->PixelOf(Effect, FVector2D(-40.0, 0.0));
	const FIntPoint InsideRight = Stage->PixelOf(Effect, FVector2D(40.0, 0.0));
	const FIntPoint OutsideLeft = Stage->PixelOf(Effect, FVector2D(-100.0, 0.0));
	const FIntPoint OutsideRight = Stage->PixelOf(Effect, FVector2D(100.0, 0.0));

	EnqueueSettledFrames(Stage);
	EnqueueDo([this, Stage, InsideLeft, InsideRight, OutsideLeft, OutsideRight]()
	{
		const TOptional<FColor> Left = ColourAt(Stage, InsideLeft);
		const TOptional<FColor> Right = ColourAt(Stage, InsideRight);
		if (TestTrue(TEXT("the target reads back inside the rect"), Left.IsSet() && Right.IsSet()))
		{
			TestTrue(FString::Printf(TEXT("the rect is one cell of one colour across the split (left %s, right %s)"),
				*FDreamPixelProbe::Describe(Left.GetValue()), *FDreamPixelProbe::Describe(Right.GetValue())),
				FDreamPixelProbe::IsNear(Left.GetValue(), Right.GetValue(), ColorTolerance));
			TestTrue(FString::Printf(TEXT("and that colour is the two it covers in equal parts (%s)"), *FDreamPixelProbe::Describe(Left.GetValue())),
				IsAMixOfBoth(Left.GetValue()) && FMath::Abs(static_cast<int32>(Left.GetValue().R) - static_cast<int32>(Left.GetValue().B)) <= ColorTolerance);
		}
		CheckPixel(Stage, OutsideLeft, Red, TEXT("the red half, outside the pixelated rect"));
		CheckPixel(Stage, OutsideRight, Blue, TEXT("the blue half, outside the pixelated rect"));
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiBlurSampleTest,
	"DreamGUI.RHI.BlurMixesTheColoursAcrossAnEdgeInsideItsRectAndLeavesTheRestAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiBlurSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	AddSplitBackground(Stage);
	UDreamWidget* Effect = Stage->AddWidget(TEXT("Blur"), FVector2D(128.0, 128.0), FVector2D::ZeroVector);
	UDreamBackgroundBlur* Blur = Effect->CreateNewVisual<UDreamBackgroundBlur>();
	if (!TestNotNull(TEXT("a blur visual"), Blur))
	{
		Stage->TearDown();
		return false;
	}
	Blur->SetBlurStrength(100.0f);
	const FIntPoint OnTheEdge = Stage->PixelOf(Effect, FVector2D::ZeroVector);
	const FIntPoint OutsideLeft = Stage->PixelOf(Effect, FVector2D(-100.0, 0.0));
	const FIntPoint OutsideRight = Stage->PixelOf(Effect, FVector2D(100.0, 0.0));

	EnqueueSettledFrames(Stage);
	EnqueueDo([this, Stage, OnTheEdge, OutsideLeft, OutsideRight]()
	{
		const TOptional<FColor> Edge = ColourAt(Stage, OnTheEdge);
		if (TestTrue(TEXT("the target reads back on the edge"), Edge.IsSet()))
		{
			TestTrue(FString::Printf(TEXT("on the edge, inside the rect, the blur mixes red and blue (%s)"), *FDreamPixelProbe::Describe(Edge.GetValue())),
				IsAMixOfBoth(Edge.GetValue()));
		}
		CheckPixel(Stage, OutsideLeft, Red, TEXT("the red half, outside the blurred rect"));
		CheckPixel(Stage, OutsideRight, Blue, TEXT("the blue half, outside the blurred rect"));
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiPixelSortSampleTest,
	"DreamGUI.RHI.PixelSortAlongTheSplitKeepsItsColoursInsideItsRectAndLeavesTheRestAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiPixelSortSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	// Sorting only reorders pixels along a run: every pixel inside the rect is still one of the colours
	// the rect covered, and every pixel outside it is untouched. Sorted vertically, down columns that are
	// each one colour, the picture does not change at all.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	AddSplitBackground(Stage);
	UDreamWidget* Effect = Stage->AddWidget(TEXT("PixelSort"), FVector2D(128.0, 128.0), FVector2D::ZeroVector);
	UDreamPixelSort* Sort = Effect->CreateNewVisual<UDreamPixelSort>();
	if (!TestNotNull(TEXT("a pixel sort visual"), Sort))
	{
		Stage->TearDown();
		return false;
	}
	Sort->SetSortAxis(EDreamPixelSortAxis::Vertical);
	Sort->SetThresholdMin(0.0f);
	Sort->SetThresholdMax(1.0f);
	Sort->SetSortStrength(1.0f);
	const FIntPoint InsideLeft = Stage->PixelOf(Effect, FVector2D(-40.0, 30.0));
	const FIntPoint InsideRight = Stage->PixelOf(Effect, FVector2D(40.0, -30.0));
	const FIntPoint OutsideLeft = Stage->PixelOf(Effect, FVector2D(-100.0, 0.0));
	const FIntPoint OutsideRight = Stage->PixelOf(Effect, FVector2D(100.0, 0.0));

	EnqueueSettledFrames(Stage);
	EnqueueDo([Stage, InsideLeft, InsideRight, OutsideLeft, OutsideRight]()
	{
		CheckPixel(Stage, InsideLeft, Red, TEXT("a red column, sorted, inside the rect"));
		CheckPixel(Stage, InsideRight, Blue, TEXT("a blue column, sorted, inside the rect"));
		CheckPixel(Stage, OutsideLeft, Red, TEXT("the red half, outside the sorted rect"));
		CheckPixel(Stage, OutsideRight, Blue, TEXT("the blue half, outside the sorted rect"));
	});
	EnqueueTearDown(Stage);
	return true;
}

namespace DreamRenderStabilityTestLocal
{
	/** A 4x4 texture of one colour, filtered nearest. */
	UTexture2D* MakeSolidTexture(FColor InColour)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
		if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		Texture->Filter = TF_Nearest;
		Texture->SRGB = true;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Index = 0; Index < 16; ++Index)
		{
			Pixels[Index] = InColour;
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	/** Every pixel of InTexture made InColour, and its resource rebuilt -- the old one is deleted on the render thread. */
	void RepaintAndRebuild(UTexture2D* InTexture, FColor InColour)
	{
		FTexture2DMipMap& Mip = InTexture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Index = 0; Index < 16; ++Index)
		{
			Pixels[Index] = InColour;
		}
		Mip.BulkData.Unlock();
		InTexture->UpdateResource();
	}

	/** The built-in shader for plain widgets, whatever the project says, put back when this goes. */
	struct FBuiltInShaderScope
	{
		bool bSaved = true;
		FBuiltInShaderScope()
		{
			bSaved = GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader;
			GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader = true;
		}
		~FBuiltInShaderScope()
		{
			GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader = bSaved;
		}
	};

	void CollectEverything()
	{
		FlushRenderingCommands();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
		FlushRenderingCommands();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiTextureRebuiltUnderASuspendedCanvasTest,
	"DreamGUI.RHI.ATextureRebuiltUnderACanvasThatRebuildsNothingDrawsItsNewPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiTextureRebuiltUnderASuspendedCanvasTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	// A texture drawn by the built-in shader has its resource rebuilt -- repainted and UpdateResource'd, as an atlas
	// repack or a reimport does -- under a canvas that rebuilds none of its draw calls, the way one inside an
	// invalidation box does. The draw used to keep the texture's old resource and read it from freed memory; it binds
	// the texture's reference now, which the rebuild points at the new pixels.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	const TSharedRef<FBuiltInShaderScope> BuiltIn = MakeShared<FBuiltInShaderScope>();
	UTexture2D* Texture = MakeSolidTexture(Red);
	if (!TestNotNull(TEXT("a texture to draw"), Texture))
	{
		Stage->TearDown();
		return false;
	}
	const TSharedRef<TStrongObjectPtr<UTexture2D>> KeepTexture = MakeShared<TStrongObjectPtr<UTexture2D>>(Texture);
	UDreamWidget* Block = Stage->AddWidget(TEXT("Textured"), FVector2D(100.0, 100.0), FVector2D::ZeroVector);
	if (UDreamTexture* Visual = Block->CreateNewVisual<UDreamTexture>())
	{
		Visual->SetTexture(Texture);
		Visual->SetColor(FColor::White);
	}
	const FIntPoint Centre = Stage->PixelOf(Block, FVector2D::ZeroVector);

	EnqueueSettledFrames(Stage);
	EnqueueDo([Stage, Centre]()
	{
		CheckPixel(Stage, Centre, Red, TEXT("the block, drawn with its texture"));
	});
	EnqueueDo([Stage, KeepTexture]()
	{
		Stage->GetCanvas()->SetDrawCallRebuildSuspended(true);
		RepaintAndRebuild(KeepTexture->Get(), Green);
		CollectEverything();
	});
	EnqueueSettledFrames(Stage);
	EnqueueDo([Stage, Centre]()
	{
		CheckPixel(Stage, Centre, Green, TEXT("the block, once its texture was rebuilt under a canvas that rebuilt nothing"));
		Stage->GetCanvas()->SetDrawCallRebuildSuspended(false);
	});
	EnqueueDo([BuiltIn, KeepTexture]() { KeepTexture->Reset(); });
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiClipOutgrowsItsTextureUnderAnEffectTest,
	"DreamGUI.RHI.AnEffectInsideAClipKeepsItsClipWhenTheClipDataOutgrowsItsTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiClipOutgrowsItsTextureUnderAnEffectTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	// A full-strength pixelate twice the size of the clip it is in: inside the clip it is one colour, the average of
	// the split it covers; outside, the clip leaves the background as it was. Then more clips than the clip data
	// texture has rows, so that it grows, and a collection. An effect used to keep the clip texture's resource, which
	// the growth replaced and the collection freed; it keeps the texture's reference now, and the texture grows in
	// place besides.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	AddSplitBackground(Stage);
	UDreamWidget* Clip = Stage->AddWidget(TEXT("Clip"), FVector2D(100.0, 100.0), FVector2D::ZeroVector);
	Clip->SetClipping(EDreamWidgetClipping::ClipToBounds);
	UDreamWidget* Effect = Stage->AddWidget(TEXT("Pixelate"), FVector2D(200.0, 200.0), FVector2D::ZeroVector, Clip);
	UDreamBackgroundPixelate* Pixelate = Effect->CreateNewVisual<UDreamBackgroundPixelate>();
	if (!TestNotNull(TEXT("a pixelate visual"), Pixelate))
	{
		Stage->TearDown();
		return false;
	}
	Pixelate->SetPixelateStrength(100.0f);
	const FIntPoint InsideClip = Stage->PixelOf(Clip, FVector2D(-30.0, 0.0));
	const FIntPoint OutsideClip = Stage->PixelOf(Effect, FVector2D(-80.0, 0.0));
	auto CheckClipped = [this, Stage, InsideClip, OutsideClip](const TCHAR* InWhen)
	{
		const TOptional<FColor> Inside = ColourAt(Stage, InsideClip);
		if (TestTrue(FString::Printf(TEXT("the target reads back inside the clip %s"), InWhen), Inside.IsSet()))
		{
			TestTrue(FString::Printf(TEXT("inside the clip %s, the effect has mixed the split (%s)"), InWhen, *FDreamPixelProbe::Describe(Inside.GetValue())),
				IsAMixOfBoth(Inside.GetValue()));
		}
		CheckPixel(Stage, OutsideClip, Red, *FString::Printf(TEXT("outside the clip %s, where the effect's rect is but the clip is not"), InWhen));
	};

	EnqueueSettledFrames(Stage);
	EnqueueDo([CheckClipped]() { CheckClipped(TEXT("before the clip data grew")); });
	EnqueueDo([this, Stage]()
	{
		UTexture* ClipTexture = Stage->GetCanvas()->GetClipDataTexture();
		const int32 RowsBefore = Cast<UDreamUIDataTexture>(ClipTexture) != nullptr ? Cast<UDreamUIDataTexture>(ClipTexture)->GetHeight() : 0;
		// Small and out of the way, and each one a clip of its own: a row each.
		for (int32 Index = 0; Index < RowsBefore + 12; ++Index)
		{
			UDreamWidget* Extra = Stage->AddWidget(*FString::Printf(TEXT("ExtraClip%d"), Index), FVector2D(2.0, 2.0), FVector2D(-120.0, -120.0));
			Extra->SetClipping(EDreamWidgetClipping::ClipToBounds);
		}
		AddInfo(FString::Printf(TEXT("the clip data texture had %d rows; %d more clips were added"), RowsBefore, RowsBefore + 12));
	});
	EnqueueSettledFrames(Stage);
	EnqueueDo([this, Stage]()
	{
		CollectEverything();
		const UDreamUIDataTexture* ClipTexture = Cast<UDreamUIDataTexture>(Stage->GetCanvas()->GetClipDataTexture());
		if (TestNotNull(TEXT("the canvas's clip data lives in a data texture"), ClipTexture))
		{
			TestTrue(FString::Printf(TEXT("which grew (%d rows now)"), ClipTexture->GetHeight()), ClipTexture->GetHeight() > 128);
		}
	});
	EnqueueSettledFrames(Stage);
	EnqueueDo([CheckClipped]() { CheckClipped(TEXT("after the clip data grew and was collected around")); });
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRhiCanvasGivenANewTargetTest,
	"DreamGUI.RHI.ACanvasGivenANewTargetDrawsIntoItAndItsOldOneIsCollected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRhiCanvasGivenANewTargetTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderStabilityTestLocal;

	// The renderer takes a render-target canvas's target on the render thread and keeps its texture, not the
	// target's resource, which a target collected between the update and the draw used to leave it holding.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UDreamWidget* Block = Stage->AddBlock(TEXT("Block"), FVector2D(100.0, 100.0), FVector2D::ZeroVector, Red);
	const FIntPoint Centre = Stage->PixelOf(Block, FVector2D::ZeroVector);
	const TSharedRef<TWeakObjectPtr<UTextureRenderTarget2D>> OldTarget = MakeShared<TWeakObjectPtr<UTextureRenderTarget2D>>();

	EnqueueSettledFrames(Stage);
	EnqueueDo([Stage, Centre]()
	{
		CheckPixel(Stage, Centre, Red, TEXT("the block, in the first target"));
	});
	EnqueueDo([Stage, OldTarget]()
	{
		*OldTarget = Stage->GetTarget();
		Stage->ReplaceTarget();
		CollectEverything();
	});
	EnqueueSettledFrames(Stage);
	EnqueueDo([this, Stage, Centre, OldTarget]()
	{
		CheckPixel(Stage, Centre, Red, TEXT("the block, in the target that replaced it"));
		TestFalse(TEXT("the first target was collected: nothing kept it"), OldTarget->IsValid());
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
