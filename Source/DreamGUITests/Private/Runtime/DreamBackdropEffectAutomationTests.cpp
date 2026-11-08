// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Curves/CurveLinearColor.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIAdjustment.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIStylize.h"
#include "DreamUIRender/DreamUICompositor.h"
#include "Extensions/Effects/DreamAdjustmentLayer.h"
#include "Extensions/Effects/DreamGlass.h"
#include "Extensions/Effects/DreamStylizeEffect.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * The effects that work on what is behind them, on the CPU: the shader's numbers held to the enums and the layout, every
 * adjustment operation on colours whose answer is known, the packing, the stylize layers' arithmetic, an effect on a
 * widget's own children drawn as a group, and glass hit by its shape. The pictures are in RHI/DreamBackdropEffectPixelAutomationTests.cpp.
 */
namespace DreamBackdropEffectTestLocal
{
	bool Near(const FVector3f& A, const FVector3f& B, float Tolerance = 1e-3f)
	{
		return FMath::Abs(A.X - B.X) <= Tolerance && FMath::Abs(A.Y - B.Y) <= Tolerance && FMath::Abs(A.Z - B.Z) <= Tolerance;
	}

	FString Describe(const FVector3f& V)
	{
		return FString::Printf(TEXT("(%.4f, %.4f, %.4f)"), V.X, V.Y, V.Z);
	}

	/** CamelCase to the shader's UPPER_CASE: an underscore where a capital follows a small letter. */
	FString ShaderName(const FString& InName)
	{
		FString Out;
		for (int32 Char = 0; Char < InName.Len(); ++Char)
		{
			if (Char > 0 && FChar::IsUpper(InName[Char]) && FChar::IsLower(InName[Char - 1]))
			{
				Out += TEXT("_");
			}
			Out += FChar::ToUpper(InName[Char]);
		}
		return Out;
	}

