// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "GlobalShader.h"
#include "MaterialShared.h"
#include "Materials/MaterialInterface.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"
#include "RHICommandList.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUILayerStyleMath.h"
#include "DreamUIRender/DreamUILayerStyleComposite.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * Layer styles, drawn: every effect on a rect block in its own pixels, each pixel held to the CPU's formula; the same scene
 * through the rect block's material; styles drawn apart round a text, an image and a container's subtree; a style drawn apart
 * held to the same style drawn in a rect block's pixels; and the jump flood a group's style measures by, held to the exact
 * distance transform.
 */
namespace DreamLayerStylePixelTestLocal
{
	using namespace DreamGalleryStage;

	constexpr int32 Cell = 96;
	constexpr int32 Columns = 5;
	constexpr int32 Rows = 3;
	const FIntPoint GallerySize(Columns * Cell, Rows * Cell);
	/** Every block of the gallery: 56 square, its corners 10. */
	constexpr float BlockHalf = 28.0f;
	constexpr float BlockCorner = 10.0f;
	const FColor BlockColor(70, 120, 200, 255);

	FVector2D CellCentre(int32 InIndex)
	{
		const int32 Column = InIndex % Columns;
		const int32 Row = InIndex / Columns;
		return FVector2D(-GallerySize.X * 0.5 + Cell * 0.5 + Cell * Column, GallerySize.Y * 0.5 - Cell * 0.5 - Cell * Row);
	}

	FDreamStyleDropShadow Shadow(float InAngle, float InDistance, float InSize, float InSpread = 0.0f, FColor InColor = FColor::Black, float InOpacity = 0.75f)
	{
		FDreamStyleDropShadow Out;
		Out.bUseGlobalLight = false;
		Out.Angle = InAngle;
		Out.Distance = InDistance;
		Out.Size = InSize;
		Out.Spread = InSpread;
		Out.Color = InColor;
		Out.Opacity = InOpacity;
		return Out;
	}

