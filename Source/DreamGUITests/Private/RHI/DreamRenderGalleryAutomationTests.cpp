// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "PixelFormat.h"
#include "TextureResource.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUITextData.h"
#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "Extensions/Effects/DreamBackgroundPixelate.h"
#include "Extensions/Effects/DreamPixelSort.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"

/*
 * The pictures DreamGUI's renderer makes, one scene to a test, each held whole to a golden image of it.
 *
 * The other pixel tests ask a pointed question -- is this pixel red -- and a renderer can answer every one of them
 * and still draw a corner square, a glyph too thin, a shadow on the wrong side. A picture asks all of it at once. Each
 * scene gathers the features one part of the renderer is responsible for: textures and tints, nested rounded clips,
 * the rect block's borders, gradients and shadows, distance-field text with its outline and underlay, the three
 * background effects, multisampled edges, and the two ways a plain widget is drawn -- the built-in shader and the
 * material -- which have to agree with each other as well as with their pictures.
 *
 * Every scene is a RenderTarget canvas in the editor's world, for the reason DreamRenderTargetPixelAutomationTests.cpp
 * gives: it is the one world a viewport draws. Its picture is read back once it has stopped changing -- glyphs
 * rasterise and material shaders compile over several frames -- and written to Saved/DreamGUITests/Captures whatever
 * happens, so that there is always a picture to look at; FDreamPixelProbe::ExpectMatchesGolden says how a golden
 * image comes to be.
 */
namespace DreamRenderGalleryTestLocal
{
	static constexpr int32 Extent = 256;
	/** Opaque and not black, so that a draw that came out black and one that never happened look different. */
	static const FColor Backdrop = FColor(28, 30, 38, 255);
	/** Per channel, and the share of the pixels that may be past it: see FDreamPixelProbe::ExpectMatchesGolden. */
	static constexpr uint8 GoldenTolerance = 8;
	static constexpr double GoldenAllowedFraction = 0.002;
	/** How long a scene may take to show at all: a material's shaders compile the first time anything draws with them. */
	static constexpr double DrawnTimeoutSeconds = 180.0;
	/** Consecutive frames a picture must stay the same before it is believed. */
	static constexpr int32 StableFrames = 3;
	static constexpr double StableTimeoutSeconds = 30.0;

	/** A RenderTarget root canvas in the editor's world, Extent pixels square, and the scene a test puts on it. */
	class FGalleryStage
	{
	public:
		explicit FGalleryStage(FAutomationTestBase& InTest)
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
			Target->InitCustomFormat(static_cast<uint32>(Extent), static_cast<uint32>(Extent), EPixelFormat::PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate(true);
			TargetTexture.Reset(Target);

			UDreamWidget* Root = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
			Root->SetDisplayName(TEXT("DreamRenderGalleryRoot"));
			Root->SetWidth(static_cast<float>(Extent));
			Root->SetHeight(static_cast<float>(Extent));
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
			Canvas->SetRenderTargetClearColor(Backdrop);
			Canvas->SetRenderTargetResolutionScale(1.0f);
			Canvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
			Canvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
			Canvas->SetRenderTarget(Target);
		}

		~FGalleryStage()
		{
			TearDown();
		}

		FGalleryStage(const FGalleryStage&) = delete;
		FGalleryStage& operator=(const FGalleryStage&) = delete;

		bool IsUsable() const
		{
			return Failure.IsEmpty() && World != nullptr && IsValid(TargetTexture.Get()) && IsValid(RootWidget.Get()) && IsValid(CanvasComponent.Get());
		}
		const FString& GetFailure() const { return Failure; }
		FAutomationTestBase& GetTest() const { return Test; }
		UDreamCanvas* GetCanvas() const { return CanvasComponent.Get(); }

		/** A registered widget of InSize under InParent (the root by default), its centre InPosition from the parent's, +Y up. */
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

		/** A block showing InTexture (plain white when none) tinted InColour. */
		UDreamWidget* AddBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColour, UTexture* InTexture = nullptr, UDreamWidget* InParent = nullptr)
		{
			UDreamWidget* Widget = AddWidget(InName, InSize, InPosition, InParent);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(InTexture != nullptr ? InTexture : FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetColor(InColour);
			}
			return Widget;
		}

