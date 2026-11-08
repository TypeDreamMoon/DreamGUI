// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"

#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStylePreset.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUILayerStyleMath.h"
#include "Core/DreamUIManager.h"
#include "DreamUIRender/DreamUICompositor.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * Layer styles on the CPU: the record's layout held to the shader's, the packing, each effect's formula at known points, a
 * rect block's row and quad with a style and without one, how a style decides where it is drawn, the markers a canvas puts
 * round a widget whose style is drawn apart, and a preset followed. The pictures are in RHI/DreamLayerStylePixelAutomationTests.cpp.
 */
namespace DreamLayerStyleTestLocal
{
	bool Near(float A, float B, float Tolerance = 1e-3f)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	bool Near(const FVector4f& A, const FVector4f& B, float Tolerance = 1e-3f)
	{
		return Near(A.X, B.X, Tolerance) && Near(A.Y, B.Y, Tolerance) && Near(A.Z, B.Z, Tolerance) && Near(A.W, B.W, Tolerance);
	}

	FString Describe(const FVector4f& V)
	{
		return FString::Printf(TEXT("(%.4f, %.4f, %.4f, %.4f)"), V.X, V.Y, V.Z, V.W);
	}

	/** A square of half size 40, corners 8: what most of the formulas below are measured round. */
	const FVector2f Half(40.0f, 40.0f);
	const FVector4f Corners(8.0f, 8.0f, 8.0f, 8.0f);

	float Square(FVector2f P)
	{
		return FDreamUIClipMaskMath::SdRoundRect(P, Half, Corners);
	}

	/** An opaque white square's pixel at P, its coverage from the distance as a rect block's own edge is. */
	FDreamUILayerStyleMath::FShaded ShadeSquare(const FDreamLayerEffects& InStyle, FVector2f P, FVector3f InColor = FVector3f(1.0f, 1.0f, 1.0f))
	{
		FDreamUILayerStyleRecord Record;
		FDreamUILayerStyleMath::Pack(InStyle, Record);
		FDreamUILayerStyleMath::FPixel Pixel;
		Pixel.P = P;
		Pixel.HalfSize = Half;
		Pixel.AA = 1.0f;
		Pixel.Color = InColor;
		Pixel.Opacity = 1.0f;
		Pixel.Coverage = FDreamUILayerStyleMath::Hard(Square(P), 1.0f);
		return FDreamUILayerStyleMath::Shade(Record.Texels, Pixel, [](FVector2f Q) { return Square(Q); });
	}

	UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
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