	/** One effect, or a few, to each cell: what the gallery shows and the parity test measures. */
	TArray<FDreamLayerEffects> GalleryStyles()
	{
		TArray<FDreamLayerEffects> Styles;
		{
			FDreamLayerEffects S;
			S.DropShadows.Add(Shadow(120.0f, 8.0f, 10.0f));
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			FDreamStyleInnerShadow& Inner = S.InnerShadows.AddDefaulted_GetRef();
			Inner.bUseGlobalLight = false;
			Inner.Angle = 120.0f;
			Inner.Distance = 5.0f;
			Inner.Size = 8.0f;
			Inner.Opacity = 0.9f;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			S.OuterGlow.bEnabled = true;
			S.OuterGlow.Color = FColor(255, 210, 90);
			S.OuterGlow.Size = 14.0f;
			S.OuterGlow.Opacity = 1.0f;
			S.OuterGlow.BlendMode = EDreamUIBlendMode::Alpha;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			S.InnerGlow.bEnabled = true;
			S.InnerGlow.Color = FColor(255, 255, 255);
			S.InnerGlow.Size = 12.0f;
			S.InnerGlow.Opacity = 0.9f;
			S.InnerGlow.BlendMode = EDreamUIBlendMode::Alpha;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			S.BevelEmboss.bEnabled = true;
			S.BevelEmboss.Size = 10.0f;
			S.BevelEmboss.Depth = 1.5f;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			S.BevelEmboss.bEnabled = true;
			S.BevelEmboss.Style = EDreamStyleBevelStyle::PillowEmboss;
			S.BevelEmboss.Technique = EDreamStyleBevelTechnique::ChiselHard;
			S.BevelEmboss.Size = 10.0f;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			S.Satin.bEnabled = true;
			S.Satin.Opacity = 0.7f;
			S.Satin.Size = 12.0f;
			S.Satin.Distance = 9.0f;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			FDreamStyleGradientOverlay& Overlay = S.GradientOverlays.AddDefaulted_GetRef();
			Overlay.Fill.Gradient = FDreamStyleGradient::FromColors(FColor(250, 80, 120), FColor(250, 230, 90));
			Overlay.Fill.Angle = 45.0f;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			FDreamStyleGradientOverlay& Overlay = S.GradientOverlays.AddDefaulted_GetRef();
			Overlay.Fill.Gradient = FDreamStyleGradient::FromColors(FColor::White, FColor(20, 30, 60));
			Overlay.Fill.Type = EDreamStyleGradientType::Radial;
			Overlay.BlendMode = EDreamUIBlendMode::Multiply;
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			FDreamStyleColorOverlay& Overlay = S.ColorOverlays.AddDefaulted_GetRef();
			Overlay.Color = FColor(255, 140, 40);
			Overlay.BlendMode = EDreamUIBlendMode::Overlay;
			Styles.Add(S);
		}
		for (const EDreamStyleStrokePosition Position : { EDreamStyleStrokePosition::Outside, EDreamStyleStrokePosition::Inside, EDreamStyleStrokePosition::Center })
		{
			FDreamLayerEffects S;
			FDreamStyleStroke& Stroke = S.Strokes.AddDefaulted_GetRef();
			Stroke.Size = 5.0f;
			Stroke.Position = Position;
			Stroke.Color = FColor(240, 240, 240);
			Styles.Add(S);
		}
		{
			FDreamLayerEffects S;
			FDreamStyleStroke& Stroke = S.Strokes.AddDefaulted_GetRef();
			Stroke.Size = 6.0f;
			Stroke.Fill = EDreamStyleStrokeFill::Gradient;
			Stroke.Gradient.Gradient = FDreamStyleGradient::FromColors(FColor(90, 255, 200), FColor(160, 60, 255));
			Stroke.Gradient.Type = EDreamStyleGradientType::Angle;
			Styles.Add(S);
		}
		{
			// A button: a soft shadow, a bevel, a gradient and an outside stroke, at fill 0.8.
			FDreamLayerEffects S;
			S.FillOpacity = 0.8f;
			S.DropShadows.Add(Shadow(90.0f, 4.0f, 8.0f, 0.1f));
			S.BevelEmboss.bEnabled = true;
			S.BevelEmboss.Size = 6.0f;
			FDreamStyleGradientOverlay& Overlay = S.GradientOverlays.AddDefaulted_GetRef();
			Overlay.Opacity = 0.6f;
			Overlay.BlendMode = EDreamUIBlendMode::SoftLight;
			FDreamStyleStroke& Stroke = S.Strokes.AddDefaulted_GetRef();
			Stroke.Size = 2.0f;
			Stroke.Color = FColor(20, 30, 50);
			Styles.Add(S);
		}
		return Styles;
	}

	UDreamRectBlock* AddStyledBlock(FGalleryStage& InStage, const TCHAR* InName, FVector2D InPosition, const FDreamLayerEffects& InStyle)
	{
		UDreamRectBlock* Block = InStage.AddRectBlock(InName, FVector2D(BlockHalf * 2.0, BlockHalf * 2.0), InPosition, BlockColor, BlockCorner);
		if (Block != nullptr)
		{
			UDreamLayerStyle* Style = Block->GetWidget()->AddComponent<UDreamLayerStyle>();
			Style->SetStyle(InStyle);
		}
		return Block;
	}

	void BuildGallery(FGalleryStage& InStage, TArray<TWeakObjectPtr<UDreamRectBlock>>* OutBlocks = nullptr)
	{
		const TArray<FDreamLayerEffects> Styles = GalleryStyles();
		for (int32 Index = 0; Index < Styles.Num(); ++Index)
		{
			UDreamRectBlock* Block = AddStyledBlock(InStage, *FString::Printf(TEXT("Styled%d"), Index), CellCentre(Index), Styles[Index]);
			if (OutBlocks != nullptr)
			{
				OutBlocks->Add(Block);
			}
		}
	}

	FVector3f Linear(const FVector3f& InEncoded)
	{
		return FDreamUIBlendMath::ToLinear(InEncoded);
	}

