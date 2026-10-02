// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "DreamWidgetBlueprint.h"
#include "Core/DreamUITextData.h"
#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "Extensions/Effects/DreamBackgroundPixelate.h"
#include "Extensions/Effects/DreamPixelSort.h"

#include "DreamGalleryStage.h"
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
 * image comes to be. The stage and the steps that let a scene settle are DreamGalleryStage.h's, shared with the .dui
 * scenes and the text parity tests.
 */
namespace DreamRenderGalleryTestLocal
{
	using namespace DreamGalleryStage;

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
	FDreamShippedSampleTest,
	"DreamGUI.RHI.TheShippedSampleCompilesFromItsTextAndDrawsItsHeading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamShippedSampleTest::RunTest(const FString& Parameters)
{
	using namespace DreamRenderGalleryTestLocal;
	// The README's "Try it", done the way it tells a user to: a widget Blueprint where the sample's `class` line says the
	// class is, pointed at the file the plugin ships -- read from the plugin as installed -- compiled, and shown.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("The plugin is installed"), Plugin.IsValid()))
	{
		return false;
	}
	FString SamplePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Plugin->GetContentDir(), TEXT("Samples"), TEXT("HelloDreamGUI.dui")));
	FPaths::NormalizeFilename(SamplePath);
	if (!TestTrue(FString::Printf(TEXT("The sample ships with it (%s)"), *SamplePath), FPaths::FileExists(SamplePath)))
	{
		return false;
	}
	UPackage* Package = CreatePackage(TEXT("/Game/UI/WBP_HelloDreamGUI"));
	Package->AddToRoot();
	UDreamWidgetBlueprint* Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
		UDreamTextUserWidget::StaticClass(), Package, FName(TEXT("WBP_HelloDreamGUI")), BPTYPE_Normal,
		UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
	UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
		? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("A widget Blueprint for it"), Defaults))
	{
		Package->RemoveFromRoot();
		return false;
	}
	// Pick Text Source, as the designer's toolbar does it: the class default the compile reads the file from.
	Defaults->SourceFile.FilePath = SamplePath;
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	AddInfo(FString::Printf(TEXT("The sample compiled with %d error(s) and %d warning(s)"), Results.NumErrors, Results.NumWarnings));
	if (!TestEqual(TEXT("The sample compiles without an error"), Results.NumErrors, 0))
	{
		Package->RemoveFromRoot();
		return false;
	}

	// A small screen, not the gallery's square: the sample's card is meant to be 420 wide.
	FStageRef Stage = BeginStage(*this, FIntPoint(640, 360));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		Package->RemoveFromRoot();
		return false;
	}
	UDreamUserWidget* Sample = CreateDreamWidget(Stage->GetWorld(), Blueprint->GeneratedClass.Get(), Stage->GetRoot());
	if (!TestNotNull(TEXT("The sample's class makes a widget on the stage"), Sample))
	{
		Stage->TearDown();
		Package->RemoveFromRoot();
		return false;
	}
	// Placed the way adding it to the viewport places a page (UDreamScreenUISubsystem::ConfigurePage): full-bleed on what
	// it is added to, the stage here. Left as it was made, it is a widget's default size, and a screen that fills
	// whatever it is added to fills that.
	Sample->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	Sample->SetAnchoredPosition(FVector2D::ZeroVector);
	Sample->SetSizeDelta(FVector2D::ZeroVector);
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 200);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage, Placed = TWeakObjectPtr<UDreamUserWidget>(Sample)]()
	{
		// Where the sample's parts stand on the stage, for when the picture is not what it should be.
		FString Parts;
		if (const UDreamUserWidget* Widget = Placed.Get())
		{
			Parts = FString::Printf(TEXT("sample %.0f x %.0f"), Widget->GetWidth(), Widget->GetHeight());
			for (const TCHAR* Name : { TEXT("Root"), TEXT("Card"), TEXT("Backdrop"), TEXT("Column"), TEXT("Heading"), TEXT("Rule"), TEXT("Subheading") })
			{
				const UDreamWidget* Part = Widget->FindChildByDisplayName(Name, true);
				Parts += Part != nullptr
					? FString::Printf(TEXT(", %s %.0f x %.0f%s%s"), Name, Part->GetWidth(), Part->GetHeight(),
						Part->GetWidgetActiveInHierarchy() ? TEXT("") : TEXT(" inactive"),
						Part->GetRenderCanvas() == Stage->GetCanvas() ? TEXT("") : TEXT(" on another canvas"))
					: FString::Printf(TEXT(", no %s"), Name);
			}
		}
		AddInfo(FString::Printf(TEXT("On the stage: %s; the stage's canvas has %d draw call(s)"),
			Parts.IsEmpty() ? TEXT("the sample is gone") : *Parts, Stage->GetCanvas() != nullptr ? Stage->GetCanvas()->GetDrawCallCount() : -1));
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The picture reads back"), Stage->ReadBack(Pixels, Size)))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Sample_HelloDreamGUI"));
		// The heading is #E6E9F0 on the card; nothing else in the picture comes near it -- the text under it is #8C93A6.
		int32 HeadingPixels = 0;
		int32 TopRow = Size.Y;
		int32 BottomRow = -1;
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const FColor& Pixel = Pixels[Y * Size.X + X];
				if (Pixel.R > 160 && Pixel.G > 160 && Pixel.B > 160)
				{
					++HeadingPixels;
					TopRow = FMath::Min(TopRow, Y);
					BottomRow = FMath::Max(BottomRow, Y);
				}
			}
		}
		if (!TestTrue(FString::Printf(TEXT("The heading is drawn (%d bright pixels)"), HeadingPixels), HeadingPixels >= 100))
		{
			return;
		}
		// Laid out as written: one line of 28-pixel type at the top of a card in the middle of the screen. Measured at the
		// wrong width, the heading comes out one character per line, far taller than the card is meant to be.
		TestTrue(FString::Printf(TEXT("The heading is one line (rows %d to %d)"), TopRow, BottomRow), BottomRow - TopRow < 2 * 28);
		TestTrue(FString::Printf(TEXT("and it is in the top half of the screen, where the card's top is (rows %d to %d of %d)"), TopRow, BottomRow, Size.Y),
			BottomRow < Size.Y / 2);
	});
	EnqueueTearDown(Stage);
	EnqueueDo([Package]()
	{
		Package->RemoveFromRoot();
	});
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