	/** A game world with a UI manager and a screen-space canvas root, which draws groups apart. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;

		FStage()
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(World.World, nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas != nullptr)
			{
				Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
				Canvas->SetCullElementsOutsideCanvas(false);
			}
		}
		bool IsUsable() const { return Manager != nullptr && Root != nullptr && Canvas != nullptr && Root->GetRenderCanvas() == Canvas; }
		void Frames(int32 InCount = 3) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
		const TArray<FDreamUIDrawCall>& DrawCalls() const { return Canvas->GetCurrentDrawCalls(); }
		FString Shape() const
		{
			FString Out;
			for (const FDreamUIDrawCall& DrawCall : DrawCalls())
			{
				switch (DrawCall.Type)
				{
				case EDreamUIDrawCallType::BatchMesh: Out += TEXT("B"); break;
				case EDreamUIDrawCallType::GroupBegin: Out += TEXT("("); break;
				case EDreamUIDrawCallType::GroupEnd: Out += TEXT(")"); break;
				default: Out += TEXT("?"); break;
				}
			}
			return Out;
		}
	};

	FDreamLayerEffects DropShadowStyle(float InDistance = 6.0f, float InSize = 8.0f, float InSpread = 0.0f)
	{
		FDreamLayerEffects Style;
		FDreamStyleDropShadow& Shadow = Style.DropShadows.AddDefaulted_GetRef();
		Shadow.BlendMode = EDreamUIBlendMode::Alpha;
		Shadow.Opacity = 1.0f;
		Shadow.Distance = InDistance;
		Shadow.Size = InSize;
		Shadow.Spread = InSpread;
		Shadow.bUseGlobalLight = false;
		Shadow.Angle = 90.0f;
		return Style;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleLayoutTest,
	"DreamGUI.LayerStyle.TheRecordsLayoutIsTheShadersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleLayoutTest::RunTest(const FString& Parameters)
{
	// Every #define DREAMUI_STYLE_* of DreamUILayerStyle.ush against its C++ twin, both ways.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("DreamGUI is a plugin this editor knows"), Plugin.IsValid()))
	{
		return false;
	}
	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private/DreamUILayerStyle.ush"));
	FString Source;
	if (!TestTrue(*FString::Printf(TEXT("%s reads"), *Path), FFileHelper::LoadFileToString(Source, *Path)))
	{
		return false;
	}
	TMap<FString, int64> Defines;
	FRegexMatcher Matcher(FRegexPattern(TEXT("#define DREAMUI_STYLE_([A-Z0-9_]+) (0x[0-9A-Fa-f]+u?|\\d+)\\s")), Source);
	while (Matcher.FindNext())
	{
		FString Value = Matcher.GetCaptureGroup(2);
		Value.RemoveFromEnd(TEXT("u"));
		Defines.Add(Matcher.GetCaptureGroup(1), Value.StartsWith(TEXT("0x")) ? FParse::HexNumber64(*Value.Mid(2)) : FCString::Atoi64(*Value));
	}
	using namespace DreamUILayerStyleLayout;
	const TMap<FString, int64> Expected = {
		{ TEXT("HEADER_TEXELS"), HeaderTexels }, { TEXT("ENTRY_TEXELS"), EntryTexels }, { TEXT("MAX_EFFECTS"), MaxEffects },
		{ TEXT("CONTOUR_SAMPLES"), ContourSamples }, { TEXT("CONTOUR_TEXELS"), ContourTexels }, { TEXT("MAX_CONTOURS"), MaxContours },
		{ TEXT("GRADIENT_SAMPLES"), GradientSamples }, { TEXT("GRADIENT_TEXELS"), GradientTexels }, { TEXT("MAX_GRADIENTS"), MaxGradients },
		{ TEXT("MAX_TEXELS"), MaxTexels }, { TEXT("MAX_OUTER_SLOTS"), MaxOuterSlots }, { TEXT("MAX_PATTERNS"), MaxPatterns },
		{ TEXT("KIND_DROP_SHADOW"), KindDropShadow }, { TEXT("KIND_OUTER_GLOW"), KindOuterGlow }, { TEXT("KIND_PATTERN_OVERLAY"), KindPatternOverlay },
		{ TEXT("KIND_GRADIENT_OVERLAY"), KindGradientOverlay }, { TEXT("KIND_COLOR_OVERLAY"), KindColorOverlay }, { TEXT("KIND_SATIN"), KindSatin },
		{ TEXT("KIND_INNER_GLOW"), KindInnerGlow }, { TEXT("KIND_INNER_SHADOW"), KindInnerShadow }, { TEXT("KIND_STROKE"), KindStroke },
		{ TEXT("KIND_BEVEL"), KindBevel },
		{ TEXT("FLAG_ANTI_ALIASED"), EntryFlagAntiAliased }, { TEXT("FLAG_KNOCK_OUT"), EntryFlagKnockOut }, { TEXT("FLAG_INVERT"), EntryFlagInvert },
		{ TEXT("FLAG_PRECISE"), EntryFlagPrecise }, { TEXT("FLAG_FROM_CENTER"), EntryFlagFromCenter }, { TEXT("FLAG_REVERSE"), EntryFlagReverse },
		{ TEXT("FLAG_DITHER"), EntryFlagDither }, { TEXT("FLAG_GRADIENT_FILL"), EntryFlagGradientFill }, { TEXT("FLAG_PATTERN_FILL"), EntryFlagPatternFill },
		{ TEXT("FLAG_DOWN"), EntryFlagDown }, { TEXT("FLAG_GLOSS_ANTI_ALIASED"), EntryFlagGlossAntiAliased }, { TEXT("FLAG_POINT_LIGHT"), EntryFlagPointLight },
		{ TEXT("RECORD_FLAG_PATTERNS"), RecordFlagPatterns },
		{ TEXT("NOISE_SEED"), FDreamUILayerStyleMath::NoiseSeed },
	};
	for (const TPair<FString, int64>& Pair : Expected)
	{
		const int64* Shader = Defines.Find(Pair.Key);
		if (TestNotNull(*FString::Printf(TEXT("DreamUILayerStyle.ush defines DREAMUI_STYLE_%s"), *Pair.Key), Shader))
		{
			TestEqual(*FString::Printf(TEXT("DREAMUI_STYLE_%s is the C++ value"), *Pair.Key), *Shader, Pair.Value);
		}
	}
	for (const TPair<FString, int64>& Pair : Defines)
	{
		TestTrue(*FString::Printf(TEXT("DREAMUI_STYLE_%s has a C++ twin this test knows"), *Pair.Key), Expected.Contains(Pair.Key));
	}
	// The kinds are numbered in the order they are drawn, as the shader reads them.
	TestTrue(TEXT("The kinds go from the bottom up"), KindDropShadow < KindOuterGlow && KindOuterGlow < KindPatternOverlay && KindInnerShadow < KindStroke && KindStroke < KindBevel);

	// A rect block's row: its own texels, then room for a whole record; where the record starts is the shader's.
	const FString BlockPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private/DreamUIRectBlock.ush"));
	FString BlockSource;
	if (TestTrue(TEXT("DreamUIRectBlock.ush reads"), FFileHelper::LoadFileToString(BlockSource, *BlockPath)))
	{
		FRegexMatcher First(FRegexPattern(TEXT("#define DREAMUI_RECT_BLOCK_STYLE_FIRST (\\d+)")), BlockSource);
		if (TestTrue(TEXT("DreamUIRectBlock.ush says where a rect block's style starts"), First.FindNext()))
		{
			TestEqual(TEXT("...where the rect block's own texels end"), FCString::Atoi(*First.GetCaptureGroup(1)), UDreamRectBlock::OwnDataTexels);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStylePackTest,
	"DreamGUI.LayerStyle.AStyleIsPackedInTheOrderItsEffectsAreDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStylePackTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStyleTestLocal;
	using namespace DreamUILayerStyleLayout;
	FDreamUILayerStyleRecord Record;
	TestFalse(TEXT("An empty style packs to nothing"), FDreamUILayerStyleMath::Pack(FDreamLayerEffects(), Record));
	TestTrue(TEXT("...and its record is empty"), Record.IsEmpty());

	FDreamLayerEffects Style;
	Style.bEnabled = true;
	// Declared out of the order they are drawn in: a stroke, a colour overlay, a drop shadow, an outer glow.
	FDreamStyleStroke& Stroke = Style.Strokes.AddDefaulted_GetRef();
	Stroke.Size = 3.0f;
	FDreamStyleColorOverlay& Overlay = Style.ColorOverlays.AddDefaulted_GetRef();
	Overlay.Color = FColor(10, 20, 30);
	FDreamStyleDropShadow& Shadow = Style.DropShadows.AddDefaulted_GetRef();
	Shadow.Distance = 4.0f;
	Shadow.Size = 6.0f;
	Shadow.bUseGlobalLight = true;
	Style.GlobalLightAngle = 90.0f;
	Style.OuterGlow.bEnabled = true;
	Style.OuterGlow.Size = 10.0f;
	Style.OuterGlow.BlendMode = EDreamUIBlendMode::Screen;
	Style.OuterGlow.Contour.Shape = EDreamStyleContourShape::Ring;
	if (!TestTrue(TEXT("It packs"), FDreamUILayerStyleMath::Pack(Style, Record)))
	{
		return false;
	}
	const FVector4f* R = Record.Texels.GetData();
	TestEqual(TEXT("Four effects"), FMath::RoundToInt(R[0].X), 4);
	auto KindAt = [R](int32 Index) { return FMath::RoundToInt(R[HeaderTexels + Index * EntryTexels].X); };
	TestEqual(TEXT("The drop shadow is drawn first"), KindAt(0), (int32)KindDropShadow);
	TestEqual(TEXT("...then the outer glow"), KindAt(1), (int32)KindOuterGlow);
	TestEqual(TEXT("...then the colour overlay"), KindAt(2), (int32)KindColorOverlay);
	TestEqual(TEXT("...and the stroke last"), KindAt(3), (int32)KindStroke);
	// A light from above casts the shadow straight down: +Y, in units, with the global light's angle.
	const FVector4f E2 = R[HeaderTexels + 2];
	TestTrue(*FString::Printf(TEXT("The shadow falls 4 down, away from a light above (offset %.3f, %.3f)"), E2.Z, E2.W), Near(E2.Z, 0.0f) && Near(E2.W, 4.0f));
	TestEqual(TEXT("A drop shadow in Multiply and a glow in Screen are put down apart: two slots"), Record.NumOuterSlots, 2);
	TestTrue(TEXT("...by their modes"), Record.OuterSlotModes[0] == EDreamUIBlendMode::Multiply && Record.OuterSlotModes[1] == EDreamUIBlendMode::Screen);
	TestEqual(TEXT("One contour table, the glow's (the shadow's is linear)"), FMath::RoundToInt(R[1].Y - R[1].X) / ContourTexels, 1);
	TestEqual(TEXT("The record is the header, four effects and one table"), Record.Texels.Num(), HeaderTexels + 4 * EntryTexels + ContourTexels);
	// Reach: the glow's 10, the shadow's 4 + 6, the stroke's 3 outside, and a unit for the edge's antialiasing.
	TestTrue(*FString::Printf(TEXT("It reaches out as far as its furthest effect and a unit more (%.3f)"), Record.OuterReach), Near(Record.OuterReach, 11.0f));
	TestTrue(TEXT("...which the style says too"), Near(Style.GetOuterReach(), 11.0f));

	// Twelve colour overlays: ten fit and the rest are left out.
	FDreamLayerEffects Many;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		Many.ColorOverlays.AddDefaulted();
	}
	FDreamUILayerStyleMath::Pack(Many, Record);
	TestEqual(TEXT("A record holds ten effects"), FMath::RoundToInt(Record.Texels[0].X), MaxEffects);
	TestTrue(TEXT("...and is never longer than its room"), Record.Texels.Num() <= MaxTexels);

	// A fill opacity alone is something to draw: the layer at less than its own opacity, its effects at theirs.
	FDreamLayerEffects Faded;
	Faded.FillOpacity = 0.5f;
	TestTrue(TEXT("A fill opacity below one alone packs"), FDreamUILayerStyleMath::Pack(Faded, Record) && Near(Record.Texels[0].Z, 0.5f));
	Faded.bEnabled = false;
	TestFalse(TEXT("A style switched off packs to nothing"), FDreamUILayerStyleMath::Pack(Faded, Record));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleCurvesTest,
	"DreamGUI.LayerStyle.ContoursAndGradientsAreTheCurvesTheyName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleCurvesTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStyleTestLocal;
	FDreamStyleContour Contour;
	TestTrue(TEXT("The default contour is linear"), Contour.IsLinear() && Near(Contour.Evaluate(0.3f), 0.3f));
	Contour.Shape = EDreamStyleContourShape::Cone;
	TestTrue(TEXT("A cone peaks halfway"), Near(Contour.Evaluate(0.5f), 1.0f) && Near(Contour.Evaluate(0.0f), 0.0f) && Near(Contour.Evaluate(1.0f), 0.0f));
	Contour.Shape = EDreamStyleContourShape::Gaussian;
	TestTrue(TEXT("A Gaussian is an S through the middle"), Near(Contour.Evaluate(0.5f), 0.5f) && Contour.Evaluate(0.1f) < 0.1f && Contour.Evaluate(0.9f) > 0.9f);
	Contour.Shape = EDreamStyleContourShape::Custom;
	Contour.Points = { FDreamStyleContourPoint(0.0f, 0.0f), FDreamStyleContourPoint(0.5f, 1.0f, true), FDreamStyleContourPoint(1.0f, 0.0f) };
	TestTrue(TEXT("A custom curve passes through its points"), Near(Contour.Evaluate(0.0f), 0.0f) && Near(Contour.Evaluate(0.5f), 1.0f) && Near(Contour.Evaluate(1.0f), 0.0f));
	TestTrue(TEXT("...and stays within 0..1 between them"), Contour.Evaluate(0.25f) >= 0.0f && Contour.Evaluate(0.25f) <= 1.0f);

	// A contour packed is the curve at its samples, read back between them by straight lines.
	FDreamLayerEffects Style = DropShadowStyle();
	Style.DropShadows[0].Contour.Shape = EDreamStyleContourShape::HalfRound;
	FDreamUILayerStyleRecord Record;
	FDreamUILayerStyleMath::Pack(Style, Record);
	int32 Off = 0;
	for (int32 Step = 0; Step <= 100; ++Step)
	{
		const float X = Step / 100.0f;
		const float Packed = FDreamUILayerStyleMath::Contour(Record.Texels.GetData(), 0.0f, X, true);
		// A half round rises infinitely steeply at 0: between its first two samples a straight line is up to 0.03 off.
		Off += Near(Packed, Style.DropShadows[0].Contour.Evaluate(X), 0.035f) ? 0 : 1;
	}
	TestEqual(TEXT("A packed contour is its curve, within its samples' spacing"), Off, 0);

	// A gradient's midpoint moves where its two stops are half and half; opacity has stops of its own.
	FDreamStyleGradient Gradient = FDreamStyleGradient::FromColors(FColor::Black, FColor::White);
	TestTrue(TEXT("Black to white is grey halfway"), Near(Gradient.Evaluate(0.5f).X, 0.5f, 0.01f));
	Gradient.ColorStops[0].Midpoint = 0.25f;
	TestTrue(TEXT("...and with the midpoint at a quarter, grey there"), Near(Gradient.Evaluate(0.25f).X, 0.5f, 0.01f));
	Gradient.OpacityStops = { FDreamStyleGradientOpacityStop(0.0f, 1.0f), FDreamStyleGradientOpacityStop(1.0f, 0.0f) };
	TestTrue(TEXT("Opacity fades by its own stops"), Near(Gradient.Evaluate(0.5f).W, 0.5f, 0.01f) && Near(Gradient.Evaluate(1.0f).W, 0.0f));

	// Laid across the layer: a linear gradient at 90 degrees goes from the bottom (0) to the top (1), +Y being down.
	const FVector2f Size(40.0f, 20.0f);
	TestTrue(TEXT("Linear at 90: 0 at the bottom"), Near(FDreamUILayerStyleMath::GradientPosition(0, UE_HALF_PI, 1.0f, FVector2f::ZeroVector, FVector2f(0.0f, 20.0f), Size, false), 0.0f));
	TestTrue(TEXT("...1 at the top"), Near(FDreamUILayerStyleMath::GradientPosition(0, UE_HALF_PI, 1.0f, FVector2f::ZeroVector, FVector2f(0.0f, -20.0f), Size, false), 1.0f));
	TestTrue(TEXT("...and reversed, the other way"), Near(FDreamUILayerStyleMath::GradientPosition(0, UE_HALF_PI, 1.0f, FVector2f::ZeroVector, FVector2f(0.0f, -20.0f), Size, true), 0.0f));
	TestTrue(TEXT("Radial: 0 in the middle, 1 out along the angle"), Near(FDreamUILayerStyleMath::GradientPosition(1, 0.0f, 1.0f, FVector2f::ZeroVector, FVector2f::ZeroVector, Size, false), 0.0f)
		&& Near(FDreamUILayerStyleMath::GradientPosition(1, 0.0f, 1.0f, FVector2f::ZeroVector, FVector2f(40.0f, 0.0f), Size, false), 1.0f));
	TestTrue(TEXT("Reflected: the same either side of the middle"), Near(FDreamUILayerStyleMath::GradientPosition(3, 0.0f, 1.0f, FVector2f::ZeroVector, FVector2f(20.0f, 0.0f), Size, false),
		FDreamUILayerStyleMath::GradientPosition(3, 0.0f, 1.0f, FVector2f::ZeroVector, FVector2f(-20.0f, 0.0f), Size, false)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleFormulaTest,
	"DreamGUI.LayerStyle.EachEffectFallsWhereItsSettingsSay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleFormulaTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStyleTestLocal;
	// No effect: the layer as it is, clipped by its edge.
	{
		FDreamLayerEffects Style;
		Style.FillOpacity = 1.0f;
		const FDreamUILayerStyleMath::FShaded Inside = ShadeSquare(Style, FVector2f(0.0f, 0.0f));
		TestTrue(TEXT("With nothing on it, the layer is itself"), Near(Inside.Flat, FVector4f(1.0f, 1.0f, 1.0f, 1.0f)));
	}
	// A drop shadow: under the layer where it is, solid where the shape shifted by the distance is, gone past size out.
	{
		const FDreamLayerEffects Style = DropShadowStyle(6.0f, 8.0f);
		const FDreamUILayerStyleMath::FShaded Below = ShadeSquare(Style, FVector2f(0.0f, 50.0f));
		const FDreamUILayerStyleMath::FShaded Far = ShadeSquare(Style, FVector2f(0.0f, 60.0f));
		const FDreamUILayerStyleMath::FShaded Above = ShadeSquare(Style, FVector2f(0.0f, -45.0f));
		TestTrue(*FString::Printf(TEXT("Under the shifted shape's edge the shadow is half (%s)"), *Describe(ShadeSquare(Style, FVector2f(0.0f, 46.0f)).Flat)), Near(ShadeSquare(Style, FVector2f(0.0f, 46.0f)).Flat.W, 0.5f, 0.02f));
		TestTrue(*FString::Printf(TEXT("Below the layer, inside its reach, there is shadow (%s)"), *Describe(Below.Flat)), Below.Flat.W > 0.05f && Below.Flat.W < 0.5f);
		TestTrue(TEXT("...and it is black"), Near(Below.Flat.X, 0.0f));
		TestTrue(TEXT("Past its size it is gone"), Near(Far.Flat.W, 0.0f));
		TestTrue(TEXT("On the side towards the light there is none"), Near(Above.Flat.W, 0.0f));
		TestTrue(TEXT("Inside the layer the layer covers it"), Near(ShadeSquare(Style, FVector2f(0.0f, 30.0f)).Flat, FVector4f(1.0f, 1.0f, 1.0f, 1.0f)));
		// A spread of one: a hard edge, size out.
		const FDreamLayerEffects Hard = DropShadowStyle(0.0f, 8.0f, 1.0f);
		TestTrue(TEXT("Spread 1: solid just inside its size"), Near(ShadeSquare(Hard, FVector2f(0.0f, 47.0f)).Flat.W, 1.0f));
		TestTrue(TEXT("...and nothing just past it"), Near(ShadeSquare(Hard, FVector2f(0.0f, 49.0f)).Flat.W, 0.0f));
	}
	// Knocked out: a see-through layer does not show its own shadow, unless it is told not to knock it out.
	{
		FDreamLayerEffects Style = DropShadowStyle(4.0f, 2.0f, 1.0f);
		Style.FillOpacity = 0.0f;
		TestTrue(TEXT("A layer at fill 0 knocks its shadow out inside it"), Near(ShadeSquare(Style, FVector2f(0.0f, 30.0f)).Flat.W, 0.0f));
		Style.DropShadows[0].bLayerKnocksOut = false;
		TestTrue(TEXT("...and shows it through when told not to"), Near(ShadeSquare(Style, FVector2f(0.0f, 30.0f)).Flat.W, 1.0f));
	}
	// An inside stroke over the layer, an outside one round it, a centred one either side.
	{
		FDreamLayerEffects Style;
		FDreamStyleStroke& Stroke = Style.Strokes.AddDefaulted_GetRef();
		Stroke.Size = 4.0f;
		Stroke.Color = FColor::Red;
		Stroke.Position = EDreamStyleStrokePosition::Outside;
		TestTrue(TEXT("Outside: red just outside the edge"), Near(ShadeSquare(Style, FVector2f(42.0f, 0.0f)).Flat, FVector4f(1.0f, 0.0f, 0.0f, 1.0f)));
		TestTrue(TEXT("...the layer inside it"), Near(ShadeSquare(Style, FVector2f(38.0f, 0.0f)).Flat, FVector4f(1.0f, 1.0f, 1.0f, 1.0f)));
		TestTrue(TEXT("...nothing past its size"), Near(ShadeSquare(Style, FVector2f(46.0f, 0.0f)).Flat.W, 0.0f));
		Style.Strokes[0].Position = EDreamStyleStrokePosition::Inside;
		TestTrue(TEXT("Inside: red just inside the edge"), Near(ShadeSquare(Style, FVector2f(38.0f, 0.0f)).Flat, FVector4f(1.0f, 0.0f, 0.0f, 1.0f)));
		TestTrue(TEXT("...the layer past it"), Near(ShadeSquare(Style, FVector2f(34.0f, 0.0f)).Flat, FVector4f(1.0f, 1.0f, 1.0f, 1.0f)));
		TestTrue(TEXT("...nothing outside the edge"), Near(ShadeSquare(Style, FVector2f(42.0f, 0.0f)).Flat.W, 0.0f));
		Style.Strokes[0].Position = EDreamStyleStrokePosition::Center;
		TestTrue(TEXT("Centred: red either side"), Near(ShadeSquare(Style, FVector2f(41.0f, 0.0f)).Flat.X, 1.0f) && Near(ShadeSquare(Style, FVector2f(39.0f, 0.0f)).Flat.Y, 0.0f));
	}
	// Overlays blend with the layer by their mode, clipped to it.
	{
		FDreamLayerEffects Style;
		FDreamStyleColorOverlay& Overlay = Style.ColorOverlays.AddDefaulted_GetRef();
		Overlay.Color = FColor(128, 0, 0);
		Overlay.BlendMode = EDreamUIBlendMode::Multiply;
		const FVector4f Grey = ShadeSquare(Style, FVector2f::ZeroVector, FVector3f(0.5f, 0.5f, 0.5f)).Flat;
		TestTrue(*FString::Printf(TEXT("A Multiply overlay of half red on grey is a quarter red, no green (%s)"), *Describe(Grey)), Near(Grey.X, 0.5f * 128.0f / 255.0f, 0.005f) && Near(Grey.Y, 0.0f));
		TestTrue(TEXT("...and outside the layer it is nothing"), Near(ShadeSquare(Style, FVector2f(60.0f, 0.0f)).Flat.W, 0.0f));
		Style.FillOpacity = 0.0f;
		Style.ColorOverlays[0].BlendMode = EDreamUIBlendMode::Alpha;
		TestTrue(TEXT("At fill 0 an overlay still covers the layer's shape"), Near(ShadeSquare(Style, FVector2f::ZeroVector).Flat, FVector4f(128.0f / 255.0f, 0.0f, 0.0f, 1.0f)));
	}
	// An inner shadow: dark along the edge on the light's side, inside the layer only.
	{
		FDreamLayerEffects Style;
		FDreamStyleInnerShadow& Shadow = Style.InnerShadows.AddDefaulted_GetRef();
		Shadow.BlendMode = EDreamUIBlendMode::Alpha;
		Shadow.Opacity = 1.0f;
		Shadow.bUseGlobalLight = false;
		Shadow.Angle = 90.0f;
		Shadow.Distance = 4.0f;
		Shadow.Size = 4.0f;
		const float Top = ShadeSquare(Style, FVector2f(0.0f, -38.0f)).Flat.X;
		const float Bottom = ShadeSquare(Style, FVector2f(0.0f, 38.0f)).Flat.X;
		TestTrue(*FString::Printf(TEXT("A light from above darkens the top inside edge (%.3f) and not the bottom (%.3f)"), Top, Bottom), Top < 0.2f && Bottom > 0.95f);
	}
	// A glow: brightest at the edge, gone past its size; inner from the edge, or from the centre.
	{
		FDreamLayerEffects Style;
		Style.OuterGlow.bEnabled = true;
		Style.OuterGlow.BlendMode = EDreamUIBlendMode::Alpha;
		Style.OuterGlow.Opacity = 1.0f;
		Style.OuterGlow.Size = 10.0f;
		Style.OuterGlow.Range = 1.0f;
		const float Near0 = ShadeSquare(Style, FVector2f(41.0f, 0.0f)).Flat.W;
		const float Near5 = ShadeSquare(Style, FVector2f(45.0f, 0.0f)).Flat.W;
		TestTrue(*FString::Printf(TEXT("An outer glow fades out from the edge (%.3f, %.3f)"), Near0, Near5), Near0 > Near5 && Near5 > 0.0f);
		TestTrue(TEXT("...and is gone past its size"), Near(ShadeSquare(Style, FVector2f(51.0f, 0.0f)).Flat.W, 0.0f));
		Style.OuterGlow.Technique = EDreamStyleTechnique::Precise;
		TestTrue(TEXT("Precise: solid at the edge"), Near(ShadeSquare(Style, FVector2f(40.5f, 0.0f)).Flat.W, 1.0f, 0.06f));
		Style.OuterGlow.bEnabled = false;
		Style.InnerGlow.bEnabled = true;
		Style.InnerGlow.BlendMode = EDreamUIBlendMode::Alpha;
		Style.InnerGlow.Opacity = 1.0f;
		Style.InnerGlow.Size = 10.0f;
		Style.InnerGlow.Range = 1.0f;
		Style.InnerGlow.Color = FColor::Black;
		const float Edge = ShadeSquare(Style, FVector2f(39.0f, 0.0f)).Flat.X;
		const float Middle = ShadeSquare(Style, FVector2f(0.0f, 0.0f)).Flat.X;
		TestTrue(*FString::Printf(TEXT("A black inner glow from the edge darkens the edge (%.3f), not the middle (%.3f)"), Edge, Middle), Edge < 0.6f && Near(Middle, 1.0f));
		Style.InnerGlow.Source = EDreamStyleGlowSource::Center;
		TestTrue(TEXT("...and from the centre, the middle"), ShadeSquare(Style, FVector2f(0.0f, 0.0f)).Flat.X < 0.05f);
	}
	// A bevel lit from the upper left: the top edge lit, the bottom edge shaded, the flat top as it was.
	{
		FDreamLayerEffects Style;
		Style.BevelEmboss.bEnabled = true;
		Style.BevelEmboss.Size = 8.0f;
		Style.BevelEmboss.bUseGlobalLight = false;
		Style.BevelEmboss.Angle = 90.0f;
		Style.BevelEmboss.Altitude = 30.0f;
		Style.BevelEmboss.HighlightMode = EDreamUIBlendMode::Alpha;
		Style.BevelEmboss.ShadowMode = EDreamUIBlendMode::Alpha;
		Style.BevelEmboss.HighlightColor = FColor::White;
		Style.BevelEmboss.ShadowColor = FColor::Black;
		const FVector3f Grey(0.5f, 0.5f, 0.5f);
		const float Top = ShadeSquare(Style, FVector2f(0.0f, -38.0f), Grey).Flat.X;
		const float Bottom = ShadeSquare(Style, FVector2f(0.0f, 38.0f), Grey).Flat.X;
		const float Middle = ShadeSquare(Style, FVector2f(0.0f, 0.0f), Grey).Flat.X;
		TestTrue(*FString::Printf(TEXT("The edge towards the light is lit (%.3f), the far edge shaded (%.3f), the top flat (%.3f)"), Top, Bottom, Middle),
			Top > 0.6f && Bottom < 0.4f && Near(Middle, 0.5f, 0.01f));
		Style.BevelEmboss.Direction = EDreamStyleBevelDirection::Down;
		TestTrue(TEXT("Pressed down, the other way round"), ShadeSquare(Style, FVector2f(0.0f, -38.0f), Grey).Flat.X < 0.4f);
	}
	// The outer effects of different modes go to different slots; a group puts each down by its own.
	{
		FDreamLayerEffects Style = DropShadowStyle(0.0f, 6.0f);
		Style.DropShadows[0].BlendMode = EDreamUIBlendMode::Multiply;
		FDreamUILayerStyleRecord Record;
		FDreamUILayerStyleMath::Pack(Style, Record);
		FDreamUILayerStyleMath::FPixel Pixel;
		Pixel.P = FVector2f(43.0f, 0.0f);
		Pixel.HalfSize = Half;
		const FDreamUILayerStyleMath::FShaded Shaded = FDreamUILayerStyleMath::Shade(Record.Texels, Pixel, [](FVector2f Q) { return Square(Q); });
		TestTrue(TEXT("The shadow is in its slot"), Shaded.Outer[0].W > 0.1f && Near(Shaded.Outer[0].W, Shaded.Flat.W));
		TestTrue(TEXT("...and not in the layer"), Near(Shaded.Layer.W, 0.0f));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleRectBlockTest,
	"DreamGUI.LayerStyle.ARectBlockDrawsItsStyleInItsOwnPixelsAndUploadsNoMoreWithoutOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleRectBlockTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStyleTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = MakeWidget(Stage.World.World, Stage.Root, TEXT("Block"), FVector2D(80.0, 80.0));
	UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
	if (!TestNotNull(TEXT("A rect block"), Block))
	{
		return false;
	}
	Stage.Frames();
	auto QuadExtent = [Block]()
	{
		const FDreamUIGeometry* Geometry = Block->GetGeometry();
		float Extent = 0.0f;
		if (Geometry != nullptr)
		{
			for (const FDreamUIOriginVertexData& Vertex : Geometry->OriginVertices)
			{
				Extent = FMath::Max(Extent, FMath::Abs(Vertex.Position.Y));
			}
		}
		return Extent;
	};
	const float PlainExtent = QuadExtent();

	// Without a style the block uploads its own twelve texels, as it always did.
	DreamUIRenderStats::TakeSnapshot(true);
	Block->SetBodyColor(FColor(200, 10, 10));
	Stage.Frames(1);
	DreamUIRenderStats::FSnapshot Snapshot = DreamUIRenderStats::TakeSnapshot(true);
	TestEqual(TEXT("A rect block with no style uploads 192 bytes"), Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::UploadedBytes)], (int64)(UDreamRectBlock::OwnDataTexels * 16));

	UDreamLayerStyle* Style = Widget->AddComponent<UDreamLayerStyle>();
	FDreamLayerEffects Effects = DropShadowStyle(6.0f, 8.0f);
	Style->SetStyle(Effects);
	Stage.Frames();
	TestEqual(TEXT("A rect block's style is drawn in its own pixels"), Style->GetPath(), EDreamLayerStylePath::Element);
	TestEqual(TEXT("...so nothing is drawn apart"), Stage.Shape(), FString(TEXT("B")));
	TestTrue(TEXT("Its quad grows by what the style reaches"), Near(QuadExtent(), PlainExtent + Style->GetRecord().OuterReach, 0.01f));
	DreamUIRenderStats::TakeSnapshot(true);
	Block->SetBodyColor(FColor(10, 200, 10));
	Stage.Frames(1);
	Snapshot = DreamUIRenderStats::TakeSnapshot(true);
	TestEqual(TEXT("...and it uploads its own texels and its style's record"), Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::UploadedBytes)],
		(int64)((UDreamRectBlock::OwnDataTexels + Style->GetRecord().Texels.Num()) * 16));

	// A pattern is read where it can be bound: the block is drawn apart.
	Effects.PatternOverlay.bEnabled = true;
	Effects.PatternOverlay.Pattern.Pattern = FDreamUIUtils::GetDefaultWhiteTexture();
	Style->SetStyle(Effects);
	Stage.Frames();
	TestEqual(TEXT("A style with a pattern is drawn apart"), Style->GetPath(), EDreamLayerStylePath::Group);
	TestEqual(TEXT("...between markers that carry it"), Stage.Shape(), FString(TEXT("(B)")));
	if (Stage.DrawCalls().Num() == 3)
	{
		TestTrue(TEXT("The opening marker knows the style"), Stage.DrawCalls()[0].LayerStyle.Get() == Style);
		TestTrue(TEXT("...and its composite has it"), Stage.DrawCalls()[0].Composite.IsValid() && Stage.DrawCalls()[0].Composite->Style.IsValid());
	}
	TestTrue(TEXT("...and its quad does not grow: the group's texture does"), Near(QuadExtent(), PlainExtent, 0.01f));

	// Gone, the block is as it was.
	Widget->RemoveComponent(Style);
	Stage.Frames();
	TestEqual(TEXT("Without the style, one draw again"), Stage.Shape(), FString(TEXT("B")));
	TestTrue(TEXT("...and its own quad"), Near(QuadExtent(), PlainExtent, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleGroupTest,
	"DreamGUI.LayerStyle.AnyOtherWidgetIsDrawnApartAndAContainersStyleTakesItsSubtree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleGroupTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStyleTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	auto MakeBlock = [World](UDreamWidget* InParent, const TCHAR* InName, FVector2D InPosition)
	{
		UDreamWidget* Widget = MakeWidget(World, InParent, InName, FVector2D(40.0, 40.0), InPosition);
		if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
		}
		return Widget;
	};
	MakeBlock(Stage.Root, TEXT("Before"), FVector2D(-200.0, 0.0));
	UDreamWidget* Image = MakeBlock(Stage.Root, TEXT("Image"), FVector2D(-100.0, 0.0));
	UDreamWidget* Child = MakeBlock(Image, TEXT("Child"), FVector2D(10.0, 0.0));
	UDreamWidget* Container = MakeWidget(World, Stage.Root, TEXT("Container"), FVector2D(200.0, 200.0), FVector2D(100.0, 0.0));
	MakeBlock(Container, TEXT("A"), FVector2D(-20.0, 0.0));
	MakeBlock(Container, TEXT("B"), FVector2D(20.0, 0.0));
	Stage.Frames();
	TestEqual(TEXT("Nothing styled: one draw"), Stage.Shape(), FString(TEXT("B")));

	// An image's style is round what the image itself draws: its child is drawn after the group, not in it.
	UDreamLayerStyle* ImageStyle = Image->AddComponent<UDreamLayerStyle>();
	ImageStyle->SetStyle(DropShadowStyle());
	Stage.Frames();
	TestEqual(TEXT("An image's style is drawn apart"), ImageStyle->GetPath(), EDreamLayerStylePath::Group);
	TestFalse(TEXT("...round its own element"), ImageStyle->StylesSubtree());
	TestEqual(TEXT("...the image alone between the markers"), Stage.Shape(), FString(TEXT("B(B)B")));

	// A container's is round its subtree.
	UDreamLayerStyle* ContainerStyle = Container->AddComponent<UDreamLayerStyle>();
	ContainerStyle->SetStyle(DropShadowStyle());
	Stage.Frames();
	TestTrue(TEXT("A container's style takes its subtree"), ContainerStyle->StylesSubtree());
	TestEqual(TEXT("...both its children in one group"), Stage.Shape(), FString(TEXT("B(B)B(B)")));
	const TArray<FDreamUIDrawCall>& DrawCalls = Stage.DrawCalls();
	if (DrawCalls.Num() == 8)
	{
		TestEqual(TEXT("The container's group holds both its children"), DrawCalls[6].BatchMeshGeometryArray.Num(), 2);
		TestTrue(TEXT("Its markers carry its style"), DrawCalls[5].LayerStyle.Get() == ContainerStyle);
	}

	// A style's settings are read every frame: changing them rebuilds nothing.
	DreamUIRenderStats::TakeSnapshot(true);
	FDreamLayerEffects Changed = DropShadowStyle(12.0f, 3.0f);
	ContainerStyle->SetStyle(Changed);
	Stage.Frames();
	const DreamUIRenderStats::FSnapshot Snapshot = DreamUIRenderStats::TakeSnapshot(true);
	TestEqual(TEXT("A style's settings change without a rebuild"), Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::DrawCallRebuilds)], (int64)0);

	// A layer group and a style on one widget: one group, styled.
	UDreamLayerGroup* Group = Container->AddComponent<UDreamLayerGroup>();
	Group->SetGroupOpacity(0.5f);
	Stage.Frames();
	TestEqual(TEXT("A layer group with a style is one group"), Stage.Shape(), FString(TEXT("B(B)B(B)")));
	if (Stage.DrawCalls().Num() == 8)
	{
		TestTrue(TEXT("...that knows both"), Stage.DrawCalls()[5].LayerStyle.Get() == ContainerStyle && Stage.DrawCalls()[5].LayerGroup.Get() == Group);
	}

	// Switched off, nothing is drawn apart for it.
	FDreamLayerEffects Off = Changed;
	Off.bEnabled = false;
	ImageStyle->SetStyle(Off);
	Stage.Frames();
	TestEqual(TEXT("A style switched off draws nothing apart"), ImageStyle->GetPath(), EDreamLayerStylePath::None);
	(void)Child;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStylePresetTest,
	"DreamGUI.LayerStyle.APresetIsDrawnInPlaceOfTheStyleAndFollowedThroughItsEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStylePresetTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStyleTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = MakeWidget(Stage.World.World, Stage.Root, TEXT("Block"), FVector2D(80.0, 80.0));
	Widget->CreateNewVisual<UDreamRectBlock>();
	UDreamLayerStyle* Style = Widget->AddComponent<UDreamLayerStyle>();
	Style->SetStyle(DropShadowStyle(2.0f, 2.0f));
	UDreamLayerStylePreset* Preset = NewObject<UDreamLayerStylePreset>(GetTransientPackage());
	Preset->SetStyle(DropShadowStyle(20.0f, 10.0f));
	Style->SetPreset(Preset);
	Stage.Frames();
	TestTrue(TEXT("The preset's style is drawn, not the widget's"), Near(Style->GetRecord().OuterReach, Preset->GetStyle().GetOuterReach()));
	Preset->SetStyle(DropShadowStyle(30.0f, 10.0f));
	TestTrue(TEXT("An edit of the preset is followed"), Near(Style->GetRecord().OuterReach, 41.0f));
	Style->DetachFromPreset();
	TestNull(TEXT("Detached, there is no preset"), Style->GetPreset());
	TestTrue(TEXT("...and the widget's style is the preset's as it was"), Style->GetStyle() == Preset->GetStyle());
	Preset->SetStyle(DropShadowStyle(1.0f, 1.0f));
	TestTrue(TEXT("...and no longer follows it"), Near(Style->GetRecord().OuterReach, 41.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerStyleRectBlockNotationTest,
	"DreamGUI.LayerStyle.ARectBlocksOwnEffectsAreASecondWayToWriteFourOfIts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerStyleRectBlockNotationTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerStyleTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = MakeWidget(Stage.World.World, Stage.Root, TEXT("Block"), FVector2D(100.0, 60.0));
	UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
	if (!TestNotNull(TEXT("A rect block"), Block))
	{
		return false;
	}
	TestTrue(TEXT("A plain rect block is no style"), !Block->GetEffectsAsLayerStyle().IsActive());

	// Written as a style...
	FDreamLayerEffects Style;
	FDreamStyleDropShadow& Shadow = Style.DropShadows.AddDefaulted_GetRef();
	Shadow.Color = FColor(10, 20, 30);
	Shadow.Opacity = 0.5f;
	Shadow.bUseGlobalLight = false;
	Shadow.Angle = 135.0f;
	Shadow.Distance = 7.0f;
	Shadow.Size = 6.0f;
	Shadow.Spread = 0.75f;
	FDreamStyleStroke& Stroke = Style.Strokes.AddDefaulted_GetRef();
	Stroke.Size = 3.0f;
	Stroke.Color = FColor(200, 100, 50);
	FDreamStyleInnerShadow& Inner = Style.InnerShadows.AddDefaulted_GetRef();
	Inner.bUseGlobalLight = false;
	Inner.Angle = 45.0f;
	Inner.Distance = 2.0f;
	Inner.Size = 4.0f;
	Inner.Choke = 0.25f;
	Block->SetEffectsFromLayerStyle(Style);
	TestTrue(TEXT("...the rect block's own outer shadow, border and inner shadow are on"), Block->GetEnableOuterShadow() && Block->GetEnableBorder() && Block->GetEnableInnerShadow());
	TestEqual(TEXT("...the shadow's colour"), Block->GetOuterShadowColor(), FColor(10, 20, 30, 128));
	TestTrue(TEXT("...its angle and distance"), Near(Block->GetOuterShadowAngle(), 135.0f) && Near(Block->GetOuterShadowDistance(), 7.0f));
	TestTrue(TEXT("...the border's width"), Near(Block->GetBorderWidth(), 3.0f));

	// ...and read back as one, the same.
	const FDreamLayerEffects Back = Block->GetEffectsAsLayerStyle();
	if (TestEqual(TEXT("Read back: one drop shadow"), Back.DropShadows.Num(), 1))
	{
		const FDreamStyleDropShadow& B = Back.DropShadows[0];
		TestTrue(TEXT("...of the same colour and opacity"), B.Color == FColor(10, 20, 30) && Near(B.Opacity, 128.0f / 255.0f));
		TestTrue(TEXT("...angle and distance"), Near(B.Angle, 135.0f) && Near(B.Distance, 7.0f));
		TestTrue(*FString::Printf(TEXT("...size and spread (%.3f, %.3f)"), B.Size, B.Spread), Near(B.Size, 6.0f) && Near(B.Spread, 0.75f));
	}
	if (TestEqual(TEXT("...one stroke"), Back.Strokes.Num(), 1))
	{
		TestTrue(TEXT("...inside, of the same size and colour"), Back.Strokes[0].Position == EDreamStyleStrokePosition::Inside && Near(Back.Strokes[0].Size, 3.0f) && Back.Strokes[0].Color == FColor(200, 100, 50));
	}
	if (TestEqual(TEXT("...one inner shadow"), Back.InnerShadows.Num(), 1))
	{
		TestTrue(TEXT("...of the same size and choke"), Near(Back.InnerShadows[0].Size, 4.0f) && Near(Back.InnerShadows[0].Choke, 0.25f));
	}
	// What the rect block cannot say is not written: no stroke, no border.
	Block->SetEffectsFromLayerStyle(FDreamLayerEffects());
	TestFalse(TEXT("A style without them turns them off"), Block->GetEnableOuterShadow() || Block->GetEnableBorder() || Block->GetEnableInnerShadow());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