	/** What a gallery block's pixel at InPixel should hold, over the stage's backdrop, on its linear target. */
	FColor ExpectedAt(const FDreamUILayerStyleRecord& InRecord, FIntPoint InPixel, FVector2D InCentre, FIntPoint InSize)
	{
		// The pixel's centre, from the block's centre, +Y down, in units: one to a pixel.
		const FVector2f P(static_cast<float>(InPixel.X + 0.5 - InSize.X * 0.5 - InCentre.X), static_cast<float>(InPixel.Y + 0.5 - (InSize.Y * 0.5 - InCentre.Y)));
		const FVector2f Half(BlockHalf, BlockHalf);
		const FVector4f Corners(BlockCorner, BlockCorner, BlockCorner, BlockCorner);
		auto Distance = [&Half, &Corners](FVector2f Q) { return FDreamUIClipMaskMath::SdRoundRect(Q, Half, Corners); };
		const float D = Distance(P);
		// The shader's fwidth: how far the distance moves to the next pixel across and down.
		const float AA = FMath::Max(FMath::Abs(Distance(P + FVector2f(1.0f, 0.0f)) - D) + FMath::Abs(Distance(P + FVector2f(0.0f, 1.0f)) - D), 1e-4f);
		FDreamUILayerStyleMath::FPixel Pixel;
		Pixel.P = P;
		Pixel.HalfSize = Half;
		Pixel.AA = AA;
		Pixel.Color = FVector3f(BlockColor.R, BlockColor.G, BlockColor.B) / 255.0f;
		Pixel.Opacity = 1.0f;
		Pixel.Coverage = FMath::Clamp(0.5f - D / AA, 0.0f, 1.0f);
		Pixel.Cell = FIntPoint(FMath::FloorToInt(P.X / AA), FMath::FloorToInt(P.Y / AA));
		const FVector4f Flat = FDreamUILayerStyleMath::Shade(InRecord.Texels, Pixel, Distance).Flat;
		const float A = FMath::Clamp(Flat.W, 0.0f, 1.0f);
		const FVector3f Straight = A > 0.0f ? FVector3f(Flat.X, Flat.Y, Flat.Z) / A : FVector3f::ZeroVector;
		const FVector3f Styled = Linear(FVector3f(FMath::Clamp(Straight.X, 0.0f, 1.0f), FMath::Clamp(Straight.Y, 0.0f, 1.0f), FMath::Clamp(Straight.Z, 0.0f, 1.0f)));
		const FVector3f Behind = Linear(FVector3f(Backdrop.R, Backdrop.G, Backdrop.B) / 255.0f);
		const FVector3f Out = FDreamUIBlendMath::ToSrgb(Styled * A + Behind * (1.0f - A));
		return FColor(FMath::RoundToInt(Out.X * 255.0f), FMath::RoundToInt(Out.Y * 255.0f), FMath::RoundToInt(Out.Z * 255.0f), 255);
	}

