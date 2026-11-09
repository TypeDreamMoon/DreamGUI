// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Curves/CurveLinearColor.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIAdjustment.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIStylize.h"
#include "Extensions/Effects/DreamAdjustmentLayer.h"
#include "Extensions/Effects/DreamBackgroundBlur.h"
#include "Extensions/Effects/DreamGlass.h"
#include "Extensions/Effects/DreamStylizeEffect.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * The effects that work on what is behind them, drawn: every adjustment operation over a chart of colours, each pixel held
 * to the CPU's twin and the picture to its golden image; an adjustment of a widget's own children and one under a clip
 * mask's texture; a progressive blur as strong as its map; glass bending its edge and leaving its middle; and each
 * stylize layer. The stage is a render target canvas, whose target holds linear light.
 */
namespace DreamBackdropEffectPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** Sixteen colours, away from the levels where a posterize or a threshold turns. */
	const FColor Patches[16] = {
		FColor(230, 60, 40), FColor(40, 170, 90), FColor(60, 90, 220), FColor(205, 205, 205),
		FColor(250, 210, 60), FColor(100, 40, 160), FColor(240, 120, 200), FColor(20, 200, 220),
		FColor(35, 35, 45), FColor(150, 110, 70), FColor(90, 150, 30), FColor(170, 30, 100),
		FColor(225, 160, 120), FColor(70, 100, 140), FColor(110, 225, 170), FColor(245, 245, 235) };
	constexpr int32 Columns = 6;
	constexpr int32 Rows = 4;
	constexpr int32 Cell = 48;
	constexpr int32 Swatch = 40;
	const FIntPoint ChartSize(Columns * Cell, Rows * Cell);

	FVector2D CellCentre(int32 InIndex)
	{
		return FVector2D(-ChartSize.X * 0.5 + Cell * 0.5 + Cell * (InIndex % Columns), ChartSize.Y * 0.5 - Cell * 0.5 - Cell * (InIndex / Columns));
	}

	/** The pixel of the target at the middle of patch InPatch (four across, four down) of a cell. */
	FIntPoint PatchPixel(int32 InIndex, int32 InPatch)
	{
		const FVector2D Centre = CellCentre(InIndex);
		const double X = Centre.X - Swatch * 0.5 + Swatch / 8.0 + Swatch / 4.0 * (InPatch % 4);
		const double Y = Centre.Y + Swatch * 0.5 - Swatch / 8.0 - Swatch / 4.0 * (InPatch / 4);
		return FIntPoint(FMath::FloorToInt(X + ChartSize.X * 0.5), FMath::FloorToInt(ChartSize.Y * 0.5 - Y));
	}

	FColor PixelAt(const TArray<FColor>& InPixels, FIntPoint InSize, FVector2D InCanvas)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt(InCanvas.X + InSize.X * 0.5), 0, InSize.X - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt(InSize.Y * 0.5 - InCanvas.Y), 0, InSize.Y - 1);
		return InPixels[Y * InSize.X + X];
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

	FDreamAdjustment Make(EDreamAdjustmentType InType)
	{
		FDreamAdjustment Adjustment;
		Adjustment.Type = InType;
		return Adjustment;
	}

	/** A cell of the chart: what it is called, its operations, how they are mixed. */
	struct FChartCell
	{
		FString Name;
		TArray<FDreamAdjustment> Adjustments;
		float Opacity = 1.0f;
		EDreamUIBlendMode Mode = EDreamUIBlendMode::Alpha;
	};

	/** The lookup table the chart's lookup operation reads: each colour turned over, in the engine's layout. */
	TArray<FColor> MakeInvertingTable()
	{
		TArray<FColor> Texels;
		Texels.SetNumUninitialized(256 * 16);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 256; ++X)
			{
				const int32 R = FMath::RoundToInt((X % 16) * 255.0f / 15.0f);
				const int32 G = FMath::RoundToInt(Y * 255.0f / 15.0f);
				const int32 B = FMath::RoundToInt((X / 16) * 255.0f / 15.0f);
				Texels[Y * 256 + X] = FColor(static_cast<uint8>(255 - R), static_cast<uint8>(255 - G), static_cast<uint8>(255 - B), 255);
			}
		}
		return Texels;
	}

	UTexture2D* MakeTableTexture(FGalleryStage& InStage, const TArray<FColor>& InTexels)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(256, 16, PF_B8G8R8A8);
		if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		// Read as it is, filtered as the shader expects: no sRGB decode, bilinear.
		Texture->SRGB = false;
		Texture->Filter = TF_Bilinear;
		Texture->LODGroup = TEXTUREGROUP_ColorLookupTable;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FMemory::Memcpy(Mip.BulkData.Lock(LOCK_READ_WRITE), InTexels.GetData(), InTexels.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		InStage.KeepAlive(Texture);
		return Texture;
	}

	TArray<FChartCell> MakeChart(FGalleryStage& InStage, UTexture2D* InTable)
	{
		TArray<FChartCell> Cells;
		auto Add = [&Cells](const TCHAR* InName, TArray<FDreamAdjustment> InAdjustments, float InOpacity = 1.0f, EDreamUIBlendMode InMode = EDreamUIBlendMode::Alpha)
		{
			FChartCell& Added = Cells.AddDefaulted_GetRef();
			Added.Name = InName;
			Added.Adjustments = MoveTemp(InAdjustments);
			Added.Opacity = InOpacity;
			Added.Mode = InMode;
		};
		FDreamAdjustment A = Make(EDreamAdjustmentType::BrightnessContrast);
		A.Brightness = 0.08f;
		A.Contrast = 0.35f;
		Add(TEXT("BrightnessContrast"), { A });
		A = Make(EDreamAdjustmentType::Levels);
		A.InputBlack = 0.1f;
		A.InputWhite = 0.85f;
		A.Gamma = 1.4f;
		A.OutputBlack = 0.05f;
		A.OutputWhite = 0.95f;
		Add(TEXT("Levels"), { A });
		UCurveLinearColor* Curve = NewObject<UCurveLinearColor>(GetTransientPackage());
		InStage.KeepAlive(Curve);
		// Red an S, blue squeezed, green the new curve's own (eased) keys.
		Curve->FloatCurves[0].Reset();
		Curve->FloatCurves[2].Reset();
		Curve->FloatCurves[0].AddKey(0.0f, 0.0f);
		Curve->FloatCurves[0].AddKey(0.3f, 0.15f);
		Curve->FloatCurves[0].AddKey(0.7f, 0.85f);
		Curve->FloatCurves[0].AddKey(1.0f, 1.0f);
		Curve->FloatCurves[2].AddKey(0.0f, 0.2f);
		Curve->FloatCurves[2].AddKey(1.0f, 0.8f);
		A = Make(EDreamAdjustmentType::Curves);
		A.Curve = Curve;
		Add(TEXT("Curves"), { A });
		A = Make(EDreamAdjustmentType::Exposure);
		A.Exposure = 0.8f;
		A.ExposureGamma = 1.1f;
		Add(TEXT("Exposure"), { A });
		A = Make(EDreamAdjustmentType::HueSaturation);
		A.Hue = 60.0f;
		A.Saturation = 0.3f;
		A.Lightness = -0.1f;
		Add(TEXT("HueSaturation"), { A });
		A = Make(EDreamAdjustmentType::HueSaturation);
		A.bColorize = true;
		A.Hue = 200.0f;
		A.Saturation = 0.6f;
		A.Lightness = 0.1f;
		Add(TEXT("Colorize"), { A });
		A = Make(EDreamAdjustmentType::Vibrance);
		A.Vibrance = 0.7f;
		A.VibranceSaturation = -0.2f;
		Add(TEXT("Vibrance"), { A });
		A = Make(EDreamAdjustmentType::ColorBalance);
		A.Shadows = FVector3f(0.3f, 0.0f, -0.2f);
		A.Midtones = FVector3f(-0.1f, 0.2f, 0.0f);
		A.Highlights = FVector3f(0.0f, 0.0f, 0.3f);
		Add(TEXT("ColorBalance"), { A });
		A = Make(EDreamAdjustmentType::BlackAndWhite);
		A.TintStrength = 0.4f;
		Add(TEXT("BlackAndWhite"), { A });
		A = Make(EDreamAdjustmentType::PhotoFilter);
		A.Density = 0.5f;
		Add(TEXT("PhotoFilter"), { A });
		A = Make(EDreamAdjustmentType::ChannelMixer);
		A.RedMix = FVector3f(0.2f, 0.7f, 0.1f);
		A.BlueMix = FVector3f(0.5f, 0.0f, 0.5f);
		A.MixConstants = FVector3f(0.05f, 0.0f, 0.0f);
		Add(TEXT("ChannelMixer"), { A });
		Add(TEXT("Invert"), { Make(EDreamAdjustmentType::Invert) });
		A = Make(EDreamAdjustmentType::Posterize);
		A.PosterizeLevels = 4;
		Add(TEXT("Posterize"), { A });
		A = Make(EDreamAdjustmentType::Threshold);
		A.ThresholdLevel = 0.5f;
		Add(TEXT("Threshold"), { A });
		A = Make(EDreamAdjustmentType::GradientMap);
		A.Gradient.Stops = { FDreamGradientStop(0.0f, FColor(20, 30, 80)), FDreamGradientStop(0.5f, FColor(230, 120, 40)), FDreamGradientStop(1.0f, FColor(250, 240, 210)) };
		Add(TEXT("GradientMap"), { A });
		A = Make(EDreamAdjustmentType::LookupTable);
		A.LookupTable = InTable;
		A.Intensity = 0.75f;
		Add(TEXT("LookupTable"), { A });
		FDreamAdjustment Turn = Make(EDreamAdjustmentType::HueSaturation);
		Turn.Hue = -90.0f;
		Add(TEXT("InvertThenHue"), { Make(EDreamAdjustmentType::Invert), Turn });
		Add(TEXT("InvertDifferenceHalf"), { Make(EDreamAdjustmentType::Invert) }, 0.6f, EDreamUIBlendMode::Difference);
		A = Make(EDreamAdjustmentType::BrightnessContrast);
		A.Brightness = 0.2f;
		Add(TEXT("BrightenMultiply"), { A }, 1.0f, EDreamUIBlendMode::Multiply);
		A = Make(EDreamAdjustmentType::HueSaturation);
		A.Saturation = -1.0f;
		Add(TEXT("DesaturateColor"), { A }, 0.8f, EDreamUIBlendMode::Luminosity);
		return Cells;
	}

	FVector3f Unit(const FColor& InColour)
	{
		return FVector3f(InColour.R / 255.0f, InColour.G / 255.0f, InColour.B / 255.0f);
	}

	FColor ToColor(const FVector4f& InLinear)
	{
		const FVector3f Encoded = FDreamUIBlendMath::ToSrgb(FVector3f(InLinear.X, InLinear.Y, InLinear.Z));
		return FColor(FMath::RoundToInt(Encoded.X * 255.0f), FMath::RoundToInt(Encoded.Y * 255.0f), FMath::RoundToInt(Encoded.Z * 255.0f), 255);
	}

	/** What the target holds where a cell's adjustment covers InPatch: the patch in linear light, through the CPU's adjust pass. */
	FColor Expected(const FDreamUIAdjustPacked& InPacked, const FColor& InPatch, FIntPoint InPixel, const TArray<FColor>* InTable)
	{
		const FVector3f Linear = FDreamUIBlendMath::ToLinear(Unit(InPatch));
		return ToColor(FDreamUIAdjustMath::ApplyToPixel(InPacked, FVector4f(Linear.X, Linear.Y, Linear.Z, 1.0f), true, false, InPixel, InTable));
	}

	/** Whether a channel's step of the input moves the answer by more than InStep: an edge the GPU and the CPU may round apart. */
	bool IsNearAnEdge(const FDreamUIAdjustPacked& InPacked, const FColor& InPatch, FIntPoint InPixel, const TArray<FColor>* InTable, int32 InStep)
	{
		const FColor Here = Expected(InPacked, InPatch, InPixel, InTable);
		for (int32 Channel = 0; Channel < 3; ++Channel)
		{
			for (const int32 Delta : { -1, 1 })
			{
				FColor Moved = InPatch;
				uint8& Value = Channel == 0 ? Moved.R : (Channel == 1 ? Moved.G : Moved.B);
				Value = static_cast<uint8>(FMath::Clamp(Value + Delta, 0, 255));
				if (!FDreamPixelProbe::IsNear(Expected(InPacked, Moved, InPixel, InTable), Here, static_cast<uint8>(InStep)))
				{
					return true;
				}
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAdjustChartPixelTest,
	"DreamGUI.RHI.Adjust.EveryOperationDrawsWhatTheCPUSaysAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamAdjustChartPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectPixelTestLocal;
	FStageRef Stage = BeginStage(*this, ChartSize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	const TSharedRef<TArray<FColor>> Table = MakeShared<TArray<FColor>>(MakeInvertingTable());
	UTexture2D* TableTexture = MakeTableTexture(*Stage, *Table);
	UTexture2D* Chart = Stage->MakeTexture(64, [](int32 X, int32 Y) { return Patches[(Y / 16) * 4 + X / 16]; });
	const TArray<FChartCell> Cells = MakeChart(*Stage, TableTexture);
	TSharedRef<TArray<TWeakObjectPtr<UDreamAdjustmentLayer>>> Layers = MakeShared<TArray<TWeakObjectPtr<UDreamAdjustmentLayer>>>();
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		Stage->AddBlock(*FString::Printf(TEXT("Chart%d"), Index), FVector2D(Swatch, Swatch), CellCentre(Index), FColor::White, Chart);
	}
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		UDreamAdjustmentLayer* Layer = Stage->AddWidget(*Cells[Index].Name, FVector2D(Swatch, Swatch), CellCentre(Index))->CreateNewVisual<UDreamAdjustmentLayer>();
		if (Layer != nullptr)
		{
			Layer->SetAdjustments(Cells[Index].Adjustments);
			Layer->SetOpacity(Cells[Index].Opacity);
			Layer->SetEffectBlendMode(Cells[Index].Mode);
		}
		Layers->Add(Layer);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Adjust_Operations"), 20000);
	EnqueueReadBack(Stage, [this, Cells, Layers, Table](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		int32 Wrong = 0;
		int32 Checked = 0;
		int32 Skipped = 0;
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			const UDreamAdjustmentLayer* Layer = (*Layers)[Index].Get();
			if (!TestNotNull(*FString::Printf(TEXT("The %s layer"), *Cells[Index].Name), Layer))
			{
				continue;
			}
			FDreamUIAdjustPacked Packed;
			Layer->PackAdjustments(Packed);
			for (int32 Patch = 0; Patch < 16; ++Patch)
			{
				const FIntPoint Pixel = PatchPixel(Index, Patch);
				if (IsNearAnEdge(Packed, Patches[Patch], Pixel, &*Table, 6))
				{
					++Skipped;
					continue;
				}
				const FColor Want = Expected(Packed, Patches[Patch], Pixel, &*Table);
				const FColor Got = InPixels[Pixel.Y * InSize.X + Pixel.X];
				++Checked;
				if (!FDreamPixelProbe::IsNear(Got, Want, 3) && ++Wrong <= 24)
				{
					AddError(FString::Printf(TEXT("%s over %s at (%d, %d): %s, the CPU says %s"), *Cells[Index].Name, *FDreamPixelProbe::Describe(Patches[Patch])
						, Pixel.X, Pixel.Y, *FDreamPixelProbe::Describe(Got), *FDreamPixelProbe::Describe(Want)));
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%d patches held to the CPU, %d left out for lying on an edge of their operation"), Checked, Skipped));
		TestEqual(TEXT("Every operation on every patch is what the CPU says"), Wrong, 0);
		TestTrue(TEXT("...and nearly all of them were held to it"), Checked >= Cells.Num() * 16 - 24);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAdjustTargetsPixelTest,
	"DreamGUI.RHI.Adjust.AnAdjustmentOfItsChildrenAndOnesUnderATextureMaskChangeOnlyWhatTheyAreFor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamAdjustTargetsPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	UTexture2D* Stripes = Stage->MakeTexture(64, [](int32 X, int32 Y) { return Patches[X / 16]; });
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Stripes);
	// Top left: a container whose own children are inverted -- the stripes between them are not.
	UDreamWidget* Children = Stage->AddWidget(TEXT("Children"), FVector2D(110.0, 110.0), FVector2D(-64.0, 64.0));
	Stage->AddBlock(TEXT("Yellow"), FVector2D(40.0, 40.0), FVector2D(-25.0, 25.0), FColor(250, 210, 60), nullptr, Children);
	Stage->AddBlock(TEXT("Blue"), FVector2D(40.0, 40.0), FVector2D(25.0, -25.0), FColor(60, 90, 220), nullptr, Children);
	if (UDreamAdjustmentLayer* Layer = Children->CreateNewVisual<UDreamAdjustmentLayer>())
	{
		Layer->SetAdjustments({ Make(EDreamAdjustmentType::Invert) });
		Layer->SetTarget(EDreamEffectTarget::Children);
	}
	// Top right: an inversion of what is behind, in a container whose clip mask is a texture white on its left half alone.
	UTexture2D* Half = Stage->MakeTexture(32, [](int32 X, int32 Y) { return X < 16 ? FColor::White : FColor::Black; });
	auto MaskedContainer = [&Stage, Half](const TCHAR* InName, FVector2D InPosition)
	{
		UDreamWidget* Masked = Stage->AddWidget(InName, FVector2D(110.0, 110.0), InPosition);
		Masked->SetClipping(EDreamWidgetClipping::ClipToBounds);
		FDreamClipMask Mask;
		Mask.Source = EDreamClipMaskSource::Texture;
		Mask.Texture = Half;
		Mask.Channel = EDreamClipMaskChannel::Red;
		Masked->SetClipMask(Mask);
		return Masked;
	};
	UDreamWidget* Masked = MaskedContainer(TEXT("Masked"), FVector2D(64.0, 64.0));
	if (UDreamAdjustmentLayer* Layer = Stage->AddWidget(TEXT("MaskedInvert"), FVector2D(110.0, 110.0), FVector2D::ZeroVector, Masked)->CreateNewVisual<UDreamAdjustmentLayer>())
	{
		Layer->SetAdjustments({ Make(EDreamAdjustmentType::Invert) });
	}
	// Bottom: a background blur in the same kind of container, which blurs its left half alone: the stripes' edge at x = 0
	// is on that half, the one at x = 64 on the other.
	UDreamWidget* Blurred = MaskedContainer(TEXT("Blurred"), FVector2D(32.0, -64.0));
	if (UDreamBackgroundBlur* Blur = Stage->AddWidget(TEXT("MaskedBlur"), FVector2D(110.0, 110.0), FVector2D::ZeroVector, Blurred)->CreateNewVisual<UDreamBackgroundBlur>())
	{
		Blur->SetBlurStrength(1.0f);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Adjust_Targets"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		auto Inverted = [](const FColor& C) { return FColor(255 - C.R, 255 - C.G, 255 - C.B, 255); };
		auto Stripe = [](double InX) { return Patches[FMath::Clamp(FMath::FloorToInt((InX + 128.0) / 64.0), 0, 3)]; };
		// The children inverted, the stripes between them as they were.
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(128 - 64 - 25, 128 - 64 - 25), Inverted(FColor(250, 210, 60)), 3, TEXT("The yellow child is inverted"));
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(128 - 64 + 25, 128 - 64 + 25), Inverted(FColor(60, 90, 220)), 3, TEXT("The blue child is inverted"));
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(128 - 64 + 25, 128 - 64 - 25), Stripe(-64.0 + 25.0), 3, TEXT("Between them the stripes are as they were"));
		// The masked inversion: inverted on the mask's white half, untouched on its black half.
		const double Left = 64.0 - 30.0;
		const double Right = 64.0 + 30.0;
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(Left + 128.0), 128 - 64), Inverted(Stripe(Left)), 3, TEXT("Under the mask's white half, inverted"));
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(Right + 128.0), 128 - 64), Stripe(Right), 3, TEXT("Under its black half, as it was"));
		// The masked blur: the stripes' edge at x = 0 smeared, the one at x = 64 kept sharp.
		const FColor SharpLeft = PixelAt(InPixels, InSize, FVector2D(64.0 - 2.0, -64.0));
		const FColor SharpRight = PixelAt(InPixels, InSize, FVector2D(64.0 + 2.0, -64.0));
		TestTrue(FString::Printf(TEXT("Under the black half the stripes' edge stays sharp: %s against %s"), *FDreamPixelProbe::Describe(SharpLeft), *FDreamPixelProbe::Describe(SharpRight))
			, FDreamPixelProbe::IsNear(SharpLeft, Stripe(62.0), 4) && FDreamPixelProbe::IsNear(SharpRight, Stripe(66.0), 4));
		const FColor SoftLeft = PixelAt(InPixels, InSize, FVector2D(-2.0, -64.0));
		TestFalse(FString::Printf(TEXT("Under the white half the edge at the middle is smeared: %s"), *FDreamPixelProbe::Describe(SoftLeft)), FDreamPixelProbe::IsNear(SoftLeft, Stripe(-2.0), 8));
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProgressiveBlurPixelTest,
	"DreamGUI.RHI.Blur.AProgressiveBlurIsAsStrongAsItsMapSaysAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamProgressiveBlurPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	UTexture2D* Checker = Stage->MakeChecker(64, 2, FColor(30, 30, 40), FColor(235, 225, 200));
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Checker);
	// Left: frosted at the top, clear at the bottom. Right: clear in the middle, frosted round it.
	FDreamBlurStrengthMap Linear;
	Linear.Kind = EDreamBlurStrengthMapKind::Linear;
	Linear.Start = FVector2D(0.5, 0.0);
	Linear.End = FVector2D(0.5, 1.0);
	Linear.StartStrength = 1.0f;
	Linear.EndStrength = 0.0f;
	if (UDreamBackgroundBlur* Blur = Stage->AddWidget(TEXT("Linear"), FVector2D(110.0, 240.0), FVector2D(-64.0, 0.0))->CreateNewVisual<UDreamBackgroundBlur>())
	{
		Blur->SetBlurStrength(1.0f);
		Blur->SetStrengthMap(Linear);
	}
	FDreamBlurStrengthMap Radial;
	Radial.Kind = EDreamBlurStrengthMapKind::Radial;
	Radial.Start = FVector2D(0.5, 0.5);
	Radial.InnerRadius = 0.1f;
	Radial.OuterRadius = 0.45f;
	Radial.StartStrength = 0.0f;
	Radial.EndStrength = 1.0f;
	if (UDreamBackgroundBlur* Blur = Stage->AddWidget(TEXT("Radial"), FVector2D(110.0, 240.0), FVector2D(64.0, 0.0))->CreateNewVisual<UDreamBackgroundBlur>())
	{
		Blur->SetBlurStrength(1.0f);
		Blur->SetStrengthMap(Radial);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Blur_Progressive"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// How much a patch of the checker still varies: the stronger the blur, the less.
		auto Contrast = [&InPixels, InSize](FVector2D InCentre)
		{
			float Min = 255.0f;
			float Max = 0.0f;
			for (int32 Dy = -4; Dy <= 4; ++Dy)
			{
				for (int32 Dx = -4; Dx <= 4; ++Dx)
				{
					const FColor C = PixelAt(InPixels, InSize, InCentre + FVector2D(Dx, Dy));
					const float L = 0.3f * C.R + 0.59f * C.G + 0.11f * C.B;
					Min = FMath::Min(Min, L);
					Max = FMath::Max(Max, L);
				}
			}
			return Max - Min;
		};
		const float Bottom = Contrast(FVector2D(-64.0, -100.0));
		const float Lower = Contrast(FVector2D(-64.0, -40.0));
		const float Upper = Contrast(FVector2D(-64.0, 40.0));
		const float Top = Contrast(FVector2D(-64.0, 100.0));
		AddInfo(FString::Printf(TEXT("Linear map, the checker's contrast from the bottom up: %.1f, %.1f, %.1f, %.1f"), Bottom, Lower, Upper, Top));
		TestTrue(TEXT("Where the map is 0 the checker is as sharp as it was"), Bottom > 150.0f);
		TestTrue(TEXT("...and it blurs more the further up the map rises"), Bottom > Lower && Lower > Upper && Upper >= Top);
		TestTrue(TEXT("...to almost nothing at the top"), Top < 30.0f);
		const float Middle = Contrast(FVector2D(64.0, 0.0));
		const float Edge = Contrast(FVector2D(64.0, 100.0));
		AddInfo(FString::Printf(TEXT("Radial map: %.1f in the middle, %.1f near the edge"), Middle, Edge));
		TestTrue(TEXT("The radial map leaves its middle sharp and frosts round it"), Middle > 150.0f && Edge < 40.0f);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlassPixelTest,
	"DreamGUI.RHI.Glass.GlassBendsItsEdgeLeavesItsMiddleAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGlassPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	// A checker of eight-unit squares: what is moved by a few units shows another square's colour.
	UTexture2D* Fine = Stage->MakeChecker(64, 2, Patches[2], Patches[4]);
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Fine);
	// Clear glass, nothing but its refraction: what its flat middle shows is what is behind it.
	if (UDreamGlass* Clear = Stage->AddWidget(TEXT("Clear"), FVector2D(200.0, 90.0), FVector2D(0.0, 60.0))->CreateNewVisual<UDreamGlass>())
	{
		Clear->SetCornerRadius(FVector4f(30.0f, 30.0f, 30.0f, 30.0f));
		Clear->SetEdgeWidth(20.0f);
		Clear->SetRefraction(14.0f);
		Clear->SetDispersion(0.2f);
		Clear->SetSaturation(1.0f);
		Clear->SetBrightness(0.0f);
		Clear->SetHighlight(FColor::White, 0.0f, 0.0f, 0.0f);
		Clear->SetInnerShadow(0.0f, 0.0f);
	}
	// Frosted glass, tinted, lit from the top left.
	if (UDreamGlass* Frosted = Stage->AddWidget(TEXT("Frosted"), FVector2D(200.0, 90.0), FVector2D(0.0, -60.0))->CreateNewVisual<UDreamGlass>())
	{
		Frosted->SetCornerRadius(FVector4f(45.0f, 45.0f, 45.0f, 45.0f));
		Frosted->SetFrost(0.5f);
		Frosted->SetGlassTint(FColor(210, 230, 255), 0.2f);
		Frosted->SetHighlight(FColor::White, 0.7f, 4.0f, 315.0f);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Glass"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// The scene as the texture paints it, at a canvas point: four units to a texel, two texels to a square.
		auto Scene = [](FVector2D InCanvas)
		{
			const int32 X = FMath::Clamp(FMath::FloorToInt((InCanvas.X + 128.0) / 4.0), 0, 63);
			const int32 Y = FMath::Clamp(FMath::FloorToInt((128.0 - InCanvas.Y) / 4.0), 0, 63);
			return ((X / 2) + (Y / 2)) % 2 == 0 ? Patches[2] : Patches[4];
		};
		// Its middle, far from the band along its edge, shows what is behind as it is.
		for (const FVector2D& Point : { FVector2D(-2.0, 62.0), FVector2D(14.0, 58.0), FVector2D(-30.0, 66.0) })
		{
			FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(Point.X + 128.0), FMath::FloorToInt(128.0 - Point.Y)), Scene(Point), 3
				, *FString::Printf(TEXT("The clear glass's middle at %s shows what is behind it"), *Point.ToString()));
		}
		// Outside its rounded corner, the rect's corner shows what is behind untouched.
		const FVector2D Corner(-99.0, 60.0 + 44.0);
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(Corner.X + 128.0), FMath::FloorToInt(128.0 - Corner.Y)), Scene(Corner), 3
			, TEXT("Outside the rounded corner, nothing of the glass"));
		// Along its bottom edge, a unit in, what shows is moved up by several units: mostly another square of the checker.
		int32 Moved = 0;
		for (int32 X = -90; X <= 90; X += 3)
		{
			const FVector2D Point(X + 0.5, 60.0 - 44.0);
			if (!FDreamPixelProbe::IsNear(PixelAt(InPixels, InSize, Point), Scene(Point), 6))
			{
				++Moved;
			}
		}
		TestTrue(FString::Printf(TEXT("Along the edge what shows is bent (%d of 61 points differ from what is behind)"), Moved), Moved >= 20);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlassShapeBlendPixelTest,
	"DreamGUI.RHI.Glass.GlassInAShapeBlendBendsTheMeltedEdgeWhereverItReachesAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGlassShapeBlendPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	UTexture2D* Fine = Stage->MakeChecker(64, 2, Patches[2], Patches[4]);
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Fine);
	// Rect blocks with no colour of their own melted into one shape, and glass in that shape: a widget of its own, much
	// narrower than the shape, and drawn over all of it.
	auto AddMember = [&Stage](UDreamWidget* InGroup, const TCHAR* InName, FVector2D InSize, FVector2D InPosition, float InCorner
		, EDreamShapeBlendOperation InOperation = EDreamShapeBlendOperation::Union)
	{
		if (UDreamRectBlock* Block = Stage->AddWidget(InName, InSize, InPosition, InGroup)->CreateNewVisual<UDreamRectBlock>())
		{
			Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetCornerRadius(FVector4(InCorner, InCorner, InCorner, InCorner));
			Block->SetBodyColor(FColor(255, 255, 255, 0));
			Block->SetShapeBlendOperation(InOperation);
		}
	};
	// Above: two blocks a gap of 2 apart, melted into a waist, clear glass. Their outer ends, at 91, are three units short of
	// a square of the checker, about what the edge bends the light in by a unit and a half in: a pixel there shows the next
	// square.
	UDreamWidget* Pair = Stage->AddWidget(TEXT("Pair"), FVector2D(240.0, 100.0), FVector2D(0.0, 64.0));
	AddMember(Pair, TEXT("PairLeft"), FVector2D(90.0, 50.0), FVector2D(-46.0, 0.0), 12.0f);
	AddMember(Pair, TEXT("PairRight"), FVector2D(90.0, 50.0), FVector2D(46.0, 0.0), 12.0f);
	if (UDreamShapeBlendGroup* Blend = Pair->AddComponent<UDreamShapeBlendGroup>())
	{
		Blend->SetSmoothness(40.0f);
	}
	if (UDreamGlass* Clear = Stage->AddWidget(TEXT("ClearGlass"), FVector2D(60.0, 100.0), FVector2D(0.0, 64.0))->CreateNewVisual<UDreamGlass>())
	{
		Clear->SetShape(EDreamGlassShape::ShapeBlend);
		Clear->SetShapeBlendGroup(Pair);
		Clear->SetEdgeWidth(18.0f);
		Clear->SetRefraction(12.0f);
		Clear->SetDispersion(0.2f);
		Clear->SetSaturation(1.0f);
		Clear->SetBrightness(0.0f);
		// A thin highlight, so that the melted outline shows: it reaches two units in, short of the middles held below.
		Clear->SetHighlight(FColor::White, 0.5f, 2.0f, 315.0f);
		Clear->SetInnerShadow(0.0f, 0.0f);
	}
	// Below: three drops, one with a hole cut out of it, frosted, tinted and lit glass.
	UDreamWidget* Drops = Stage->AddWidget(TEXT("Drops"), FVector2D(240.0, 110.0), FVector2D(0.0, -64.0));
	AddMember(Drops, TEXT("DropLeft"), FVector2D(70.0, 70.0), FVector2D(-62.0, 4.0), 24.0f);
	AddMember(Drops, TEXT("DropMiddle"), FVector2D(64.0, 64.0), FVector2D(10.0, -8.0), 32.0f);
	AddMember(Drops, TEXT("DropHole"), FVector2D(26.0, 26.0), FVector2D(10.0, -8.0), 13.0f, EDreamShapeBlendOperation::Subtract);
	AddMember(Drops, TEXT("DropRight"), FVector2D(40.0, 40.0), FVector2D(78.0, 18.0), 20.0f);
	if (UDreamShapeBlendGroup* Blend = Drops->AddComponent<UDreamShapeBlendGroup>())
	{
		Blend->SetSmoothness(22.0f);
	}
	if (UDreamGlass* Frosted = Stage->AddWidget(TEXT("FrostedGlass"), FVector2D(40.0, 40.0), FVector2D(0.0, -64.0))->CreateNewVisual<UDreamGlass>())
	{
		Frosted->SetShape(EDreamGlassShape::ShapeBlend);
		Frosted->SetShapeBlendGroup(Drops);
		Frosted->SetFrost(0.45f);
		Frosted->SetGlassTint(FColor(210, 230, 255), 0.2f);
		Frosted->SetHighlight(FColor::White, 0.7f, 4.0f, 315.0f);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Glass_ShapeBlend"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		auto Scene = [](FVector2D InCanvas)
		{
			const int32 X = FMath::Clamp(FMath::FloorToInt((InCanvas.X + 128.0) / 4.0), 0, 63);
			const int32 Y = FMath::Clamp(FMath::FloorToInt((128.0 - InCanvas.Y) / 4.0), 0, 63);
			return ((X / 2) + (Y / 2)) % 2 == 0 ? Patches[2] : Patches[4];
		};
		auto Expect = [this, &InPixels, InSize, &Scene](FVector2D InPoint, const TCHAR* InWhat)
		{
			FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(InPoint.X + 128.0), FMath::FloorToInt(128.0 - InPoint.Y)), Scene(InPoint), 3
				, *FString::Printf(TEXT("%s, at %s"), InWhat, *InPoint.ToString()));
		};
		// The members' middles, far past the glass's own rect (60 wide) and far from the shape's edge: flat glass shows what
		// is behind as it is.
		for (const FVector2D& Point : { FVector2D(-62.0, 64.0), FVector2D(-70.0, 66.0), FVector2D(62.0, 62.0), FVector2D(70.0, 64.0) })
		{
			Expect(Point, TEXT("A member's middle, outside the glass's rect, is clear glass"));
		}
		// The glass's own rect where the shape is not: nothing of the glass.
		for (const FVector2D& Point : { FVector2D(-2.0, 110.0), FVector2D(14.0, 18.0) })
		{
			Expect(Point, TEXT("Its own rect off the shape shows what is behind"));
		}
		// The shape's outer ends, sixty units past the glass's rect, a unit and a half in along their straight run: their
		// edge bends what is behind in from the next square.
		for (const double Edge : { -89.5, 89.5 })
		{
			int32 Moved = 0, Points = 0;
			for (double Y = 52.0; Y <= 76.0; Y += 2.0)
			{
				const FVector2D Point(Edge, Y + 0.5);
				Moved += FDreamPixelProbe::IsNear(PixelAt(InPixels, InSize, Point), Scene(Point), 6) ? 0 : 1;
				++Points;
			}
			TestTrue(FString::Printf(TEXT("The shape's end at x %.1f, past the glass's rect, is glass: what shows is bent (%d of %d points differ)"), Edge, Moved, Points), Moved >= Points / 2);
		}
		// Past the shape's ends, nothing.
		Expect(FVector2D(-112.0, 64.0), TEXT("Past the shape's left end"));
		Expect(FVector2D(112.0, 64.0), TEXT("Past its right end"));
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStylizePixelTest,
	"DreamGUI.RHI.Stylize.EachLayerDrawsWhatItShouldAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamStylizePixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectPixelTestLocal;
	const FIntPoint Size(240, 240);
	FStageRef Stage = BeginStage(*this, Size);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	// A colourful scene, and three flat squares for the layers held to the CPU.
	UTexture2D* Scene = Stage->MakeTexture(64, [](int32 X, int32 Y) { return FColor(static_cast<uint8>(X * 4), static_cast<uint8>(Y * 4), static_cast<uint8>(255 - X * 2), 255); });
	Stage->AddBlock(TEXT("Scene"), FVector2D(Size), FVector2D::ZeroVector, FColor::White, Scene);
	const FColor Flat(150, 90, 40);
	auto CellCentreOf = [](int32 InIndex) { return FVector2D(-80.0 + 80.0 * (InIndex % 3), 80.0 - 80.0 * (InIndex / 3)); };
	for (const int32 Index : { 5, 6, 8 })
	{
		Stage->AddBlock(*FString::Printf(TEXT("Flat%d"), Index), FVector2D(72.0, 72.0), CellCentreOf(Index), Flat);
	}
	// A checker under the CRT, the glitch and the aberration, which show on edges.
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(30, 40, 60), FColor(235, 225, 200));
	for (const int32 Index : { 0, 1, 2 })
	{
		Stage->AddBlock(*FString::Printf(TEXT("Checker%d"), Index), FVector2D(72.0, 72.0), CellCentreOf(Index), FColor::White, Checker);
	}
	TArray<FDreamStylizeLayer> Cells;
	FDreamStylizeLayer Layer;
	Layer.Type = EDreamStylizeType::CRT;
	Layer.FlickerStrength = 0.0f;
	Cells.Add(Layer);
	Layer = FDreamStylizeLayer();
	Layer.Type = EDreamStylizeType::Glitch;
	Layer.Rate = 0.0f;
	Layer.GlitchIntensity = 1.0f;
	Layer.Seed = 3;
	Cells.Add(Layer);
	Layer = FDreamStylizeLayer();
	Layer.Type = EDreamStylizeType::ChromaticAberration;
	Layer.AberrationAmount = 4.0f;
	Cells.Add(Layer);
	Layer = FDreamStylizeLayer();
	Layer.Type = EDreamStylizeType::Halftone;
	Cells.Add(Layer);
	Layer.bCMYK = true;
	Cells.Add(Layer);
	Layer = FDreamStylizeLayer();
	Layer.Type = EDreamStylizeType::Dither;
	Layer.Levels = 3;
	Cells.Add(Layer);
	Layer = FDreamStylizeLayer();
	Layer.Type = EDreamStylizeType::Posterize;
	Layer.Levels = 3;
	Cells.Add(Layer);
	Layer = FDreamStylizeLayer();
	Layer.Type = EDreamStylizeType::FilmGrain;
	Layer.bAnimateGrain = false;
	Layer.GrainIntensity = 0.25f;
	Cells.Add(Layer);
	Layer = FDreamStylizeLayer();
	Layer.Type = EDreamStylizeType::Vignette;
	Layer.VignetteIntensity = 1.0f;
	Layer.VignetteRadius = 0.3f;
	Layer.VignetteSoftness = 0.6f;
	Cells.Add(Layer);
	for (int32 Index = 0; Index < Cells.Num(); ++Index)
	{
		if (UDreamStylizeEffect* Effect = Stage->AddWidget(*FString::Printf(TEXT("Stylize%d"), Index), FVector2D(72.0, 72.0), CellCentreOf(Index))->CreateNewVisual<UDreamStylizeEffect>())
		{
			Effect->SetLayers({ Cells[Index] });
		}
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Stylize"), 30000);
	EnqueueReadBack(Stage, [this, Flat, CellCentreOf, Size](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// The stage's target holds linear light: the layers work on the encoded colour, which is the flat square's own.
		auto ToByte = [](float X) { return static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(X, 0.0f, 1.0f) * 255.0f)); };
		const FVector3f C(Flat.R / 255.0f, Flat.G / 255.0f, Flat.B / 255.0f);
		// Dither, three levels, through the 4 by 4 matrix laid on the target's pixels.
		int32 DitherWrong = 0;
		const FVector2D DitherCentre = CellCentreOf(5);
		for (int32 Dy = -12; Dy < 12; ++Dy)
		{
			for (int32 Dx = -12; Dx < 12; ++Dx)
			{
				const FIntPoint Pixel(FMath::FloorToInt(DitherCentre.X + Size.X * 0.5) + Dx, FMath::FloorToInt(Size.Y * 0.5 - DitherCentre.Y) + Dy);
				const float T = FDreamUIStylizeMath::Bayer(Pixel.X, Pixel.Y, 4);
				const FColor Want(ToByte(FDreamUIStylizeMath::Dither(C.X, 3.0f, T)), ToByte(FDreamUIStylizeMath::Dither(C.Y, 3.0f, T)), ToByte(FDreamUIStylizeMath::Dither(C.Z, 3.0f, T)), 255);
				if (!FDreamPixelProbe::IsNear(InPixels[Pixel.Y * InSize.X + Pixel.X], Want, 3) && ++DitherWrong <= 8)
				{
					AddError(FString::Printf(TEXT("Dither at (%d, %d): %s, the CPU says %s"), Pixel.X, Pixel.Y, *FDreamPixelProbe::Describe(InPixels[Pixel.Y * InSize.X + Pixel.X]), *FDreamPixelProbe::Describe(Want)));
				}
			}
		}
		TestEqual(TEXT("The dither is the Bayer matrix's, pixel for pixel"), DitherWrong, 0);
		// Posterize to three levels: 0, a half or 1 a channel.
		auto Posterized = [](float X) { return FMath::Min(FMath::FloorToFloat(X * 3.0f), 2.0f) / 2.0f; };
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(CellCentreOf(6).X + Size.X * 0.5), FMath::FloorToInt(Size.Y * 0.5 - CellCentreOf(6).Y))
			, FColor(ToByte(Posterized(C.X)), ToByte(Posterized(C.Y)), ToByte(Posterized(C.Z)), 255), 3, TEXT("Posterized to three levels"));
		// The vignette: the middle untouched, a corner all but black.
		const FVector2D VignetteCentre = CellCentreOf(8);
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(VignetteCentre.X + Size.X * 0.5), FMath::FloorToInt(Size.Y * 0.5 - VignetteCentre.Y))
			, Flat, 3, TEXT("The vignette leaves the middle"));
		const float Corner = FDreamUIStylizeMath::Vignette(FVector2f(0.9f, 0.9f), 1.0f, 1.0f, 0.3f, 0.6f, 1.0f);
		const FColor CornerWant(ToByte(C.X * (1.0f - Corner)), ToByte(C.Y * (1.0f - Corner)), ToByte(C.Z * (1.0f - Corner)), 255);
		FDreamPixelProbe::ExpectColorAt(*this, InPixels, InSize, FIntPoint(FMath::FloorToInt(VignetteCentre.X + 36.0 * 0.9 + Size.X * 0.5), FMath::FloorToInt(Size.Y * 0.5 - VignetteCentre.Y + 36.0 * 0.9))
			, CornerWant, 6, TEXT("...and darkens towards the corner as the CPU says"));
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