	/** The #defines of a shader file with a prefix, by the rest of their names. */
	bool ReadDefines(FAutomationTestBase& InTest, const TCHAR* InFile, const TCHAR* InPrefix, TMap<FString, int64>& OutDefines)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
		FString Source;
		if (!InTest.TestTrue(FString::Printf(TEXT("%s is read"), InFile), Plugin.IsValid()
			&& FFileHelper::LoadFileToString(Source, *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private"), InFile))))
		{
			return false;
		}
		FRegexMatcher Matcher(FRegexPattern(FString::Printf(TEXT("#define\\s+%s([A-Z0-9_]+)\\s+(0x[0-9A-Fa-f]+u?|[0-9]+)"), InPrefix)), Source);
		while (Matcher.FindNext())
		{
			FString Value = Matcher.GetCaptureGroup(2);
			Value.RemoveFromEnd(TEXT("u"));
			OutDefines.Add(Matcher.GetCaptureGroup(1), Value.StartsWith(TEXT("0x")) ? FParse::HexNumber64(*Value.Mid(2)) : FCString::Atoi64(*Value));
		}
		return true;
	}

	/** One operation packed alone, applied to C. */
	FVector3f ApplyOne(const FDreamAdjustment& InAdjustment, const FVector3f& C, const TArray<FColor>* InLut = nullptr)
	{
		FDreamUIAdjustPacked Packed;
		FDreamUIAdjustMath::Pack({ InAdjustment }, 1.0f, EDreamUIBlendMode::Alpha, Packed);
		return FDreamUIAdjustMath::ApplyOp(Packed, 0, C, InLut);
	}

	FDreamAdjustment Make(EDreamAdjustmentType InType)
	{
		FDreamAdjustment Adjustment;
		Adjustment.Type = InType;
		return Adjustment;
	}

	/** A game world with a screen-space canvas, which draws groups apart. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;

		FStage()
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(TEXT("Root"), nullptr, FVector2D(800.0, 600.0));
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas != nullptr)
			{
				Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
				Canvas->SetCullElementsOutsideCanvas(false);
			}
		}
		UDreamWidget* MakeWidget(const TCHAR* InName, UDreamWidget* InParent, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector) const
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			Widget->SetDisplayName(InName);
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
		UDreamWidget* MakeBlock(const TCHAR* InName, UDreamWidget* InParent, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector) const
		{
			UDreamWidget* Widget = MakeWidget(InName, InParent, InSize, InPosition);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			}
			return Widget;
		}
		bool IsUsable() const { return Manager != nullptr && Root != nullptr && Canvas != nullptr; }
		void Frames(int32 InCount = 3) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
		/** B a batch, P a post process, ( and ) a group. */
		FString Shape() const
		{
			FString Out;
			for (const FDreamUIDrawCall& DrawCall : Canvas->GetCurrentDrawCalls())
			{
				switch (DrawCall.Type)
				{
				case EDreamUIDrawCallType::BatchMesh: Out += TEXT("B"); break;
				case EDreamUIDrawCallType::PostProcess: Out += TEXT("P"); break;
				case EDreamUIDrawCallType::GroupBegin: Out += TEXT("("); break;
				case EDreamUIDrawCallType::GroupEnd: Out += TEXT(")"); break;
				default: Out += TEXT("?"); break;
				}
			}
			return Out;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAdjustNumbersTest,
	"DreamGUI.Adjust.TheOperationNumbersAndTheLayoutAreTheShadersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAdjustNumbersTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectTestLocal;
	TMap<FString, int64> Adjust;
	if (!ReadDefines(*this, TEXT("DreamUIAdjust.ush"), TEXT("DREAMUI_ADJUST_"), Adjust))
	{
		return false;
	}
	TestEqual(TEXT("Operations at most"), Adjust.FindRef(TEXT("MAX_OPS")), static_cast<int64>(DreamUIAdjustLayout::MaxOps));
	TestEqual(TEXT("Rows to an operation"), Adjust.FindRef(TEXT("ROWS_PER_OP")), static_cast<int64>(DreamUIAdjustLayout::RowsPerOp));
	TestEqual(TEXT("Texels to a table row"), Adjust.FindRef(TEXT("TABLE_WIDTH")), static_cast<int64>(DreamUIAdjustLayout::TableWidth));
	Adjust.Remove(TEXT("MAX_OPS"));
	Adjust.Remove(TEXT("ROWS_PER_OP"));
	Adjust.Remove(TEXT("TABLE_WIDTH"));
	const UEnum* AdjustEnum = StaticEnum<EDreamAdjustmentType>();
	TestEqual(TEXT("The shader numbers every operation and nothing else"), Adjust.Num(), AdjustEnum->NumEnums() - 1);
	for (int32 Index = 0; Index < AdjustEnum->NumEnums() - 1; ++Index)
	{
		const FString Name = ShaderName(AdjustEnum->GetNameStringByIndex(Index));
		const int64* Value = Adjust.Find(Name);
		TestTrue(FString::Printf(TEXT("DREAMUI_ADJUST_%s is %lld"), *Name, AdjustEnum->GetValueByIndex(Index)), Value != nullptr && *Value == AdjustEnum->GetValueByIndex(Index));
	}

	TMap<FString, int64> Stylize;
	if (!ReadDefines(*this, TEXT("DreamUIStylize.ush"), TEXT("DREAMUI_STYLIZE_"), Stylize))
	{
		return false;
	}
	TestEqual(TEXT("Layers at most"), Stylize.FindRef(TEXT("MAX_LAYERS")), static_cast<int64>(DreamUIStylizeLayout::MaxLayers));
	TestEqual(TEXT("Rows to a layer"), Stylize.FindRef(TEXT("ROWS_PER_LAYER")), static_cast<int64>(DreamUIStylizeLayout::RowsPerLayer));
	TestTrue(TEXT("The glitch's seed is the CPU's"), static_cast<uint32>(Stylize.FindRef(TEXT("GLITCH_SEED"))) == FDreamUIStylizeMath::GlitchSeed);
	TestTrue(TEXT("The grain's seed is the CPU's"), static_cast<uint32>(Stylize.FindRef(TEXT("GRAIN_SEED"))) == FDreamUIStylizeMath::GrainSeed);
	for (const TCHAR* Key : { TEXT("MAX_LAYERS"), TEXT("ROWS_PER_LAYER"), TEXT("GLITCH_SEED"), TEXT("GRAIN_SEED") })
	{
		Stylize.Remove(Key);
	}
	const UEnum* StylizeEnum = StaticEnum<EDreamStylizeType>();
	TestEqual(TEXT("The shader numbers every layer and nothing else"), Stylize.Num(), StylizeEnum->NumEnums() - 1);
	for (int32 Index = 0; Index < StylizeEnum->NumEnums() - 1; ++Index)
	{
		const FString Name = ShaderName(StylizeEnum->GetNameStringByIndex(Index));
		const int64* Value = Stylize.Find(Name);
		TestTrue(FString::Printf(TEXT("DREAMUI_STYLIZE_%s is %lld"), *Name, StylizeEnum->GetValueByIndex(Index)), Value != nullptr && *Value == StylizeEnum->GetValueByIndex(Index));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAdjustOperationsTest,
	"DreamGUI.Adjust.EachOperationDoesWhatItSaysToColoursWhoseAnswerIsKnown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAdjustOperationsTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectTestLocal;
	const FVector3f Colours[] = { FVector3f(0.2f, 0.4f, 0.6f), FVector3f(0.9f, 0.1f, 0.3f), FVector3f(0.5f), FVector3f(0.05f, 0.8f, 0.45f), FVector3f(1.0f, 1.0f, 0.0f) };
	// Every operation at its defaults that leave a colour as it is, leaves it.
	for (const EDreamAdjustmentType Type : { EDreamAdjustmentType::BrightnessContrast, EDreamAdjustmentType::Levels, EDreamAdjustmentType::Curves, EDreamAdjustmentType::Exposure
		, EDreamAdjustmentType::HueSaturation, EDreamAdjustmentType::Vibrance, EDreamAdjustmentType::ColorBalance, EDreamAdjustmentType::ChannelMixer })
	{
		for (const FVector3f& C : Colours)
		{
			const FVector3f Out = ApplyOne(Make(Type), C);
			TestTrue(FString::Printf(TEXT("%s at its defaults leaves %s: %s"), *StaticEnum<EDreamAdjustmentType>()->GetNameStringByValue(static_cast<int64>(Type)), *Describe(C), *Describe(Out))
				, Near(Out, C, 2e-3f));
		}
	}
	TestTrue(TEXT("Invert"), Near(ApplyOne(Make(EDreamAdjustmentType::Invert), FVector3f(0.2f, 0.4f, 0.6f)), FVector3f(0.8f, 0.6f, 0.4f)));
	{
		FDreamAdjustment Brightness = Make(EDreamAdjustmentType::BrightnessContrast);
		Brightness.Brightness = 0.1f;
		TestTrue(TEXT("Brightness shifts every channel"), Near(ApplyOne(Brightness, FVector3f(0.2f, 0.4f, 0.6f)), FVector3f(0.3f, 0.5f, 0.7f)));
		FDreamAdjustment Contrast = Make(EDreamAdjustmentType::BrightnessContrast);
		Contrast.Contrast = 0.5f;
		const float K = FMath::Tan(1.5f * 0.78539816f);
		TestTrue(TEXT("Contrast turns the line about middle grey"), Near(ApplyOne(Contrast, FVector3f(0.4f, 0.5f, 0.6f)), FVector3f(0.5f - 0.1f * K, 0.5f, 0.5f + 0.1f * K)));
		Contrast.Contrast = -1.0f;
		TestTrue(TEXT("...and -1 is flat grey"), Near(ApplyOne(Contrast, FVector3f(0.1f, 0.5f, 0.9f)), FVector3f(0.5f)));
	}
	{
		FDreamAdjustment Levels = Make(EDreamAdjustmentType::Levels);
		Levels.InputBlack = 0.2f;
		Levels.InputWhite = 0.8f;
		Levels.OutputBlack = 0.1f;
		Levels.OutputWhite = 0.9f;
		TestTrue(TEXT("Levels stretch the input range onto the output range"), Near(ApplyOne(Levels, FVector3f(0.2f, 0.5f, 0.8f)), FVector3f(0.1f, 0.5f, 0.9f)));
	}
	{
		FDreamAdjustment Exposure = Make(EDreamAdjustmentType::Exposure);
		Exposure.Exposure = 1.0f;
		const FVector3f C(0.3f, 0.5f, 0.1f);
		const FVector3f Want = FDreamUIBlendMath::ToSrgb(FDreamUIBlendMath::ToLinear(C) * 2.0f);
		TestTrue(TEXT("A stop of exposure doubles the light"), Near(ApplyOne(Exposure, C), Want));
	}
	{
		FDreamAdjustment Hue = Make(EDreamAdjustmentType::HueSaturation);
		Hue.Hue = 120.0f;
		TestTrue(TEXT("A third of a turn takes red to green"), Near(ApplyOne(Hue, FVector3f(1.0f, 0.0f, 0.0f)), FVector3f(0.0f, 1.0f, 0.0f)));
		Hue.Hue = 0.0f;
		Hue.Saturation = -1.0f;
		TestTrue(TEXT("Saturation -1 is grey at the colour's lightness"), Near(ApplyOne(Hue, FVector3f(1.0f, 0.0f, 0.0f)), FVector3f(0.5f)));
		FDreamAdjustment Colorize = Make(EDreamAdjustmentType::HueSaturation);
		Colorize.bColorize = true;
		Colorize.Hue = 240.0f;
		Colorize.Saturation = 1.0f;
		TestTrue(TEXT("Colorize gives every colour the hue, at its own lightness"), Near(ApplyOne(Colorize, FVector3f(0.5f)), FVector3f(0.0f, 0.0f, 1.0f)));
		Colorize.Lightness = 1.0f;
		TestTrue(TEXT("Lightness 1 is white"), Near(ApplyOne(Colorize, FVector3f(0.3f, 0.6f, 0.1f)), FVector3f(1.0f)));
	}
	{
		FDreamAdjustment Vibrance = Make(EDreamAdjustmentType::Vibrance);
		Vibrance.Vibrance = 1.0f;
		const FVector3f Grey(0.4f);
		TestTrue(TEXT("Vibrance leaves grey grey"), Near(ApplyOne(Vibrance, Grey), Grey));
		const FVector3f Dull(0.45f, 0.5f, 0.55f);
		const FVector3f Vivid(0.0f, 1.0f, 0.0f);
		const FVector3f DullOut = ApplyOne(Vibrance, Dull);
		TestTrue(TEXT("...raises a dull colour's saturation"), (DullOut.Z - DullOut.X) > (Dull.Z - Dull.X) * 1.5f);
		TestTrue(TEXT("...and leaves a saturated one"), Near(ApplyOne(Vibrance, Vivid), Vivid));
	}
	{
		FDreamAdjustment Balance = Make(EDreamAdjustmentType::ColorBalance);
		Balance.Shadows = FVector3f(1.0f, 0.0f, 0.0f);
		Balance.bPreserveLuminosity = false;
		const FVector3f Dark = ApplyOne(Balance, FVector3f(0.0f, 0.0f, 0.0f));
		TestTrue(TEXT("A shadow shift reaches black"), Near(Dark, FVector3f(0.5f, 0.0f, 0.0f)));
		const FVector3f Bright = ApplyOne(Balance, FVector3f(1.0f, 1.0f, 1.0f));
		TestTrue(TEXT("...and not white"), Near(Bright, FVector3f(1.0f)));
		Balance.bPreserveLuminosity = true;
		const FVector3f Kept = ApplyOne(Balance, FVector3f(0.2f));
		TestTrue(TEXT("Preserving luminosity keeps it"), FMath::IsNearlyEqual(FDreamUIBlendMath::Lum(Kept), 0.2f, 1e-3f));
	}
	{
		const FDreamAdjustment Gray = Make(EDreamAdjustmentType::BlackAndWhite);
		TestTrue(TEXT("Black and white: red is its weight"), Near(ApplyOne(Gray, FVector3f(1.0f, 0.0f, 0.0f)), FVector3f(0.4f)));
		TestTrue(TEXT("...yellow its pair's"), Near(ApplyOne(Gray, FVector3f(1.0f, 1.0f, 0.0f)), FVector3f(0.6f)));
		TestTrue(TEXT("...magenta its"), Near(ApplyOne(Gray, FVector3f(1.0f, 0.0f, 1.0f)), FVector3f(0.8f)));
		TestTrue(TEXT("...and grey stays"), Near(ApplyOne(Gray, FVector3f(0.3f)), FVector3f(0.3f)));
	}
	{
		FDreamAdjustment Filter = Make(EDreamAdjustmentType::PhotoFilter);
		Filter.FilterColor = FColor(255, 0, 0, 255);
		Filter.Density = 1.0f;
		Filter.bPreserveLuminosity = false;
		TestTrue(TEXT("A red filter at full density keeps only red"), Near(ApplyOne(Filter, FVector3f(0.5f, 0.6f, 0.7f)), FVector3f(0.5f, 0.0f, 0.0f)));
	}
	{
		FDreamAdjustment Mixer = Make(EDreamAdjustmentType::ChannelMixer);
		Mixer.RedMix = FVector3f(0.0f, 0.0f, 1.0f);
		Mixer.BlueMix = FVector3f(1.0f, 0.0f, 0.0f);
		TestTrue(TEXT("The channel mixer swaps red and blue"), Near(ApplyOne(Mixer, FVector3f(0.1f, 0.5f, 0.9f)), FVector3f(0.9f, 0.5f, 0.1f)));
		Mixer.bMonochrome = true;
		TestTrue(TEXT("...and monochrome, every channel is red's"), Near(ApplyOne(Mixer, FVector3f(0.1f, 0.5f, 0.9f)), FVector3f(0.9f)));
	}
	{
		FDreamAdjustment Posterize = Make(EDreamAdjustmentType::Posterize);
		Posterize.PosterizeLevels = 3;
		TestTrue(TEXT("Posterize to three levels"), Near(ApplyOne(Posterize, FVector3f(0.1f, 0.5f, 0.9f)), FVector3f(0.0f, 0.5f, 1.0f)));
		FDreamAdjustment Threshold = Make(EDreamAdjustmentType::Threshold);
		Threshold.ThresholdLevel = 0.5f;
		TestTrue(TEXT("Threshold: white at the level"), Near(ApplyOne(Threshold, FVector3f(0.6f)), FVector3f(1.0f)));
		TestTrue(TEXT("...black below"), Near(ApplyOne(Threshold, FVector3f(0.4f)), FVector3f(0.0f)));
	}
	{
		FDreamAdjustment Map = Make(EDreamAdjustmentType::GradientMap);
		Map.Gradient.Stops = { FDreamGradientStop(0.0f, FColor::Black), FDreamGradientStop(1.0f, FColor::White) };
		// Black to white along the stops: the colour's luminosity, to the table's half-float and its sample spacing.
		for (const FVector3f& C : Colours)
		{
			const float L = FDreamUIBlendMath::Lum(C);
			TestTrue(FString::Printf(TEXT("A black-to-white map is the luminosity of %s"), *Describe(C)), Near(ApplyOne(Map, C), FVector3f(L), 4e-3f));
		}
		Map.bReverse = true;
		TestTrue(TEXT("...and reversed, its complement"), Near(ApplyOne(Map, FVector3f(0.25f)), FVector3f(0.75f), 4e-3f));
	}
	{
		UCurveLinearColor* Curve = NewObject<UCurveLinearColor>(GetTransientPackage());
		// A new colour curve starts with keys of its own in the editor: red's are replaced, green's and blue's left out.
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			Curve->FloatCurves[Channel].Reset();
		}
		// Straight between its keys: a curve's own keys are eased flat at its ends otherwise.
		Curve->FloatCurves[0].SetKeyInterpMode(Curve->FloatCurves[0].AddKey(0.0f, 1.0f), RCIM_Linear);
		Curve->FloatCurves[0].SetKeyInterpMode(Curve->FloatCurves[0].AddKey(1.0f, 0.0f), RCIM_Linear);
		FDreamAdjustment Curves = Make(EDreamAdjustmentType::Curves);
		Curves.Curve = Curve;
		TestTrue(TEXT("A falling red curve inverts red alone"), Near(ApplyOne(Curves, FVector3f(0.2f, 0.4f, 0.6f)), FVector3f(0.8f, 0.4f, 0.6f), 4e-3f));
	}
	{
		// The identity table: each slice's red along X and green along Y, blue by slice.
		TArray<FColor> Lut;
		Lut.SetNumUninitialized(256 * 16);
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 X = 0; X < 256; ++X)
			{
				Lut[Y * 256 + X] = FColor(static_cast<uint8>(FMath::RoundToInt((X % 16) * 255.0f / 15.0f)), static_cast<uint8>(FMath::RoundToInt(Y * 255.0f / 15.0f))
					, static_cast<uint8>(FMath::RoundToInt((X / 16) * 255.0f / 15.0f)), 255);
			}
		}
		FDreamAdjustment Table = Make(EDreamAdjustmentType::LookupTable);
		for (const FVector3f& C : Colours)
		{
			TestTrue(FString::Printf(TEXT("The identity table leaves %s"), *Describe(C)), Near(ApplyOne(Table, C, &Lut), C, 3e-3f));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAdjustPackingTest,
	"DreamGUI.Adjust.ThePackingLeavesOutTheDisabledKeepsTheFirstEightAndMixesByModeAndOpacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAdjustPackingTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectTestLocal;
	TArray<FDreamAdjustment> Adjustments;
	FDreamAdjustment Disabled = Make(EDreamAdjustmentType::Threshold);
	Disabled.bEnabled = false;
	Adjustments.Add(Disabled);
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Adjustments.Add(Make(Index % 2 == 0 ? EDreamAdjustmentType::Invert : EDreamAdjustmentType::Posterize));
	}
	FDreamUIAdjustPacked Packed;
	FDreamUIAdjustMath::Pack(Adjustments, 0.5f, EDreamUIBlendMode::Difference, Packed);
	TestEqual(TEXT("The first eight enabled ones"), Packed.NumOps, DreamUIAdjustLayout::MaxOps);
	TestEqual(TEXT("...the disabled one left out"), FMath::RoundToInt32(Packed.Rows[0].X), static_cast<int32>(EDreamAdjustmentType::Invert));
	TestEqual(TEXT("...in order"), FMath::RoundToInt32(Packed.Rows[DreamUIAdjustLayout::RowsPerOp].X), static_cast<int32>(EDreamAdjustmentType::Posterize));
	TestEqual(TEXT("The table has a row for every operation"), Packed.Table.Num(), DreamUIAdjustLayout::TableWidth * DreamUIAdjustLayout::MaxOps);

	// Mixed: an inversion at half opacity, Normal, is grey over anything; in Difference it is |In - (1 - In)| halfway.
	FDreamUIAdjustPacked Invert;
	FDreamUIAdjustMath::Pack({ Make(EDreamAdjustmentType::Invert) }, 0.5f, EDreamUIBlendMode::Alpha, Invert);
	const FVector4f Half = FDreamUIAdjustMath::ApplyToPixel(Invert, FVector4f(0.2f, 0.6f, 1.0f, 1.0f), false, true, FIntPoint::ZeroValue);
	TestTrue(TEXT("Half an inversion is grey"), Near(FVector3f(Half.X, Half.Y, Half.Z), FVector3f(0.5f)));
	FDreamUIAdjustMath::Pack({ Make(EDreamAdjustmentType::Invert) }, 1.0f, EDreamUIBlendMode::Difference, Invert);
	const FVector4f Difference = FDreamUIAdjustMath::ApplyToPixel(Invert, FVector4f(0.2f, 0.6f, 1.0f, 1.0f), false, true, FIntPoint::ZeroValue);
	TestTrue(TEXT("An inversion in Difference is |2c - 1|"), Near(FVector3f(Difference.X, Difference.Y, Difference.Z), FVector3f(0.6f, 0.2f, 1.0f)));
	// What is transparent stays as transparent as it was: an adjustment adds nothing where there is nothing.
	FDreamUIAdjustMath::Pack({ Make(EDreamAdjustmentType::Invert) }, 1.0f, EDreamUIBlendMode::Alpha, Invert);
	const FVector4f Clear = FDreamUIAdjustMath::ApplyToPixel(Invert, FVector4f(0.0f, 0.0f, 0.0f, 0.0f), false, false, FIntPoint::ZeroValue);
	TestTrue(TEXT("A transparent pixel stays transparent"), Clear.W == 0.0f && Clear.X == 0.0f);
	const FVector4f HalfAlpha = FDreamUIAdjustMath::ApplyToPixel(Invert, FVector4f(0.1f, 0.2f, 0.3f, 0.5f), false, false, FIntPoint::ZeroValue);
	TestTrue(TEXT("...and a half-transparent one keeps its alpha, its colour inverted"), FMath::IsNearlyEqual(HalfAlpha.W, 0.5f) && Near(FVector3f(HalfAlpha.X, HalfAlpha.Y, HalfAlpha.Z), FVector3f(0.4f, 0.3f, 0.2f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStylizeArithmeticTest,
	"DreamGUI.Stylize.TheBayerMatrixAndTheLayersArithmetic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStylizeArithmeticTest::RunTest(const FString& Parameters)
{
	// The 4 by 4 Bayer matrix, row by row.
	const int32 Bayer4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
	bool bBayer = true;
	for (int32 Y = 0; Y < 4; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			bBayer &= FMath::IsNearlyEqual(FDreamUIStylizeMath::Bayer(X, Y, 4), (Bayer4[Y * 4 + X] + 0.5f) / 16.0f);
		}
	}
	TestTrue(TEXT("The 4 by 4 matrix is Bayer's"), bBayer);
	TSet<int32> Seen;
	for (int32 Y = 0; Y < 8; ++Y)
	{
		for (int32 X = 0; X < 8; ++X)
		{
			Seen.Add(FMath::RoundToInt32(FDreamUIStylizeMath::Bayer(X, Y, 8) * 64.0f - 0.5f));
		}
	}
	TestEqual(TEXT("The 8 by 8 one holds every threshold once"), Seen.Num(), 64);
	TestEqual(TEXT("A dither of two levels at threshold 0.5 rounds"), FDreamUIStylizeMath::Dither(0.6f, 2.0f, 0.5f), 1.0f);
	TestEqual(TEXT("...and below it"), FDreamUIStylizeMath::Dither(0.4f, 2.0f, 0.5f), 0.0f);
	TestTrue(TEXT("A scanline's middle keeps its light"), FMath::IsNearlyEqual(FDreamUIStylizeMath::Scanline(0.0f, 1.0f, 3.0f, 0.5f), 1.0f));
	TestTrue(TEXT("...and between lines it is darkened by the strength"), FMath::IsNearlyEqual(FDreamUIStylizeMath::Scanline(1.5f, 1.0f, 3.0f, 0.5f), 0.5f));
	TestEqual(TEXT("The vignette leaves the middle"), FDreamUIStylizeMath::Vignette(FVector2f::ZeroVector, 1.0f, 1.0f, 0.5f, 0.5f, 1.0f), 0.0f);
	TestTrue(TEXT("...and darkens the corner fully"), FMath::IsNearlyEqual(FDreamUIStylizeMath::Vignette(FVector2f(1.0f, 1.0f), 1.0f, 1.0f, 0.5f, 0.5f, 1.0f), 1.0f));
	TestTrue(TEXT("A full halftone dot inks its cell's middle"), FMath::IsNearlyEqual(FDreamUIStylizeMath::HalftoneInk(FVector2f(0.5f, 0.5f), 1.0f, false, 0.05f), 1.0f));
	TestEqual(TEXT("...and an empty one inks nothing"), FDreamUIStylizeMath::HalftoneInk(FVector2f(0.5f, 0.5f), 0.0f, false, 0.05f), 0.0f);

	TArray<FDreamStylizeLayer> Layers;
	FDreamStylizeLayer Aberration;
	Aberration.Type = EDreamStylizeType::ChromaticAberration;
	Aberration.AberrationAmount = 5.0f;
	Layers.Add(Aberration);
	FDreamStylizeLayer Off;
	Off.bEnabled = false;
	Layers.Add(Off);
	FDreamUIStylizePacked Packed;
	FDreamUIStylizeMath::Pack(Layers, 1.0f, EDreamUIBlendMode::Alpha, -1.0f, 0.0f, 0.0f, Packed);
	TestEqual(TEXT("The disabled layer is left out"), Packed.NumLayers, 1);
	TestEqual(TEXT("...and the effect reaches as far as its aberration"), Packed.Reach, 5.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAdjustTargetsTest,
	"DreamGUI.Adjust.AnEffectOnItsChildrenMakesItsWidgetAGroupAndOneOnWhatIsBehindDrawsItself",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAdjustTargetsTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Container = Stage.MakeWidget(TEXT("Container"), Stage.Root, FVector2D(200.0, 200.0));
	Stage.MakeBlock(TEXT("Child"), Container, FVector2D(60.0, 60.0));
	UDreamAdjustmentLayer* Layer = Container->CreateNewVisual<UDreamAdjustmentLayer>();
	if (!TestNotNull(TEXT("An adjustment layer"), Layer))
	{
		return false;
	}
	Layer->SetAdjustments({ Make(EDreamAdjustmentType::Invert) });
	Stage.Frames();
	TestEqual(TEXT("On what is behind, it is a post process before its child"), Stage.Shape(), FString(TEXT("PB")));
	Layer->SetTarget(EDreamEffectTarget::Children);
	Stage.Frames();
	TestEqual(TEXT("On its children, its widget is a group and it draws nothing itself"), Stage.Shape(), FString(TEXT("(B)")));
	const TArray<FDreamUIDrawCall>& DrawCalls = Stage.Canvas->GetCurrentDrawCalls();
	const FDreamUIDrawCall* Begin = DrawCalls.FindByPredicate([](const FDreamUIDrawCall& InDrawCall) { return InDrawCall.Type == EDreamUIDrawCallType::GroupBegin; });
	TestTrue(TEXT("The group's opening marker is the layer's"), Begin != nullptr && Begin->EffectVisual.Get() == Layer);
	TestTrue(TEXT("...and the layer gives the group its effect"), Layer->GetChildrenEffect().IsValid());
	Layer->SetAdjustments({});
	Stage.Frames();
	TestEqual(TEXT("With no operations there is no effect, and no group"), Stage.Shape(), FString(TEXT("B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGlassHitTest,
	"DreamGUI.Glass.APointOutsideItsRoundedCornerIsNotOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamGlassHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamBackdropEffectTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = Stage.MakeWidget(TEXT("Glass"), Stage.Root, FVector2D(200.0, 100.0));
	UDreamGlass* Glass = Widget->CreateNewVisual<UDreamGlass>();
	if (!TestNotNull(TEXT("Glass"), Glass))
	{
		return false;
	}
	Glass->SetCornerRadius(FVector4f(40.0f, 40.0f, 40.0f, 40.0f));
	if (!TestEqual(TEXT("The widget is 200 by 100"), FVector2D(Widget->GetWidth(), Widget->GetHeight()), FVector2D(200.0, 100.0)))
	{
		return false;
	}
	TestTrue(TEXT("Its middle is on it"), Glass->IsPointOnGlass(FVector2D::ZeroVector));
	TestTrue(TEXT("...and the middle of an edge"), Glass->IsPointOnGlass(FVector2D(0.0, 49.0)));
	TestFalse(TEXT("A rect's corner is outside the rounded one"), Glass->IsPointOnGlass(FVector2D(98.0, 48.0)));
	TestTrue(TEXT("...and inside the corner's circle it is on it"), Glass->IsPointOnGlass(FVector2D(100.0 - 40.0 + 20.0, 50.0 - 40.0 + 20.0)));
	Glass->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Percentage);
	Glass->SetCornerRadius(FVector4f(1.0f, 1.0f, 1.0f, 1.0f));
	TestTrue(TEXT("A percentage of half the smaller side: 1 is a capsule"), Glass->MakeParams().CornerRadii.Equals(FVector4f(50.0f, 50.0f, 50.0f, 50.0f)));
	return true;
}

#endif