	void EnqueueReadBack(const FStageRef& InStage, TFunction<void(const TArray<FColor>&, FIntPoint)> InCheck)
	{
		EnqueueDo([InStage, InCheck]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (InStage->ReadBack(Pixels, Size))
			{
				InCheck(Pixels, Size);
			}
			else
			{
				InStage->GetTest().AddError(TEXT("The picture was not read back"));
			}
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleRectBlockPixelTest,
	"DreamGUI.RHI.LayerStyle.EveryEffectOnARectBlockIsWhatTheCpuSaysAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLayerStyleRectBlockPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStylePixelTestLocal;
	FStageRef Stage = BeginStage(*this, GallerySize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	BuildGallery(*Stage);
	EnqueuePictureCheck(Stage, TEXT("Gallery_LayerStyle_RectBlock"), 20000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// Every pixel of every cell against the CPU's formula, round the block's exact shape.
		const TArray<FDreamLayerEffects> Styles = GalleryStyles();
		int32 Checked = 0;
		int32 Wrong = 0;
		int32 Reported = 0;
		for (int32 Index = 0; Index < Styles.Num(); ++Index)
		{
			FDreamUILayerStyleRecord Record;
			FDreamUILayerStyleMath::Pack(Styles[Index], Record);
			const FVector2D Centre = CellCentre(Index);
			const FIntPoint Middle(FMath::FloorToInt(InSize.X * 0.5 + Centre.X), FMath::FloorToInt(InSize.Y * 0.5 - Centre.Y));
			for (int32 Y = Middle.Y - Cell / 2 + 2; Y < Middle.Y + Cell / 2 - 2; ++Y)
			{
				for (int32 X = Middle.X - Cell / 2 + 2; X < Middle.X + Cell / 2 - 2; ++X)
				{
					const FColor Want = ExpectedAt(Record, FIntPoint(X, Y), Centre, InSize);
					const FColor Got = InPixels[Y * InSize.X + X];
					++Checked;
					if (!FDreamPixelProbe::IsNear(Got, Want, 4))
					{
						++Wrong;
						if (Reported++ < 16)
						{
							AddInfo(FString::Printf(TEXT("Cell %d at (%d, %d): %s, the CPU says %s"), Index, X, Y, *FDreamPixelProbe::Describe(Got), *FDreamPixelProbe::Describe(Want)));
						}
					}
				}
			}
		}
		// The two antialias their edges a little apart (the GPU's derivatives are a 2x2 quad's): a few pixels along the edges.
		TestTrue(*FString::Printf(TEXT("Every effect's pixels are the CPU's within 4 (%d of %d differ)"), Wrong, Checked), Checked > 0 && Wrong <= Checked / 200);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleMaterialPathPixelTest,
	"DreamGUI.RHI.LayerStyle.ARectBlocksMaterialDrawsItsStyleAsTheBuiltInShaderDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLayerStyleMaterialPathPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStylePixelTestLocal;
	// One stage drawn by the built-in shader, then by the rect block's material: the choice is the project's, a setting every
	// stage shares, so the two pictures are taken one after the other rather than from two stages at once.
	FStageRef Stage = BeginStage(*this, GallerySize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	TSharedRef<TArray<TWeakObjectPtr<UDreamRectBlock>>> Blocks = MakeShared<TArray<TWeakObjectPtr<UDreamRectBlock>>>();
	BuildGallery(*Stage, &*Blocks);
	TSharedRef<TArray<FColor>> BuiltIn = MakeShared<TArray<FColor>>();
	TSharedRef<FIntPoint> BuiltInSize = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
	EnqueueFramesUntilDrawn(Stage, 20000);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage, Blocks, BuiltIn, BuiltInSize]()
	{
		Stage->ReadBack(*BuiltIn, *BuiltInSize);
		// Its shaders ready first: a material still compiling is not drawn at all, and this one's change with its shader file.
		UDreamRectBlockData* Data = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultRectBlockData, TEXT("DefaultRectBlockData"));
		UMaterialInterface* Material = Data != nullptr ? Data->GetMaterial() : nullptr;
		if (FMaterialResource* Resource = Material != nullptr ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr)
		{
			Resource->FinishCompilation();
			if (Resource->GetCompileErrors().Num() > 0)
			{
				AddError(FString::Printf(TEXT("The rect block's material %s does not compile: %s"), *Material->GetPathName(), *FString::Join(Resource->GetCompileErrors(), TEXT(" | "))));
			}
		}
		else
		{
			AddError(TEXT("The rect block's material was not found"));
		}
		Stage->UseBuiltInShader(false);
		// A rect block asks which draws it when its material is marked changed: the setting itself tells no one.
		for (const TWeakObjectPtr<UDreamRectBlock>& Block : *Blocks)
		{
			if (UDreamVisualBatchMesh* Element = Block.Get())
			{
				Element->MarkAllDirty();
			}
		}
		Stage->GetCanvas()->MarkCanvasUpdate(true);
	});
	// Redrawn by the material path before waiting for it to draw: the target holds the built-in picture until then.
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 20000);
	EnqueueFramesUntilStable(Stage);
	EnqueueReadBack(Stage, [this, BuiltIn, BuiltInSize](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		FDreamPixelProbe::SaveCapture(InPixels, InSize, TEXT("Gallery_LayerStyle_MaterialPath"));
		TestTrue(*FString::Printf(TEXT("The material draws the gallery (%d pixels drawn)"), CountDrawn(InPixels, InSize)), CountDrawn(InPixels, InSize) > 20000);
		if (TestTrue(TEXT("The built-in shader's picture was read back"), BuiltIn->Num() > 0))
		{
			FDreamPixelProbe::ExpectPicturesMatch(*this, *BuiltIn, *BuiltInSize, InPixels, InSize, TEXT("Gallery_LayerStyle_BuiltInAgainstMaterial"), 6, 0.005);
		}
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleGroupPixelTest,
	"DreamGUI.RHI.LayerStyle.AStyleDrawnApartIsRoundATextAnImageAndASubtreeAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLayerStyleGroupPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStylePixelTestLocal;
	const FIntPoint Size(384, 192);
	FStageRef Stage = BeginStage(*this, Size);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	// A backdrop of stripes, so that the modes of what lies behind show.
	UTexture2D* Stripes = Stage->MakeTexture(64, [](int32 X, int32 Y) { return (X / 8) % 2 == 0 ? FColor(150, 160, 170) : FColor(90, 100, 120); });
	Stage->AddBlock(TEXT("Backdrop"), FVector2D(Size.X, Size.Y), FVector2D::ZeroVector, FColor::White, Stripes);

	// A text with an outside stroke, a drop shadow in Multiply and a gradient on its glyphs.
	if (UDreamText* Text = Stage->AddText(TEXT("Title"), TEXT("STYLE"), 56.0f, FVector2D(220.0, 80.0), FVector2D(-70.0, 40.0), FColor::White))
	{
		FDreamLayerEffects S;
		S.DropShadows.Add(Shadow(120.0f, 6.0f, 6.0f));
		FDreamStyleStroke& Stroke = S.Strokes.AddDefaulted_GetRef();
		Stroke.Size = 3.0f;
		Stroke.Color = FColor(30, 20, 60);
		FDreamStyleGradientOverlay& Overlay = S.GradientOverlays.AddDefaulted_GetRef();
		Overlay.Fill.Gradient = FDreamStyleGradient::FromColors(FColor(255, 120, 60), FColor(255, 240, 120));
		Text->GetWidget()->AddComponent<UDreamLayerStyle>()->SetStyle(S);
	}
	// An image, a ring with a hole, with an outer glow in Screen and an inner shadow.
	UTexture2D* Ring = Stage->MakeTexture(64, [](int32 X, int32 Y)
	{
		const float R = FVector2f(X - 31.5f, Y - 31.5f).Size();
		return R < 28.0f && R > 14.0f ? FColor::White : FColor(255, 255, 255, 0);
	});
	if (UDreamWidget* Image = Stage->AddBlock(TEXT("Ring"), FVector2D(96.0, 96.0), FVector2D(120.0, 40.0), FColor(90, 200, 160), Ring))
	{
		FDreamLayerEffects S;
		S.OuterGlow.bEnabled = true;
		S.OuterGlow.Size = 12.0f;
		S.OuterGlow.Color = FColor(120, 255, 200);
		FDreamStyleInnerShadow& Inner = S.InnerShadows.AddDefaulted_GetRef();
		Inner.Distance = 4.0f;
		Inner.Size = 5.0f;
		Image->AddComponent<UDreamLayerStyle>()->SetStyle(S);
	}
	// A container of three overlapping blocks, styled as one: a bevel and a drop shadow round their union.
	UDreamWidget* Container = Stage->AddWidget(TEXT("Container"), FVector2D(200.0, 70.0), FVector2D(0.0, -55.0));
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Stage->AddBlock(*FString::Printf(TEXT("Part%d"), Index), FVector2D(60.0, 44.0), FVector2D(-50.0 + 50.0 * Index, Index == 1 ? 6.0 : -4.0), FColor(220, 120 + 40 * Index, 80), nullptr, Container);
	}
	{
		FDreamLayerEffects S;
		S.DropShadows.Add(Shadow(90.0f, 5.0f, 8.0f));
		S.BevelEmboss.bEnabled = true;
		S.BevelEmboss.Size = 7.0f;
		S.BevelEmboss.Technique = EDreamStyleBevelTechnique::ChiselSoft;
		Container->AddComponent<UDreamLayerStyle>()->SetStyle(S);
	}
	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_LayerStyle_Group"), 40000, EMissingGolden::Warn);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleApartAgainstElementPixelTest,
	"DreamGUI.RHI.LayerStyle.AStyleDrawnApartLooksAsItDoesInARectBlocksOwnPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLayerStyleApartAgainstElementPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStylePixelTestLocal;
	// The gallery twice: once each style on its rect block, once on a container holding the block, which is drawn apart and
	// measured by the jump flood. The two measure the same shape, one exactly and one from its pixels.
	TSharedRef<TArray<FColor>> Element = MakeShared<TArray<FColor>>();
	TSharedRef<FIntPoint> ElementSize = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
	for (const bool bApart : { false, true })
	{
		FStageRef Stage = BeginStage(*this, GallerySize);
		if (!Stage->IsUsable())
		{
			Stage->TearDown();
			return false;
		}
		Stage->UseBuiltInShader(true);
		const TArray<FDreamLayerEffects> Styles = GalleryStyles();
		for (int32 Index = 0; Index < Styles.Num(); ++Index)
		{
			if (!bApart)
			{
				AddStyledBlock(*Stage, *FString::Printf(TEXT("Styled%d"), Index), CellCentre(Index), Styles[Index]);
				continue;
			}
			UDreamWidget* Holder = Stage->AddWidget(*FString::Printf(TEXT("Holder%d"), Index), FVector2D(BlockHalf * 2.0, BlockHalf * 2.0), CellCentre(Index));
			UDreamWidget* Block = Stage->AddWidget(*FString::Printf(TEXT("Block%d"), Index), FVector2D(BlockHalf * 2.0, BlockHalf * 2.0), FVector2D::ZeroVector, Holder);
			if (UDreamRectBlock* Rect = Block->CreateNewVisual<UDreamRectBlock>())
			{
				Rect->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
				Rect->SetCornerRadius(FVector4(BlockCorner, BlockCorner, BlockCorner, BlockCorner));
				Rect->SetBodyColor(BlockColor);
			}
			Holder->AddComponent<UDreamLayerStyle>()->SetStyle(Styles[Index]);
		}
		EnqueueFramesUntilDrawn(Stage, 20000);
		EnqueueFramesUntilStable(Stage);
		EnqueueReadBack(Stage, [this, bApart, Element, ElementSize](const TArray<FColor>& InPixels, FIntPoint InSize)
		{
			if (!bApart)
			{
				*Element = InPixels;
				*ElementSize = InSize;
				return;
			}
			// The flood is half a texel out here and there, and its normals are its seeds': close, not the same.
			FDreamPixelProbe::ExpectPicturesMatch(*this, *Element, *ElementSize, InPixels, InSize, TEXT("Gallery_LayerStyle_ApartAgainstElement"), 24, 0.04);
		});
		EnqueueTearDown(Stage);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleJumpFloodTest,
	"DreamGUI.RHI.LayerStyle.TheJumpFloodIsTheExactDistanceTransformWithinATexel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamLayerStyleJumpFloodTest::RunTest(const FString& Parameters)
{
	// A shape with curves, corners, a hole and a thin bar, flooded on the GPU and transformed exactly on the CPU.
	const FIntPoint Size(160, 112);
	TArray<FColor> Source;
	TArray<bool> Inside;
	Source.SetNumUninitialized(Size.X * Size.Y);
	Inside.SetNumUninitialized(Size.X * Size.Y);
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			const FVector2f P(X + 0.5f, Y + 0.5f);
			const float Ring = (P - FVector2f(48.0f, 56.0f)).Size();
			const bool bRing = Ring < 36.0f && Ring > 14.0f;
			const bool bBox = P.X > 96.0f && P.X < 144.0f && P.Y > 20.0f && P.Y < 60.0f;
			const bool bBar = P.X > 100.0f && P.X < 150.0f && P.Y > 80.0f && P.Y < 83.0f;
			const bool bIn = bRing || bBox || bBar;
			Inside[Y * Size.X + X] = bIn;
			Source[Y * Size.X + X] = bIn ? FColor::White : FColor(0, 0, 0, 0);
		}
	}
	TArray<FLinearColor> Seeds;
	ENQUEUE_RENDER_COMMAND(DreamLayerStyleJumpFloodTest)([&Source, &Seeds, Size](FRHICommandListImmediate& RHICmdList)
	{
		const FRHITextureCreateDesc Desc = FRHITextureCreateDesc::Create2D(TEXT("DreamLayerStyleJumpFloodSource"), Size.X, Size.Y, PF_B8G8R8A8)
			.SetFlags(ETextureCreateFlags::ShaderResource)
			.SetInitialState(ERHIAccess::SRVMask);
		FTextureRHIRef SourceRHI = RHICmdList.CreateTexture(Desc);
		RHICmdList.UpdateTexture2D(SourceRHI, 0, FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y), Size.X * sizeof(FColor), reinterpret_cast<const uint8*>(Source.GetData()));
		TRefCountPtr<IPooledRenderTarget> Extracted;
		{
			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef SourceTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceRHI, TEXT("DreamLayerStyleJumpFloodSource")));
			FRDGTextureRef Flooded = FDreamUILayerStyleComposite::JumpFlood(GraphBuilder, GetGlobalShaderMap(GMaxRHIFeatureLevel), SourceTexture);
			if (Flooded != nullptr)
			{
				GraphBuilder.QueueTextureExtraction(Flooded, &Extracted);
			}
			GraphBuilder.Execute();
		}
		if (Extracted.IsValid())
		{
			RHICmdList.ReadSurfaceData(Extracted->GetRHI(), FIntRect(FIntPoint::ZeroValue, Size), Seeds, FReadSurfaceDataFlags(RCM_MinMax));
		}
	});
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The flood was read back"), Seeds.Num(), Size.X * Size.Y))
	{
		return false;
	}
	TArray<float> Exact;
	FDreamUILayerStyleMath::ExactDistance(Inside, Size.X, Size.Y, Exact);
	int32 Within = 0;
	float Worst = 0.0f;
	for (int32 Y = 0; Y < Size.Y; ++Y)
	{
		for (int32 X = 0; X < Size.X; ++X)
		{
			// As DreamUILayerStyleComposite.usf measures it at a texel's centre.
			const FLinearColor& S = Seeds[Y * Size.X + X];
			const FVector2f Q(X + 0.5f, Y + 0.5f);
			const float ToInside = S.R >= 0.0f ? (Q - FVector2f(S.R + 0.5f, S.G + 0.5f)).Size() : 1e6f;
			const float ToOutside = S.B >= 0.0f ? (Q - FVector2f(S.B + 0.5f, S.A + 0.5f)).Size() : 1e6f;
			const float Flood = ToInside <= ToOutside ? -(ToOutside - 0.5f) : ToInside - 0.5f;
			const float Error = FMath::Abs(Flood - Exact[Y * Size.X + X]);
			Worst = FMath::Max(Worst, Error);
			Within += Error <= 0.01f ? 1 : 0;
		}
	}
	const int32 Count = Size.X * Size.Y;
	TestTrue(*FString::Printf(TEXT("Nearly every texel's distance is the exact one (%d of %d)"), Within, Count), Within >= Count * 99 / 100);
	TestTrue(*FString::Printf(TEXT("...and none is more than a texel out (%.3f)"), Worst), Worst <= 1.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