		UDreamRectBlock* AddRectBlock(const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InBody, float InCornerRadius)
		{
			UDreamWidget* Widget = AddWidget(InName, InSize, InPosition);
			UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
			if (Block != nullptr)
			{
				// In pixels: a rect block's sizes default to fractions of its own size.
				Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetBorderWidthUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetInnerShadowSizeUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetInnerShadowBlurUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetOuterShadowSizeUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetOuterShadowBlurUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetOuterShadowDistanceUnitMode(EDreamRectBlockUnitMode::Value);
				Block->SetBodyColor(InBody);
				Block->SetCornerRadius(FVector4(InCornerRadius, InCornerRadius, InCornerRadius, InCornerRadius));
			}
			return Block;
		}

		UDreamText* AddText(const TCHAR* InName, const TCHAR* InText, float InFontSize, FVector2D InSize, FVector2D InPosition, FColor InColour)
		{
			UDreamWidget* Widget = AddWidget(InName, InSize, InPosition);
			UDreamText* Text = Widget->CreateNewVisual<UDreamText>();
			if (Text != nullptr)
			{
				Text->SetText(FText::FromString(InText));
				Text->SetFontSize(InFontSize);
				Text->SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
				Text->SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
				// One line each: a line that wrapped would run into the next one's.
				Text->SetOverflowType(EDreamUITextOverflowType::HorizontalOverflow);
				Text->SetColor(InColour);
			}
			return Text;
		}

		/** An InExtent-square texture, filtered nearest so its pattern keeps hard edges, held until the stage goes. */
		UTexture2D* MakeTexture(int32 InExtent, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt)
		{
			UTexture2D* Texture = UTexture2D::CreateTransient(InExtent, InExtent, PF_B8G8R8A8);
			if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
			{
				return nullptr;
			}
			Texture->Filter = TF_Nearest;
			Texture->SRGB = true;
			Texture->LODGroup = TEXTUREGROUP_UI;
			FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
			FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
			for (int32 Y = 0; Y < InExtent; ++Y)
			{
				for (int32 X = 0; X < InExtent; ++X)
				{
					Pixels[Y * InExtent + X] = InColourAt(X, Y);
				}
			}
			Mip.BulkData.Unlock();
			Texture->UpdateResource();
			Textures.Emplace(Texture);
			return Texture;
		}

		UTexture2D* MakeChecker(int32 InExtent, int32 InCell, FColor InA, FColor InB)
		{
			return MakeTexture(InExtent, [InCell, InA, InB](int32 X, int32 Y) { return ((X / InCell) + (Y / InCell)) % 2 == 0 ? InA : InB; });
		}

		/** InTop along the first row, InBottom along the last, in even steps between. */
		UTexture2D* MakeGradient(int32 InExtent, FColor InTop, FColor InBottom)
		{
			return MakeTexture(InExtent, [InExtent, InTop, InBottom](int32 X, int32 Y)
			{
				const float Alpha = InExtent > 1 ? static_cast<float>(Y) / static_cast<float>(InExtent - 1) : 0.0f;
				return FColor(
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(InTop.R), static_cast<float>(InBottom.R), Alpha))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(InTop.G), static_cast<float>(InBottom.G), Alpha))),
					static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(InTop.B), static_cast<float>(InBottom.B), Alpha))),
					255);
			});
		}

		/** The built-in shader or the material path for plain widgets; put back when the stage is torn down. */
		void UseBuiltInShader(bool bInUse)
		{
			UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
			if (!SavedBuiltInShader.IsSet())
			{
				SavedBuiltInShader = Settings->bUseBuiltInUIShader;
			}
			Settings->bUseBuiltInUIShader = bInUse;
		}

		/** Multisampling for every DreamUI renderer, 1 for none; put back when the stage is torn down. */
		void UseMultisampling(uint8 InSamples)
		{
			UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
			if (!SavedAntiAliasing.IsSet())
			{
				SavedAntiAliasing = Settings->AntiAliasingMethod;
				SavedSampleCount = Settings->MSAASampleCount;
			}
			Settings->AntiAliasingMethod = InSamples > 1 ? EDreamUIRendererAntiAliasingMethod::MSAA : EDreamUIRendererAntiAliasingMethod::None;
			Settings->MSAASampleCount = static_cast<EDreamUIRendererMSAASampleCount>(InSamples > 1 ? InSamples : 1);
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

		/** Idempotent; the destructor calls it too. */
		void TearDown()
		{
			if (bTornDown)
			{
				return;
			}
			bTornDown = true;
			UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
			if (SavedBuiltInShader.IsSet())
			{
				Settings->bUseBuiltInUIShader = SavedBuiltInShader.GetValue();
			}
			if (SavedAntiAliasing.IsSet())
			{
				Settings->AntiAliasingMethod = SavedAntiAliasing.GetValue();
				Settings->MSAASampleCount = SavedSampleCount;
			}
			if (UDreamWidget* Root = RootWidget.Get(); IsValid(Root))
			{
				Root->DestroyWidget();
			}
			CanvasComponent.Reset();
			RootWidget.Reset();
			TargetTexture.Reset();
			Textures.Reset();
			World = nullptr;
		}

	private:
		FAutomationTestBase& Test;
		FString Failure;
		UWorld* World = nullptr;
		TStrongObjectPtr<UTextureRenderTarget2D> TargetTexture;
		TStrongObjectPtr<UDreamWidget> RootWidget;
		TStrongObjectPtr<UDreamCanvas> CanvasComponent;
		TArray<TStrongObjectPtr<UTexture2D>> Textures;
		TOptional<bool> SavedBuiltInShader;
		TOptional<EDreamUIRendererAntiAliasingMethod> SavedAntiAliasing;
		EDreamUIRendererMSAASampleCount SavedSampleCount = EDreamUIRendererMSAASampleCount::One;
		bool bTornDown = false;
	};

	using FStageRef = TSharedRef<FGalleryStage>;

	FStageRef BeginStage(FAutomationTestBase& InTest)
	{
		FStageRef Stage = MakeShared<FGalleryStage>(InTest);
		if (!Stage->IsUsable())
		{
			InTest.AddError(FString::Printf(TEXT("The gallery's render-target stage did not come up: %s."), *Stage->GetFailure()));
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

	void EnqueueFrames(const FStageRef& InStage, int32 InFrames)
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

	/** Pixels of the picture that are not the backdrop. */
	int32 CountDrawn(const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		return InSize.X * InSize.Y - FDreamPixelProbe::CountColor(InPixels, InSize, FIntRect(0, 0, InSize.X, InSize.Y), Backdrop, GoldenTolerance);
	}

	/**
	 * Frames, a redraw asked for on each, until at least InMinDrawn pixels are something other than the backdrop, or
	 * DrawnTimeoutSeconds: for the first draw with a material, whose shaders the editor compiles only once something
	 * asks for them, and for glyphs, which rasterise on worker threads. The picture's own check says which it was.
	 */
	void EnqueueFramesUntilDrawn(const FStageRef& InStage, int32 InMinDrawn)
	{
		TSharedRef<double> Deadline = MakeShared<double>(0.0);
		EnqueueStep([InStage, InMinDrawn, Deadline]()
		{
			if (*Deadline == 0.0)
			{
				*Deadline = FPlatformTime::Seconds() + DrawnTimeoutSeconds;
			}
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if ((InStage->ReadBack(Pixels, Size) && CountDrawn(Pixels, Size) >= InMinDrawn) || FPlatformTime::Seconds() > *Deadline)
			{
				return true;
			}
			InStage->RequestRedraw();
			return false;
		});
	}

	/** Frames until the picture is the same StableFrames times running, or StableTimeoutSeconds. */
	void EnqueueFramesUntilStable(const FStageRef& InStage)
	{
		struct FState
		{
			TArray<FColor> Previous;
			FIntPoint PreviousSize = FIntPoint::ZeroValue;
			int32 SameInARow = 0;
			double Deadline = 0.0;
		};
		TSharedRef<FState> State = MakeShared<FState>();
		EnqueueStep([InStage, State]()
		{
			if (State->Deadline == 0.0)
			{
				State->Deadline = FPlatformTime::Seconds() + StableTimeoutSeconds;
			}
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			const bool bRead = InStage->ReadBack(Pixels, Size);
			State->SameInARow = bRead && Size == State->PreviousSize && Pixels == State->Previous ? State->SameInARow + 1 : 0;
			State->Previous = MoveTemp(Pixels);
			State->PreviousSize = Size;
			if (State->SameInARow >= StableFrames || FPlatformTime::Seconds() > State->Deadline)
			{
				return true;
			}
			InStage->RequestRedraw();
			return false;
		});
	}

	/** Let the scene settle -- drawn, then still -- and hold the picture to Golden/<InName>.png. */
	void EnqueuePictureCheck(const FStageRef& InStage, const FString& InName, int32 InMinDrawn)
	{
		EnqueueFrames(InStage, 3);
		EnqueueFramesUntilDrawn(InStage, InMinDrawn);
		EnqueueFramesUntilStable(InStage);
		EnqueueDo([InStage, InName, InMinDrawn]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			FAutomationTestBase& Test = InStage->GetTest();
			if (!InStage->ReadBack(Pixels, Size))
			{
				Test.AddError(FString::Printf(TEXT("%s: the render target could not be read back at all."), *InName));
				return;
			}
			const int32 Drawn = CountDrawn(Pixels, Size);
			Test.TestTrue(FString::Printf(TEXT("%s draws something (%d pixel(s) are not the backdrop, at least %d expected)"), *InName, Drawn, InMinDrawn), Drawn >= InMinDrawn);
			FDreamPixelProbe::ExpectMatchesGolden(Test, Pixels, Size, InName, GoldenTolerance, GoldenAllowedFraction);
		});
	}

	void EnqueueTearDown(const FStageRef& InStage)
	{
		EnqueueDo([InStage]() { InStage->TearDown(); });
	}

	/** Two textured blocks, a tinted checker, a gradient and three blocks of partial alpha stacked over each other. */
	void BuildBlocks(FGalleryStage& InStage)
	{
		UTexture2D* Checker = InStage.MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
		UTexture2D* Gradient = InStage.MakeGradient(64, FColor(255, 70, 70, 255), FColor(40, 90, 255, 255));
		InStage.AddBlock(TEXT("Checker"), FVector2D(96.0, 96.0), FVector2D(-64.0, 64.0), FColor::White, Checker);
		InStage.AddBlock(TEXT("TintedChecker"), FVector2D(96.0, 96.0), FVector2D(64.0, 64.0), FColor(255, 200, 64, 255), Checker);
		InStage.AddBlock(TEXT("Gradient"), FVector2D(96.0, 96.0), FVector2D(-64.0, -64.0), FColor::White, Gradient);
		InStage.AddBlock(TEXT("Opaque"), FVector2D(64.0, 64.0), FVector2D(44.0, -44.0), FColor(255, 0, 0, 255));
		InStage.AddBlock(TEXT("TwoThirds"), FVector2D(64.0, 64.0), FVector2D(64.0, -64.0), FColor(0, 255, 0, 170));
		InStage.AddBlock(TEXT("OneThird"), FVector2D(64.0, 64.0), FVector2D(84.0, -84.0), FColor(0, 0, 255, 85));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryBlocksTest,
	"DreamGUI.RHI.Gallery.TexturedTintedAndTranslucentBlocksMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryBlocksTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	BuildBlocks(*Stage);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Blocks"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryClipsTest,
	"DreamGUI.RHI.Gallery.RoundedClipsNestedThreeDeepMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryClipsTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// Each clip is smaller than what it holds, so every corner of the picture is a clip's corner: the outer one rounds a
	// checker larger than itself, the middle one a red block, and the innermost -- as round as it is wide -- a blue one,
	// each also inside the clips around it.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
	UDreamWidget* Outer = Stage->AddWidget(TEXT("OuterClip"), FVector2D(200.0, 200.0), FVector2D::ZeroVector);
	Outer->SetClipping(EDreamWidgetClipping::ClipToBounds);
	Outer->SetClippingCornerRadius(FVector4f(40.0f, 40.0f, 40.0f, 40.0f));
	Stage->AddBlock(TEXT("OuterContent"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Checker, Outer);
	UDreamWidget* Middle = Stage->AddWidget(TEXT("MiddleClip"), FVector2D(140.0, 140.0), FVector2D(30.0, 30.0), Outer);
	Middle->SetClipping(EDreamWidgetClipping::ClipToBounds);
	Middle->SetClippingCornerRadius(FVector4f(24.0f, 24.0f, 24.0f, 24.0f));
	Stage->AddBlock(TEXT("MiddleContent"), FVector2D(200.0, 200.0), FVector2D::ZeroVector, FColor(230, 40, 40, 220), nullptr, Middle);
	UDreamWidget* Inner = Stage->AddWidget(TEXT("InnerClip"), FVector2D(60.0, 60.0), FVector2D(20.0, 20.0), Middle);
	Inner->SetClipping(EDreamWidgetClipping::ClipToBounds);
	Inner->SetClippingCornerRadius(FVector4f(30.0f, 30.0f, 30.0f, 30.0f));
	Stage->AddBlock(TEXT("InnerContent"), FVector2D(120.0, 120.0), FVector2D::ZeroVector, FColor(40, 80, 255, 255), nullptr, Inner);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Clips"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryRectBlocksTest,
	"DreamGUI.RHI.Gallery.RectBlocksWithBordersGradientsAndShadowsMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryRectBlocksTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	if (UDreamRectBlock* Bordered = Stage->AddRectBlock(TEXT("Bordered"), FVector2D(100.0, 100.0), FVector2D(-60.0, 60.0), FColor(80, 160, 255, 255), 24.0f))
	{
		Bordered->SetEnableBorder(true);
		Bordered->SetBorderWidth(4.0f);
		Bordered->SetBorderColor(FColor::White);
	}
	if (UDreamRectBlock* Graded = Stage->AddRectBlock(TEXT("Gradient"), FVector2D(100.0, 100.0), FVector2D(60.0, 60.0), FColor(255, 120, 40, 255), 12.0f))
	{
		Graded->SetEnableBodyGradient(true);
		Graded->SetBodyGradientColor(FColor(255, 40, 160, 255));
		Graded->SetBodyGradientRotation(45.0f);
	}
	if (UDreamRectBlock* Inset = Stage->AddRectBlock(TEXT("InnerShadow"), FVector2D(100.0, 100.0), FVector2D(-60.0, -60.0), FColor(60, 200, 120, 255), 16.0f))
	{
		Inset->SetEnableInnerShadow(true);
		Inset->SetInnerShadowColor(FColor(0, 0, 0, 180));
		Inset->SetInnerShadowSize(10.0f);
		Inset->SetInnerShadowBlur(10.0f);
	}
	if (UDreamRectBlock* Raised = Stage->AddRectBlock(TEXT("OuterShadow"), FVector2D(100.0, 100.0), FVector2D(60.0, -60.0), FColor(240, 240, 240, 255), 20.0f))
	{
		Raised->SetEnableOuterShadow(true);
		Raised->SetOuterShadowColor(FColor(0, 0, 0, 200));
		Raised->SetOuterShadowSize(6.0f);
		Raised->SetOuterShadowBlur(12.0f);
		Raised->SetOuterShadowDistance(6.0f);
		Raised->SetOuterShadowAngle(-45.0f);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_RectBlocks"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryTextTest,
	"DreamGUI.RHI.Gallery.TextWithAnOutlineAndAnUnderlayMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryTextTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// Three sizes of the default font: large enough to show a distance field's edges, one with an outline and a drop
	// shadow, and small enough that its glyphs are only a few texels of the atlas across.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->AddText(TEXT("Large"), TEXT("DreamGUI 0123"), 30.0f, FVector2D(250.0, 50.0), FVector2D(0.0, 80.0), FColor::White);
	if (UDreamText* Styled = Stage->AddText(TEXT("Styled"), TEXT("Outline & Shadow"), 26.0f, FVector2D(250.0, 50.0), FVector2D(0.0, 20.0), FColor(255, 220, 64, 255)))
	{
		FDreamTextStyle Style = Styled->GetTextStyle();
		Style.OutlineColor = FColor(0, 0, 0, 255);
		Style.OutlineWidth = 0.15f;
		Style.UnderlayColor = FColor(0, 0, 0, 200);
		Style.UnderlayOffset = FVector2f(0.06f, 0.06f);
		Style.UnderlaySoftness = 0.05f;
		Styled->SetTextStyle(Style);
	}
	Stage->AddText(TEXT("Small"), TEXT("The quick brown fox jumps"), 16.0f, FVector2D(250.0, 30.0), FVector2D(0.0, -40.0), FColor(200, 220, 255, 255));
	EnqueuePictureCheck(Stage, TEXT("Gallery_Text"), 1500);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryEffectsTest,
	"DreamGUI.RHI.Gallery.BlurPixelateAndPixelSortOverStripesMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryEffectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// Eight upright stripes crossed by a white bar, and the three background effects side by side over them, each in a
	// column of its own: what each does to edges it is given, in both directions.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	const FColor Stripes[] =
	{
		FColor(255, 0, 0, 255), FColor(255, 255, 0, 255), FColor(0, 255, 0, 255), FColor(0, 255, 255, 255),
		FColor(0, 0, 255, 255), FColor(255, 0, 255, 255), FColor(255, 255, 255, 255), FColor(0, 0, 0, 255),
	};
	const int32 StripeCount = UE_ARRAY_COUNT(Stripes);
	const double StripeWidth = static_cast<double>(Extent) / StripeCount;
	for (int32 Index = 0; Index < StripeCount; ++Index)
	{
		const double Centre = -Extent / 2.0 + StripeWidth * (Index + 0.5);
		Stage->AddBlock(*FString::Printf(TEXT("Stripe%d"), Index), FVector2D(StripeWidth, Extent), FVector2D(Centre, 0.0), Stripes[Index]);
	}
	Stage->AddBlock(TEXT("Bar"), FVector2D(Extent, 24.0), FVector2D(0.0, 30.0), FColor::White);
	if (UDreamBackgroundBlur* Blur = Stage->AddWidget(TEXT("Blur"), FVector2D(70.0, 200.0), FVector2D(-88.0, 0.0))->CreateNewVisual<UDreamBackgroundBlur>())
	{
		// Strength runs from nothing to the renderer's full blur at 1.
		Blur->SetBlurStrength(0.6f);
	}
	if (UDreamBackgroundPixelate* Pixelate = Stage->AddWidget(TEXT("Pixelate"), FVector2D(70.0, 200.0), FVector2D::ZeroVector)->CreateNewVisual<UDreamBackgroundPixelate>())
	{
		Pixelate->SetPixelateStrength(40.0f);
	}
	if (UDreamPixelSort* Sort = Stage->AddWidget(TEXT("PixelSort"), FVector2D(70.0, 200.0), FVector2D(88.0, 0.0))->CreateNewVisual<UDreamPixelSort>())
	{
		Sort->SetSortAxis(EDreamPixelSortAxis::Vertical);
		Sort->SetThresholdMin(0.0f);
		Sort->SetThresholdMax(1.0f);
		Sort->SetSortStrength(1.0f);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Effects"), 40000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProxyMadeAgainKeepsRootTest,
	"DreamGUI.RHI.ACanvasMeshWhoseProxyIsMadeAgainKeepsItsSectionsAndItsPicture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamProxyMadeAgainKeepsRootTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// The engine makes a primitive's scene proxy again whenever its render state is dirtied. The canvas's sections live in
	// the mesh's render root, which the new proxy is made for: the root, its sections and the picture stay as they were.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->AddBlock(TEXT("Left"), FVector2D(Extent / 2.0, Extent), FVector2D(-Extent / 4.0, 0.0), FColor(255, 64, 0, 255));
	Stage->AddBlock(TEXT("Right"), FVector2D(Extent / 2.0, Extent / 2.0), FVector2D(Extent / 4.0, 0.0), FColor(0, 128, 255, 255));
	TSharedRef<TArray<FColor>> Before = MakeShared<TArray<FColor>>();
	TSharedRef<const void*> RootBefore = MakeShared<const void*>(nullptr);
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 1000);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage, Before, RootBefore]()
	{
		FIntPoint Size = FIntPoint::ZeroValue;
		TestTrue(TEXT("The picture reads back"), Stage->ReadBack(*Before, Size));
		UDreamUIMeshComponent* Mesh = Stage->GetCanvas()->GetUIMesh();
		if (TestNotNull(TEXT("The canvas has a mesh"), Mesh))
		{
			*RootBefore = Mesh->GetRenderRoot();
			TestNotNull(TEXT("...with a render root"), *RootBefore);
			Mesh->MarkRenderStateDirty();
		}
	});
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage, Before, RootBefore]()
	{
		UDreamUIMeshComponent* Mesh = Stage->GetCanvas()->GetUIMesh();
		if (!TestNotNull(TEXT("The canvas still has its mesh"), Mesh))
		{
			return;
		}
		TestTrue(TEXT("The proxy made again is made for the same render root"), Mesh->GetRenderRoot() == *RootBefore);
		TArray<FColor> After;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The picture reads back again"), Stage->ReadBack(After, Size))
			|| !TestEqual(TEXT("...at the size it had"), After.Num(), Before->Num()))
		{
			return;
		}
		int32 Different = 0;
		for (int32 Index = 0; Index < After.Num(); ++Index)
		{
			const FColor& A = (*Before)[Index];
			const FColor& B = After[Index];
			if (FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B)) > GoldenTolerance)
			{
				++Different;
			}
		}
		TestEqual(TEXT("...and it is the same picture"), Different, 0);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFullSizeBlurMultisampledTest,
	"DreamGUI.RHI.AFullSizeBlurOnAMultisampledCanvasSurvivesItsResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamFullSizeBlurMultisampledTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// Stripes under a blur the size of the canvas, drawn without multisampling and then with four samples. On a
	// multisampled target the blur is done in the target's resolved copy; it used to stay there, and the resolve that ends
	// the UI's recording wrote the unblurred target over the picture. The two pictures are to be the same blur.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	const FColor Stripes[] =
	{
		FColor(255, 0, 0, 255), FColor(255, 255, 0, 255), FColor(0, 255, 0, 255), FColor(0, 255, 255, 255),
		FColor(0, 0, 255, 255), FColor(255, 0, 255, 255), FColor(255, 255, 255, 255), FColor(0, 0, 0, 255),
	};
	const int32 StripeCount = UE_ARRAY_COUNT(Stripes);
	const double StripeWidth = static_cast<double>(Extent) / StripeCount;
	for (int32 Index = 0; Index < StripeCount; ++Index)
	{
		const double Centre = -Extent / 2.0 + StripeWidth * (Index + 0.5);
		Stage->AddBlock(*FString::Printf(TEXT("Stripe%d"), Index), FVector2D(StripeWidth, Extent), FVector2D(Centre, 0.0), Stripes[Index]);
	}
	UDreamBackgroundBlur* Blur = Stage->AddWidget(TEXT("Blur"), FVector2D(Extent, Extent), FVector2D::ZeroVector)->CreateNewVisual<UDreamBackgroundBlur>();
	if (!TestNotNull(TEXT("A blur"), Blur))
	{
		Stage->TearDown();
		return false;
	}
	Blur->SetUseFullSize(true);
	Blur->SetBlurStrength(0.8f);

	// A pixel that is none of the stripes' colours is one the blur mixed.
	auto CountMixed = [Stripes, StripeCount](const TArray<FColor>& InPixels)
	{
		int32 Mixed = 0;
		for (const FColor& Pixel : InPixels)
		{
			bool bStripe = false;
			for (int32 Index = 0; Index < StripeCount && !bStripe; ++Index)
			{
				bStripe = FMath::Abs(Pixel.R - Stripes[Index].R) <= GoldenTolerance && FMath::Abs(Pixel.G - Stripes[Index].G) <= GoldenTolerance
					&& FMath::Abs(Pixel.B - Stripes[Index].B) <= GoldenTolerance;
			}
			Mixed += bStripe ? 0 : 1;
		}
		return Mixed;
	};
	TSharedRef<TArray<FColor>> Plain = MakeShared<TArray<FColor>>();
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 40000);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([Stage, Plain]()
	{
		FIntPoint Size = FIntPoint::ZeroValue;
		if (Stage->ReadBack(*Plain, Size))
		{
			FDreamPixelProbe::SaveCapture(*Plain, Size, TEXT("FullSizeBlur_Plain"));
		}
	});
	EnqueueDo([Stage]() { Stage->UseMultisampling(4); });
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 40000);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage, Plain, CountMixed]()
	{
		TArray<FColor> Multisampled;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The multisampled picture reads back"), Stage->ReadBack(Multisampled, Size))
			|| !TestEqual(TEXT("...at the size of the other"), Multisampled.Num(), Plain->Num()))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Multisampled, Size, TEXT("FullSizeBlur_Multisampled"));
		const int32 Pixels = Plain->Num();
		const int32 MixedPlain = CountMixed(*Plain);
		const int32 MixedMultisampled = CountMixed(Multisampled);
		TestTrue(FString::Printf(TEXT("Without multisampling the stripes are blurred (%d of %d pixels mixed)"), MixedPlain, Pixels), MixedPlain > Pixels / 5);
		TestTrue(FString::Printf(TEXT("With four samples they are blurred too (%d of %d pixels mixed)"), MixedMultisampled, Pixels), MixedMultisampled > Pixels / 5);
		int32 Different = 0;
		for (int32 Index = 0; Index < Pixels; ++Index)
		{
			const FColor& A = (*Plain)[Index];
			const FColor& B = Multisampled[Index];
			if (FMath::Max3(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G), FMath::Abs(A.B - B.B)) > GoldenTolerance)
			{
				++Different;
			}
		}
		TestTrue(FString::Printf(TEXT("...and the two are the same blur (%d of %d pixels differ)"), Different, Pixels), Different <= Pixels / 100);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMultisamplingTest,
	"DreamGUI.RHI.Gallery.RotatedEdgesWithAndWithoutMultisamplingMatchTheirGoldenImages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMultisamplingTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// The same slanted edges twice: stepped without multisampling, and softened with four samples. The canvas draws
	// into a multisampled target of its own and resolves it into this one, so the second picture is that path's.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseMultisampling(1);
	UDreamWidget* Square = Stage->AddBlock(TEXT("Square"), FVector2D(120.0, 120.0), FVector2D(-20.0, 10.0), FColor::White);
	Square->SetRelativeRotationEuler(FRotator(0.0, 0.0, 30.0));
	UDreamWidget* Bar = Stage->AddBlock(TEXT("Bar"), FVector2D(200.0, 8.0), FVector2D(10.0, -70.0), FColor(255, 60, 60, 255));
	Bar->SetRelativeRotationEuler(FRotator(0.0, 0.0, -20.0));
	EnqueuePictureCheck(Stage, TEXT("Gallery_Aliased"), 10000);
	EnqueueDo([Stage]() { Stage->UseMultisampling(4); });
	EnqueuePictureCheck(Stage, TEXT("Gallery_Multisampled"), 10000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryBuiltInAgainstMaterialTest,
	"DreamGUI.RHI.Gallery.TheBuiltInShaderAndTheMaterialPathDrawTheSamePicture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryBuiltInAgainstMaterialTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// A plain widget -- a texture, a tint, a glyph -- is drawn by the renderer's own shader or by the default UI
	// material, as the project chooses, and the choice is meant to be invisible. The same scene both ways, each held to
	// its own golden image and the two held to each other.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	BuildBlocks(*Stage);
	Stage->AddText(TEXT("Label"), TEXT("Built-in / Material"), 22.0f, FVector2D(240.0, 36.0), FVector2D(0.0, 0.0), FColor::White);
	TSharedRef<TArray<FColor>> BuiltIn = MakeShared<TArray<FColor>>();
	TSharedRef<FIntPoint> BuiltInSize = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
	EnqueuePictureCheck(Stage, TEXT("Gallery_BuiltInShader"), 20000);
	EnqueueDo([Stage, BuiltIn, BuiltInSize]()
	{
		Stage->ReadBack(*BuiltIn, *BuiltInSize);
		Stage->UseBuiltInShader(false);
		Stage->GetCanvas()->MarkCanvasUpdate(true);
	});
	EnqueuePictureCheck(Stage, TEXT("Gallery_MaterialPath"), 20000);
	EnqueueDo([this, Stage, BuiltIn, BuiltInSize]()
	{
		TArray<FColor> Material;
		FIntPoint MaterialSize = FIntPoint::ZeroValue;
		if (TestTrue(TEXT("both pictures read back"), BuiltIn->Num() > 0 && Stage->ReadBack(Material, MaterialSize)))
		{
			FDreamPixelProbe::ExpectPicturesMatch(*this, *BuiltIn, *BuiltInSize, Material, MaterialSize, TEXT("Gallery_BuiltInAgainstMaterial"), 12, 0.01);
		}
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
