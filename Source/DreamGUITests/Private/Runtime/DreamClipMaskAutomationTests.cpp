// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipData.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * Clip masks and clipping to the layer below, on the CPU.
 *
 * The shader's half of every formula (DreamUIClip.ush) has a CPU twin (FDreamUIClipMaskMath) that a hit test runs, so
 * these hold the twin to what each mask is meant to do, and the layout the two share to the shader's own #defines. Whether
 * the twin and the shader then agree pixel for pixel is the picture tests' question (DreamClipMaskPixelAutomationTests).
 */
namespace DreamClipMaskTestLocal
{
	/** A level of InMask over a clip of InHalfSize, packed as the row holds it. */
	FDreamUIClipLevel MaskLevel(const FDreamClipMask& InMask, FVector2f InHalfSize, FVector4f InCornerRadius = FVector4f::Zero(), int32 InSlot = 0)
	{
		FDreamUIClipLevel Level;
		Level.HalfSize = InHalfSize;
		Level.CornerRadius = FVector4f(InCornerRadius.Y, InCornerRadius.X, InCornerRadius.W, InCornerRadius.Z);
		Level.bMasked = FDreamUIClipMaskMath::PackMask(InMask, InHalfSize, InSlot, Level.Mask);
		return Level;
	}

	/** The level's alpha at P, a hard edge, its texture (if any) white unless InTexel answers. */
	float AlphaAt(const FDreamUIClipLevel& InLevel, FVector2f P, TFunction<FLinearColor(FVector2f)> InTexel = nullptr, float InAAWidth = 0.0f)
	{
		return FDreamUIClipMaskMath::EvaluateLevel(InLevel, P, InAAWidth, [&InTexel](FVector2f UV, FLinearColor& OutTexel)
		{
			if (!InTexel)
			{
				return false;
			}
			OutTexel = InTexel(UV);
			return true;
		});
	}

	FDreamClipMask Shaped(EDreamClipMaskShape InShape)
	{
		FDreamClipMask Mask;
		Mask.Shape = InShape;
		return Mask;
	}

	FDreamClipMask Pattern(EDreamClipMaskPattern InPattern, float InRotation = 0.0f)
	{
		FDreamClipMask Mask;
		Mask.Source = EDreamClipMaskSource::Procedural;
		Mask.Pattern = InPattern;
		Mask.Rotation = InRotation;
		return Mask;
	}

	/** A registered widget of InSize under InParent, its centre at InPosition from the parent's. */
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

	UDreamWidget* MakeBlock(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector)
	{
		UDreamWidget* Widget = MakeWidget(InWorld, InParent, InName, InSize, InPosition);
		if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
		{
			Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
		}
		return Widget;
	}

	/** The world point InOffset from InWidget's centre, in its own plane. */
	FVector PointAt(const UDreamWidget* InWidget, FVector2D InOffset)
	{
		const FVector2D Centre = InWidget->GetLocalSpaceCenter();
		return InWidget->GetWorldTransform().TransformPosition(FVector(0.0, Centre.X + InOffset.X, Centre.Y + InOffset.Y));
	}

	/** A game world with a UI manager, a canvas root, and a frame to run. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;

		FStage()
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(World.World, nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			if (UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>())
			{
				Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
			}
		}
		bool IsUsable() const { return Manager != nullptr && Root != nullptr && Root->GetRenderCanvas() != nullptr; }
		void Frames(int32 InCount = 2) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
	};

	FDreamUIGeometry MakeQuad(const FVector2D& InMin, const FVector2D& InMax, UTexture* InSlot0 = nullptr, UTexture* InSlot1 = nullptr)
	{
		FDreamUIGeometry Geo;
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMin.X, (float)InMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMax.X, (float)InMin.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMin.X, (float)InMax.Y)));
		Geo.Vertices.Add(FDreamUIMeshVertex(FVector3f(0.0f, (float)InMax.X, (float)InMax.Y)));
		Geo.Triangles = { 0, 3, 2, 0, 1, 3 };
		Geo.BoundsMin2DInCanvasSpace = InMin;
		Geo.BoundsMax2DInCanvasSpace = InMax;
		Geo.bSupportDrawcallBatching = true;
		Geo.MaskTextures.Slots[0] = InSlot0;
		Geo.MaskTextures.Slots[1] = InSlot1;
		return Geo;
	}

	FDreamUIRenderData RenderDataOf(const FDreamUIGeometry& InGeo)
	{
		FDreamUIRenderData RenderData(EDreamUIDrawCallType::BatchMesh);
		const TSharedRef<FDreamUIGeometry> Prepared = MakeShared<FDreamUIGeometry>();
		Prepared->CopyDataForPrepare(InGeo);
		RenderData.BatchMeshGeometry = Prepared;
		return RenderData;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskLayoutTest,
	"DreamGUI.ClipMask.TheRowLayoutIsTheShadersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskLayoutTest::RunTest(const FString& Parameters)
{
	// Every #define DREAMUI_CLIP_* of DreamUIClip.ush against the constant or enum value it is paired with in C++. A
	// constant added on one side and not the other fails here, as does one that moved.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestTrue(TEXT("DreamGUI is a plugin this editor knows"), Plugin.IsValid()))
	{
		return false;
	}
	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private/DreamUIClip.ush"));
	FString Source;
	if (!TestTrue(*FString::Printf(TEXT("%s reads"), *Path), FFileHelper::LoadFileToString(Source, *Path)))
	{
		return false;
	}
	TMap<FString, int32> Defines;
	FRegexMatcher Matcher(FRegexPattern(TEXT("#define DREAMUI_CLIP_([A-Z0-9_]+) (\\d+)")), Source);
	while (Matcher.FindNext())
	{
		Defines.Add(Matcher.GetCaptureGroup(1), FCString::Atoi(*Matcher.GetCaptureGroup(2)));
	}
	using namespace DreamUIClipLayout;
	const TMap<FString, int32> Expected = {
		{ TEXT("DEPTH"), Depth }, { TEXT("BASE_TEXELS"), BaseTexels }, { TEXT("MASK_TEXELS"), MaskTexels },
		{ TEXT("KIND_SHIFT"), KindShift }, { TEXT("SHAPE_SHIFT"), ShapeShift }, { TEXT("SOURCE_SHIFT"), SourceShift },
		{ TEXT("CHANNEL_SHIFT"), ChannelShift }, { TEXT("SLOT_SHIFT"), SlotShift }, { TEXT("PATTERN_SHIFT"), PatternShift },
		{ TEXT("INVERT_SHIFT"), InvertShift }, { TEXT("ADDRESS_SHIFT"), AddressShift }, { TEXT("NO_CENTRE_SHIFT"), NoCentreShift },
		{ TEXT("REVERSE_SHIFT"), ReverseShift },
		{ TEXT("KIND_MASK"), KindMask }, { TEXT("KIND_UNDERLAY_IMAGE"), KindUnderlayImage }, { TEXT("KIND_UNDERLAY_SHAPE"), KindUnderlayShape },
		{ TEXT("ADDRESS_BOUNDED"), AddressBounded }, { TEXT("ADDRESS_REPEAT"), AddressRepeat }, { TEXT("ADDRESS_RAW"), AddressRaw },
		{ TEXT("SHAPE_RECT"), (int32)EDreamClipMaskShape::Rect }, { TEXT("SHAPE_ELLIPSE"), (int32)EDreamClipMaskShape::Ellipse },
		{ TEXT("SHAPE_POLYGON"), (int32)EDreamClipMaskShape::Polygon }, { TEXT("SHAPE_STAR"), (int32)EDreamClipMaskShape::Star },
		{ TEXT("SHAPE_RING"), (int32)EDreamClipMaskShape::Ring },
		{ TEXT("SOURCE_NONE"), (int32)EDreamClipMaskSource::None }, { TEXT("SOURCE_TEXTURE"), (int32)EDreamClipMaskSource::Texture },
		{ TEXT("SOURCE_PROCEDURAL"), (int32)EDreamClipMaskSource::Procedural },
		{ TEXT("CHANNEL_ALPHA"), (int32)EDreamClipMaskChannel::Alpha }, { TEXT("CHANNEL_RED"), (int32)EDreamClipMaskChannel::Red },
		{ TEXT("CHANNEL_GREEN"), (int32)EDreamClipMaskChannel::Green }, { TEXT("CHANNEL_BLUE"), (int32)EDreamClipMaskChannel::Blue },
		{ TEXT("CHANNEL_LUMINANCE"), (int32)EDreamClipMaskChannel::Luminance },
		{ TEXT("PATTERN_LINEAR"), (int32)EDreamClipMaskPattern::Linear }, { TEXT("PATTERN_RADIAL"), (int32)EDreamClipMaskPattern::Radial },
		{ TEXT("PATTERN_ANGULAR"), (int32)EDreamClipMaskPattern::Angular }, { TEXT("PATTERN_DIAMOND"), (int32)EDreamClipMaskPattern::Diamond },
		{ TEXT("PATTERN_BLINDS"), (int32)EDreamClipMaskPattern::Blinds }, { TEXT("PATTERN_CHECKER"), (int32)EDreamClipMaskPattern::Checker },
		{ TEXT("PATTERN_NOISE"), (int32)EDreamClipMaskPattern::Noise },
	};
	for (const TPair<FString, int32>& Pair : Expected)
	{
		const int32* Shader = Defines.Find(Pair.Key);
		if (TestNotNull(*FString::Printf(TEXT("DreamUIClip.ush defines DREAMUI_CLIP_%s"), *Pair.Key), Shader))
		{
			TestEqual(*FString::Printf(TEXT("DREAMUI_CLIP_%s is the C++ value"), *Pair.Key), *Shader, Pair.Value);
		}
	}
	for (const TPair<FString, int32>& Pair : Defines)
	{
		TestTrue(*FString::Printf(TEXT("DREAMUI_CLIP_%s has a C++ twin this test knows"), *Pair.Key), Expected.Contains(Pair.Key));
	}
	TestEqual(TEXT("The chain is as deep as the row"), FDreamUIClipData::InheritClipDepth, Depth);
	TestEqual(TEXT("A row has room for every level masked"), FDreamUIClipData::BlockSizeInBytes, RowBytes);
	TestEqual(TEXT("A level without a mask is the matrix and the corner radius"), FDreamUIClipData::SingleBlockSizeInBytes, BaseTexels * BytesPerTexel);
	TestEqual(TEXT("A chain without masks uploads the row it always did: sixteen levels of five texels"), UnmaskedRowBytes, 16 * 5 * 16);
	// The flags are written as a float value: the largest must be an integer a float holds exactly.
	TestTrue(TEXT("Every flag fits in a float's integers"), (1 << (ReverseShift + 1)) < (1 << 24));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskShapesTest,
	"DreamGUI.ClipMask.EachShapeCutsAlongItsOutline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskShapesTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	const FVector2f Wide(100.0f, 50.0f);
	const FVector2f Square(50.0f, 50.0f);
	{
		const FDreamUIClipLevel Ellipse = MaskLevel(Shaped(EDreamClipMaskShape::Ellipse), Wide);
		TestEqual(TEXT("ellipse: the centre shows"), AlphaAt(Ellipse, FVector2f(0.0f, 0.0f)), 1.0f);
		TestEqual(TEXT("ellipse: just inside the end of its long axis shows"), AlphaAt(Ellipse, FVector2f(98.0f, 0.0f)), 1.0f);
		TestEqual(TEXT("ellipse: the rect's corner is cut"), AlphaAt(Ellipse, FVector2f(90.0f, 40.0f)), 0.0f);
		TestEqual(TEXT("ellipse: past the rect is cut"), AlphaAt(Ellipse, FVector2f(105.0f, 0.0f)), 0.0f);
	}
	{
		FDreamClipMask Diamond = Shaped(EDreamClipMaskShape::Polygon);
		Diamond.Sides = 4;
		const FDreamUIClipLevel Polygon = MaskLevel(Diamond, Square);
		TestEqual(TEXT("square polygon, a corner up: the centre shows"), AlphaAt(Polygon, FVector2f(0.0f, 0.0f)), 1.0f);
		TestEqual(TEXT("...inside its edge shows"), AlphaAt(Polygon, FVector2f(20.0f, 20.0f)), 1.0f);
		TestEqual(TEXT("...past its edge is cut, inside the rect"), AlphaAt(Polygon, FVector2f(30.0f, 30.0f)), 0.0f);
		TestEqual(TEXT("...its top corner shows"), AlphaAt(Polygon, FVector2f(0.0f, 47.0f)), 1.0f);
	}
	{
		FDreamClipMask StarMask = Shaped(EDreamClipMaskShape::Star);
		StarMask.Sides = 5;
		StarMask.InnerRatio = 0.4f;
		const FDreamUIClipLevel Star = MaskLevel(StarMask, Square);
		const float Between = FMath::DegreesToRadians(36.0f);
		TestEqual(TEXT("star: the centre shows"), AlphaAt(Star, FVector2f(0.0f, 0.0f)), 1.0f);
		TestEqual(TEXT("star: its top point shows"), AlphaAt(Star, FVector2f(0.0f, 46.0f)), 1.0f);
		TestEqual(TEXT("star: between two points, past the inner corner, is cut"), AlphaAt(Star, FVector2f(FMath::Sin(Between), FMath::Cos(Between)) * 30.0f), 0.0f);
		TestEqual(TEXT("star: between two points, inside the inner corner, shows"), AlphaAt(Star, FVector2f(FMath::Sin(Between), FMath::Cos(Between)) * 15.0f), 1.0f);
	}
	{
		FDreamClipMask RingMask = Shaped(EDreamClipMaskShape::Ring);
		RingMask.InnerRatio = 0.5f;
		const FDreamUIClipLevel Ring = MaskLevel(RingMask, Square);
		TestEqual(TEXT("ring: the hole is cut"), AlphaAt(Ring, FVector2f(0.0f, 0.0f)), 0.0f);
		TestEqual(TEXT("ring: the band shows"), AlphaAt(Ring, FVector2f(37.0f, 0.0f)), 1.0f);
		TestEqual(TEXT("ring: outside is cut"), AlphaAt(Ring, FVector2f(0.0f, 55.0f)), 0.0f);
	}
	{
		// A Rect mask is the clip rect, rounded by the widget's own corner radius.
		FDreamClipMask Inverted = Shaped(EDreamClipMaskShape::Rect);
		Inverted.bInvert = true;
		const FDreamUIClipLevel Rect = MaskLevel(Inverted, Square, FVector4f(20.0f, 20.0f, 20.0f, 20.0f));
		TestEqual(TEXT("rect, rounded, inverted: the centre is cut"), AlphaAt(Rect, FVector2f(0.0f, 0.0f)), 0.0f);
		TestEqual(TEXT("...the rounded-off corner shows"), AlphaAt(Rect, FVector2f(48.0f, 48.0f)), 1.0f);
		TestEqual(TEXT("...and so does everything outside the rect"), AlphaAt(Rect, FVector2f(200.0f, 0.0f)), 1.0f);
	}
	TestFalse(TEXT("A mask at its defaults is no mask"), FDreamClipMask().IsActive());
	FDreamUIClipLevel Unused;
	TestFalse(TEXT("...and packs to nothing"), FDreamUIClipMaskMath::PackMask(FDreamClipMask(), Square, INDEX_NONE, Unused.Mask));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskFeatherTest,
	"DreamGUI.ClipMask.FeatherFadesInFromTheEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskFeatherTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FDreamClipMask Rect;
	Rect.Feather = FMargin(20.0f, 0.0f, 0.0f, 10.0f);
	const FDreamUIClipLevel RectLevel = MaskLevel(Rect, FVector2f(100.0f, 50.0f));
	TestEqual(TEXT("a rect's left side is half in, halfway across its feather"), AlphaAt(RectLevel, FVector2f(-90.0f, 0.0f)), 0.5f, 1e-4f);
	TestEqual(TEXT("...and all in past it"), AlphaAt(RectLevel, FVector2f(-75.0f, 0.0f)), 1.0f);
	TestEqual(TEXT("its right side, unfeathered, is all in to the edge"), AlphaAt(RectLevel, FVector2f(99.0f, 0.0f)), 1.0f);
	TestEqual(TEXT("its bottom fades over its own width"), AlphaAt(RectLevel, FVector2f(0.0f, -45.0f)), 0.5f, 1e-4f);

	FDreamClipMask Ellipse = Shaped(EDreamClipMaskShape::Ellipse);
	Ellipse.Feather = FMargin(10.0f);
	const FDreamUIClipLevel EllipseLevel = MaskLevel(Ellipse, FVector2f(50.0f, 50.0f));
	TestEqual(TEXT("an ellipse's feather is centred on its outline"), AlphaAt(EllipseLevel, FVector2f(50.0f, 0.0f)), 0.5f, 1e-3f);
	TestEqual(TEXT("...a quarter in, a quarter of the way inside"), AlphaAt(EllipseLevel, FVector2f(47.5f, 0.0f)), 0.75f, 1e-3f);
	TestEqual(TEXT("...and all in past half its width"), AlphaAt(EllipseLevel, FVector2f(44.0f, 0.0f)), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskPatternsTest,
	"DreamGUI.ClipMask.ProceduralPatternsRunAcrossTheClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskPatternsTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	const FVector2f Half(100.0f, 50.0f);
	// At the default threshold and softness a pattern's value is the mask's. The points stay a hair inside the clip rect:
	// on its edge the rect itself is half in.
	const FDreamUIClipLevel Linear = MaskLevel(Pattern(EDreamClipMaskPattern::Linear), Half);
	TestEqual(TEXT("linear: 0 at the left edge"), AlphaAt(Linear, FVector2f(-99.9f, 0.0f)), 0.0f, 1e-3f);
	TestEqual(TEXT("linear: one half at the centre"), AlphaAt(Linear, FVector2f(0.0f, 20.0f)), 0.5f, 1e-4f);
	TestEqual(TEXT("linear: 1 at the right edge"), AlphaAt(Linear, FVector2f(99.9f, 0.0f)), 1.0f, 1e-3f);
	const FDreamUIClipLevel Down = MaskLevel(Pattern(EDreamClipMaskPattern::Linear, 90.0f), Half);
	TestEqual(TEXT("linear turned 90 degrees clockwise: 0 along the top"), AlphaAt(Down, FVector2f(30.0f, 49.9f)), 0.0f, 1e-2f);
	TestEqual(TEXT("...and 1 along the bottom"), AlphaAt(Down, FVector2f(-30.0f, -49.9f)), 1.0f, 1e-2f);
	const FDreamUIClipLevel Radial = MaskLevel(Pattern(EDreamClipMaskPattern::Radial), Half);
	TestEqual(TEXT("radial: 0 at the centre"), AlphaAt(Radial, FVector2f(0.0f, 0.0f)), 0.0f, 1e-4f);
	TestEqual(TEXT("radial: 1 at the middle of a side"), AlphaAt(Radial, FVector2f(0.0f, 49.9f)), 1.0f, 1e-2f);
	const FDreamUIClipLevel Angular = MaskLevel(Pattern(EDreamClipMaskPattern::Angular), Half);
	TestEqual(TEXT("angular: a quarter at three o'clock"), AlphaAt(Angular, FVector2f(80.0f, 0.0f)), 0.25f, 1e-3f);
	TestEqual(TEXT("angular: a half at six o'clock"), AlphaAt(Angular, FVector2f(0.0f, -40.0f)), 0.5f, 1e-3f);
	const FDreamUIClipLevel Diamond = MaskLevel(Pattern(EDreamClipMaskPattern::Diamond), Half);
	TestEqual(TEXT("diamond: one half halfway out along an axis"), AlphaAt(Diamond, FVector2f(50.0f, 0.0f)), 0.5f, 1e-4f);
	FDreamClipMask BlindsMask = Pattern(EDreamClipMaskPattern::Blinds);
	BlindsMask.Frequency = 4.0f;
	TestEqual(TEXT("blinds: halfway across the first of four stripes"), AlphaAt(MaskLevel(BlindsMask, Half), FVector2f(-75.0f, 0.0f)), 0.5f, 1e-4f);
	FDreamClipMask CheckerMask = Pattern(EDreamClipMaskPattern::Checker);
	CheckerMask.Frequency = 2.0f;
	const FDreamUIClipLevel Checker = MaskLevel(CheckerMask, Half);
	TestEqual(TEXT("checker: the top left square is out"), AlphaAt(Checker, FVector2f(-50.0f, 25.0f)), 0.0f);
	TestEqual(TEXT("checker: the one beside it is in"), AlphaAt(Checker, FVector2f(50.0f, 25.0f)), 1.0f);
	FDreamClipMask NoiseMask = Pattern(EDreamClipMaskPattern::Noise);
	NoiseMask.Seed = 7;
	const float NoiseA = AlphaAt(MaskLevel(NoiseMask, Half), FVector2f(13.0f, 7.0f));
	TestTrue(TEXT("noise: a value between 0 and 1"), NoiseA >= 0.0f && NoiseA <= 1.0f);
	TestEqual(TEXT("noise: the same seed, the same value"), AlphaAt(MaskLevel(NoiseMask, Half), FVector2f(13.0f, 7.0f)), NoiseA);
	bool bAnyDiffers = false;
	for (int32 Seed = 8; Seed < 12; ++Seed)
	{
		NoiseMask.Seed = Seed;
		bAnyDiffers |= AlphaAt(MaskLevel(NoiseMask, Half), FVector2f(13.0f, 7.0f)) != NoiseA;
	}
	TestTrue(TEXT("noise: another seed, other values"), bAnyDiffers);
	// A small softness makes a cut at the threshold: a wipe.
	FDreamClipMask Wipe = Pattern(EDreamClipMaskPattern::Linear);
	Wipe.Threshold = 0.25f;
	Wipe.Softness = 0.0f;
	const FDreamUIClipLevel WipeLevel = MaskLevel(Wipe, Half);
	TestEqual(TEXT("a wipe at a quarter: left of it is cut"), AlphaAt(WipeLevel, FVector2f(-60.0f, 0.0f)), 0.0f);
	TestEqual(TEXT("...right of it shows"), AlphaAt(WipeLevel, FVector2f(-40.0f, 0.0f)), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskTextureTest,
	"DreamGUI.ClipMask.ATextureMaskReadsItsChannelWhereItsFillPutsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskTextureTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	UTexture2D* Texture = UTexture2D::CreateTransient(64, 64, PF_B8G8R8A8);
	if (!TestNotNull(TEXT("A 64x64 texture"), Texture))
	{
		return false;
	}
	const FVector2f Half(100.0f, 50.0f);
	FDreamClipMask Mask;
	Mask.Source = EDreamClipMaskSource::Texture;
	Mask.Texture = Texture;
	// The texel answers with its coordinate, so what the mask reads says where it read.
	TArray<FVector2f> Reads;
	auto Probe = [&Reads](FVector2f UV) { Reads.Add(UV); return FLinearColor(0.0f, 0.0f, 0.0f, 1.0f); };
	AlphaAt(MaskLevel(Mask, Half), FVector2f(-100.0f, 50.0f), Probe);
	if (TestEqual(TEXT("stretched: one read"), Reads.Num(), 1))
	{
		TestTrue(TEXT("stretched: the top left corner reads the texture's top left"), Reads[0].Equals(FVector2f(0.0f, 0.0f), 1e-4f));
	}
	Reads.Reset();
	Mask.Fill = EDreamClipMaskFill::Fit;
	TestEqual(TEXT("fit: beside a square texture fitted to a wide rect is nothing"),
		AlphaAt(MaskLevel(Mask, Half), FVector2f(-80.0f, 0.0f), Probe), 0.0f);
	TestEqual(TEXT("fit: over it, its alpha"), AlphaAt(MaskLevel(Mask, Half), FVector2f(0.0f, 0.0f), Probe), 1.0f);
	Reads.Reset();
	Mask.Fill = EDreamClipMaskFill::Tile;
	AlphaAt(MaskLevel(Mask, Half), FVector2f(-100.0f, 0.0f), Probe);
	if (TestEqual(TEXT("tiled: one read"), Reads.Num(), 1))
	{
		TestTrue(TEXT("tiled: at its own size, repeated"), Reads[0].Equals(FVector2f(FMath::Frac(-100.0f / 64.0f + 0.5f), 0.5f), 1e-4f));
	}
	Mask.Fill = EDreamClipMaskFill::Stretch;
	Mask.Channel = EDreamClipMaskChannel::Luminance;
	TestEqual(TEXT("luminance of green"), AlphaAt(MaskLevel(Mask, Half), FVector2f(0.0f, 0.0f), [](FVector2f) { return FLinearColor(0.0f, 1.0f, 0.0f, 0.0f); }),
		0.7152f, 1e-4f);
	FDreamUIClipLevel Dropped;
	Dropped.HalfSize = Half;
	Dropped.bMasked = FDreamUIClipMaskMath::PackMask(Mask, Half, INDEX_NONE, Dropped.Mask);
	TestEqual(TEXT("a texture with no slot is left out: the shape alone clips"),
		AlphaAt(Dropped, FVector2f(0.0f, 0.0f), [](FVector2f) { return FLinearColor::Transparent; }), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskUploadTest,
	"DreamGUI.ClipMask.AChainWithNoMaskUploadsTheRowItAlwaysDid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskUploadTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Clipper = MakeWidget(Stage.World.World, Stage.Root, TEXT("Clipper"), FVector2D(200.0, 100.0));
	Clipper->SetClipping(EDreamWidgetClipping::ClipToBounds);
	UDreamUIDataAsTexture* Rows = NewObject<UDreamUIDataAsTexture>(GetTransientPackage(), NAME_None, RF_Transient);
	Rows->Init(FDreamUIClipData::BlockSizeInBytes, EDreamUIDataAsTexturePixelFormat::R32G32B32A32, 4);
	auto BytesOf = [](TFunctionRef<void()> InWork)
	{
		DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
		InWork();
		return DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true).Counters[static_cast<int32>(DreamUIRenderStats::ECounter::UploadedBytes)];
	};
	{
		FDreamUIClipData Clip(nullptr, Rows, Clipper);
		TestEqual(TEXT("A clip with no mask uploads the row's old size"), BytesOf([&Clip]() { Clip.UpdateData(); }), (int64)DreamUIClipLayout::UnmaskedRowBytes);
		TestEqual(TEXT("...and nothing while it holds still"), BytesOf([&Clip]() { Clip.UpdateData(); }), (int64)0);
		FDreamClipMask Ellipse;
		Ellipse.Shape = EDreamClipMaskShape::Ellipse;
		Clipper->SetClipMask(Ellipse);
		TestEqual(TEXT("A masked one uploads the whole row"), BytesOf([&Clip]() { Clip.UpdateData(); }), (int64)DreamUIClipLayout::RowBytes);
		Clipper->SetClipMask(FDreamClipMask());
		TestEqual(TEXT("...and the old size again once the mask goes"), BytesOf([&Clip]() { Clip.UpdateData(); }), (int64)DreamUIClipLayout::UnmaskedRowBytes);
	}
	TestTrue(TEXT("The rows' texture is wide enough for a row with every level masked"),
		Rows->GetDataTexture() != nullptr && Rows->GetDataTexture()->GetSurfaceWidth() >= DreamUIClipLayout::RowTexels);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskBatchingTest,
	"DreamGUI.ClipMask.MaskTexturesSplitDrawCallsOnlyWhereTheyDiffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskBatchingTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	UTexture2D* A = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
	UTexture2D* B = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
	UTexture2D* C = UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8);
	// Every quad overlaps the last, so the order holds and each can only join the draw call before it.
	TArray<FDreamUIRenderData> RenderData;
	RenderData.Add(RenderDataOf(MakeQuad(FVector2D(0, 0), FVector2D(40, 40), A)));
	RenderData.Add(RenderDataOf(MakeQuad(FVector2D(10, 10), FVector2D(50, 50), A)));
	RenderData.Add(RenderDataOf(MakeQuad(FVector2D(20, 20), FVector2D(60, 60))));
	RenderData.Add(RenderDataOf(MakeQuad(FVector2D(30, 30), FVector2D(70, 70), nullptr, C)));
	RenderData.Add(RenderDataOf(MakeQuad(FVector2D(40, 40), FVector2D(80, 80), B)));
	RenderData.Add(RenderDataOf(MakeQuad(FVector2D(50, 50), FVector2D(90, 90), A)));
	TArray<FDreamUIDrawCall> DrawCalls;
	UDreamCanvas::BatchDrawCallAsync(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0), RenderData, DrawCalls);
	if (!TestEqual(TEXT("Three draw calls: A's, B's, A's again"), DrawCalls.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("The first holds the two under A, the one under no mask, and the one under C in the other slot"), DrawCalls[0].BatchMeshGeometryArray.Num(), 4);
	TestTrue(TEXT("...and binds A and C"), DrawCalls[0].MaskTextures.Slots[0] == A && DrawCalls[0].MaskTextures.Slots[1] == C);
	TestTrue(TEXT("The second binds B"), DrawCalls[1].MaskTextures.Slots[0] == B);
	TestTrue(TEXT("The third binds A"), DrawCalls[2].MaskTextures.Slots[0] == A);

	// No mask anywhere: the batching is what it was.
	TArray<FDreamUIRenderData> Plain;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Plain.Add(RenderDataOf(MakeQuad(FVector2D(Index * 10, 0), FVector2D(Index * 10 + 30, 30))));
	}
	TArray<FDreamUIDrawCall> PlainCalls;
	UDreamCanvas::BatchDrawCallAsync(FVector2D(-500.0, -500.0), FVector2D(500.0, 500.0), Plain, PlainCalls);
	TestEqual(TEXT("Without masks one draw call takes them all"), PlainCalls.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskSlotsTest,
	"DreamGUI.ClipMask.OnlyTheOutermostTwoTextureMasksOfAChainAreRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskSlotsTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	AddExpectedMessagePlain(TEXT("only the outermost 2 of a chain of clips read their textures"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	UTexture2D* Textures[3] = {
		UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8), UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8), UTexture2D::CreateTransient(4, 4, PF_B8G8R8A8) };
	UDreamWidget* Parent = Stage.Root;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UDreamWidget* Clipper = MakeWidget(Stage.World.World, Parent, *FString::Printf(TEXT("Clipper%d"), Index), FVector2D(300.0 - Index * 50.0, 200.0));
		Clipper->SetClipping(EDreamWidgetClipping::ClipToBounds);
		FDreamClipMask Mask;
		Mask.Source = EDreamClipMaskSource::Texture;
		Mask.Texture = Textures[Index];
		Clipper->SetClipMask(Mask);
		Parent = Clipper;
	}
	UDreamWidget* Content = MakeBlock(Stage.World.World, Parent, TEXT("Content"), FVector2D(100.0, 100.0));
	Stage.Frames(3);
	const UDreamVisualBatchMesh* Visual = Cast<UDreamVisualBatchMesh>(Content->GetVisual());
	const FDreamUIGeometry* Geometry = Visual != nullptr ? Visual->GetGeometry() : nullptr;
	if (!TestNotNull(TEXT("The content has a geometry"), Geometry))
	{
		return false;
	}
	TestTrue(TEXT("The outermost mask's texture is in slot 0"), Geometry->MaskTextures.Slots[0] == Textures[0]);
	TestTrue(TEXT("The next one's in slot 1"), Geometry->MaskTextures.Slots[1] == Textures[1]);
	TSharedPtr<FDreamUIClipData> Clip = Content->GetClipData().Pin();
	if (TestTrue(TEXT("The content is clipped"), Clip.IsValid()))
	{
		TArray<FDreamUIClipData::FLevel, TInlineAllocator<16>> Levels;
		Clip->CollectLevels(Levels);
		if (TestEqual(TEXT("Three levels"), Levels.Num(), 3))
		{
			TestEqual(TEXT("The innermost mask's texture has no slot"), Levels[0].TextureSlot, (int32)INDEX_NONE);
			TestEqual(TEXT("The middle one reads slot 1"), Levels[1].TextureSlot, 1);
			TestEqual(TEXT("The outermost reads slot 0"), Levels[2].TextureSlot, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskHitTestTest,
	"DreamGUI.ClipMask.AWidgetHitTestsByItsMaskWhenItAsksTo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskHitTestTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Clipper = MakeWidget(Stage.World.World, Stage.Root, TEXT("Clipper"), FVector2D(200.0, 100.0), FVector2D(40.0, -20.0));
	Clipper->SetClipping(EDreamWidgetClipping::ClipToBounds);
	UDreamWidget* Content = MakeBlock(Stage.World.World, Clipper, TEXT("Content"), FVector2D(400.0, 300.0));
	FDreamClipMask Ellipse;
	Ellipse.Shape = EDreamClipMaskShape::Ellipse;
	Clipper->SetClipMask(Ellipse);
	Stage.Frames();
	const FVector Corner = PointAt(Clipper, FVector2D(90.0, 40.0));
	const FVector Centre = PointAt(Clipper, FVector2D(0.0, 0.0));
	const FVector Outside = PointAt(Clipper, FVector2D(150.0, 0.0));
	TestTrue(TEXT("By bounds, the default: the rect's corner is hit"), Content->IsPointVisibleOnClip(Corner));
	TestFalse(TEXT("...outside the rect is not"), Content->IsPointVisibleOnClip(Outside));
	Ellipse.HitTest = EDreamClipMaskHitTest::Mask;
	Clipper->SetClipMask(Ellipse);
	Stage.Frames();
	TestFalse(TEXT("By the mask: the corner the ellipse cuts is not hit"), Content->IsPointVisibleOnClip(Corner));
	TestTrue(TEXT("...its centre is"), Content->IsPointVisibleOnClip(Centre));
	Ellipse.bInvert = true;
	Clipper->SetClipMask(Ellipse);
	Stage.Frames();
	TestTrue(TEXT("Inverted: the corner is hit"), Content->IsPointVisibleOnClip(Corner));
	TestFalse(TEXT("...the centre is not"), Content->IsPointVisibleOnClip(Centre));
	TestTrue(TEXT("...and neither is the rect's outside cut any more"), Content->IsPointVisibleOnClip(Outside));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipCentreScaleTest,
	"DreamGUI.ClipMask.AClipsCentreIsPlacedThroughItsOwnScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipCentreScaleTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	// A clipper whose pivot is not its centre, under a scaled and turned parent: its rect's centre is its local centre
	// taken through all of that, which is where the hit test has always looked for it.
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Scaled = MakeWidget(Stage.World.World, Stage.Root, TEXT("Scaled"), FVector2D(400.0, 300.0));
	Scaled->SetRelativeScale(FVector(1.0, 2.0, 2.0));
	Scaled->SetRelativeRotationEuler(FRotator(0.0, 0.0, 20.0));
	UDreamWidget* Clipper = MakeWidget(Stage.World.World, Scaled, TEXT("Clipper"), FVector2D(120.0, 80.0));
	Clipper->SetPivot(FVector2D(0.0, 1.0));
	Clipper->SetClipping(EDreamWidgetClipping::ClipToBounds);
	Stage.Frames();
	TSharedPtr<FDreamUIClipData> Clip = Clipper->GetClipData().Pin();
	if (!TestTrue(TEXT("The clipper clips"), Clip.IsValid()))
	{
		return false;
	}
	TArray<FDreamUIClipData::FLevel, TInlineAllocator<16>> Levels;
	Clip->CollectLevels(Levels);
	if (!TestEqual(TEXT("One level"), Levels.Num(), 1))
	{
		return false;
	}
	const FVector Placed(Levels[0].CentreToWorld.TransformPosition(FVector::ZeroVector));
	TestTrue(TEXT("The level's centre is the clipper's centre"), Placed.Equals(PointAt(Clipper, FVector2D::ZeroVector), 0.01));
	TestTrue(TEXT("A point just inside its corner is in the clip"), Clipper->IsPointVisibleOnClip(PointAt(Clipper, FVector2D(55.0, 35.0))));
	TestFalse(TEXT("A point just past its edge is not"), Clipper->IsPointVisibleOnClip(PointAt(Clipper, FVector2D(65.0, 0.0))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipToBelowUnderlayTest,
	"DreamGUI.ClipToBelow.TheLayerBelowIsTheSiblingBeforeThatDoesNotClipToBelow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipToBelowUnderlayTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* First = MakeBlock(World, Stage.Root, TEXT("First"), FVector2D(50.0, 50.0));
	UDreamWidget* Base = MakeBlock(World, Stage.Root, TEXT("Base"), FVector2D(50.0, 50.0));
	UDreamWidget* ClippedA = MakeBlock(World, Stage.Root, TEXT("ClippedA"), FVector2D(50.0, 50.0));
	UDreamWidget* ClippedB = MakeBlock(World, Stage.Root, TEXT("ClippedB"), FVector2D(50.0, 50.0));
	UDreamWidget* Other = MakeBlock(World, Stage.Root, TEXT("Other"), FVector2D(50.0, 50.0));
	UDreamWidget* ClippedC = MakeBlock(World, Stage.Root, TEXT("ClippedC"), FVector2D(50.0, 50.0));
	ClippedA->SetClipToBelow(true);
	ClippedB->SetClipToBelow(true);
	ClippedC->SetClipToBelow(true);
	TestTrue(TEXT("A widget that does not clip to the layer below has none"), Base->FindClipToBelowUnderlay() == nullptr);
	TestTrue(TEXT("The first one clips to the sibling before it"), ClippedA->FindClipToBelowUnderlay() == Base);
	TestTrue(TEXT("One in a row shares the same layer"), ClippedB->FindClipToBelowUnderlay() == Base);
	TestTrue(TEXT("A later run clips to its own"), ClippedC->FindClipToBelowUnderlay() == Other);
	Base->SetSiblingIndex(0);
	TestTrue(TEXT("Moved: the run clips to whatever is before it now"), ClippedA->FindClipToBelowUnderlay() == First);
	ClippedA->SetSiblingIndex(0);
	TestTrue(TEXT("With nothing before it a widget clips to nothing"), ClippedA->FindClipToBelowUnderlay() == nullptr);
	TestTrue(TEXT("...and the one it ran with clips to what is before that one now"), ClippedB->FindClipToBelowUnderlay() == First);
	Stage.Frames();
	TestTrue(TEXT("...and is not clipped at all"), ClippedA->IsPointVisibleOnClip(PointAt(ClippedA, FVector2D(0.0, 0.0))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipToBelowHitTest,
	"DreamGUI.ClipToBelow.WhatClipsToALayerShowsAndIsHitOnlyWhereThatLayerIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipToBelowHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Base = MakeBlock(World, Stage.Root, TEXT("Base"), FVector2D(100.0, 100.0), FVector2D(-100.0, 0.0));
	UDreamWidget* Clipped = MakeBlock(World, Stage.Root, TEXT("Clipped"), FVector2D(400.0, 300.0));
	Clipped->SetClipToBelow(true);
	Stage.Frames();
	const FVector OnBase = PointAt(Base, FVector2D(0.0, 0.0));
	const FVector BesideBase = PointAt(Base, FVector2D(80.0, 0.0));
	TestTrue(TEXT("Over the layer below: hit"), Clipped->IsPointVisibleOnClip(OnBase));
	TestFalse(TEXT("Beside it: not"), Clipped->IsPointVisibleOnClip(BesideBase));
	FDreamClipUnderlay Underlay;
	if (TestTrue(TEXT("A texture is followed"), FDreamUIClipData::DescribeUnderlay(Base, Underlay)))
	{
		TestEqual(TEXT("...as an image"), (int32)Underlay.Kind, (int32)FDreamClipUnderlay::EKind::Image);
		TestTrue(TEXT("...its quad the widget's rect"), Underlay.HalfSize.Equals(FVector2f(50.0f, 50.0f), 0.01f));
		TestEqual(TEXT("...at its opacity"), Underlay.Opacity, 1.0f);
	}

	Base->SetWidgetActive(false);
	Stage.Frames();
	TestFalse(TEXT("A hidden layer below hides what clips to it"), Clipped->IsPointVisibleOnClip(OnBase));
	Base->SetWidgetActive(true);

	// A rect block below: its rounded body.
	UDreamWidget* BlockWidget = MakeWidget(World, Stage.Root, TEXT("Block"), FVector2D(100.0, 100.0), FVector2D(150.0, 0.0));
	UDreamRectBlock* Block = BlockWidget->CreateNewVisual<UDreamRectBlock>();
	Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
	Block->SetCornerRadius(FVector4(40.0, 40.0, 40.0, 40.0));
	UDreamWidget* OnBlock = MakeBlock(World, Stage.Root, TEXT("OnBlock"), FVector2D(400.0, 300.0));
	OnBlock->SetClipToBelow(true);
	Stage.Frames();
	TestTrue(TEXT("Over the rect block's middle: hit"), OnBlock->IsPointVisibleOnClip(PointAt(BlockWidget, FVector2D(0.0, 0.0))));
	TestFalse(TEXT("In its rounded-off corner: not"), OnBlock->IsPointVisibleOnClip(PointAt(BlockWidget, FVector2D(46.0, 46.0))));

	// The layer below going away: the next one before it is the layer below now.
	Base->DestroyWidget();
	Stage.Frames();
	TestTrue(TEXT("With the layer below gone, the widget clips to nothing before it and shows everywhere"),
		Clipped->IsPointVisibleOnClip(BesideBase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipToBelowUnsupportedTest,
	"DreamGUI.ClipToBelow.TextBelowIsNotClippedToAndSaysWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipToBelowUnsupportedTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	AddExpectedMessagePlain(TEXT("clips to the layer below"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	UWorld* World = Stage.World.World;
	UDreamWidget* Label = MakeWidget(World, Stage.Root, TEXT("Label"), FVector2D(100.0, 30.0));
	if (UDreamText* Text = Label->CreateNewVisual<UDreamText>())
	{
		Text->SetText(FText::FromString(TEXT("Below")));
	}
	UDreamWidget* Clipped = MakeBlock(World, Stage.Root, TEXT("Clipped"), FVector2D(400.0, 300.0));
	Clipped->SetClipToBelow(true);
	Stage.Frames(3);
	FDreamClipUnderlay Underlay;
	TestFalse(TEXT("Text is not followed"), FDreamUIClipData::DescribeUnderlay(Label, Underlay));
	TestFalse(TEXT("...and says why"), Underlay.Unsupported.IsEmpty());
	TestTrue(TEXT("What clips to it is not clipped"), Clipped->IsPointVisibleOnClip(PointAt(Clipped, FVector2D(150.0, 100.0))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipToBelowNineSliceTest,
	"DreamGUI.ClipToBelow.ANineSlicedLayerBelowIsFollowedSliceBySlice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipToBelowNineSliceTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("A world with a UI manager and a canvas"), Stage.IsUsable()))
	{
		return false;
	}
	UWorld* World = Stage.World.World;
	UDreamWidget* Frame = MakeWidget(World, Stage.Root, TEXT("Frame"), FVector2D(200.0, 120.0));
	UDreamTexture* Sliced = Frame->CreateNewVisual<UDreamTexture>();
	UTexture2D* Texture = UTexture2D::CreateTransient(32, 32, PF_B8G8R8A8);
	Sliced->SetTexture(Texture);
	FDreamUISpriteInfo Info = Sliced->GetSpriteInfo();
	Info.Border = FMargin(8.0f, 4.0f, 8.0f, 4.0f);
	Sliced->SetSpriteInfo(Info);
	Sliced->SetDrawType(EDreamUISpriteDrawType::SlicedFrame);
	UDreamWidget* Clipped = MakeBlock(World, Stage.Root, TEXT("Clipped"), FVector2D(400.0, 300.0));
	Clipped->SetClipToBelow(true);
	Stage.Frames();
	FDreamClipUnderlay Underlay;
	if (!TestTrue(TEXT("A nine-sliced texture is followed"), FDreamUIClipData::DescribeUnderlay(Frame, Underlay)))
	{
		return false;
	}
	TestFalse(TEXT("...a frame without its centre"), Underlay.bFillCentre);
	TestTrue(TEXT("...its borders in pixels, left, right, bottom, top"), Underlay.Border.Equals(FVector4f(8.0f, 8.0f, 4.0f, 4.0f), 0.01f));
	TestTrue(TEXT("...its quad the widget's rect"), Underlay.HalfSize.Equals(FVector2f(100.0f, 60.0f), 0.01f));
	TestTrue(TEXT("Over its border: hit"), Clipped->IsPointVisibleOnClip(PointAt(Frame, FVector2D(-97.0, 0.0))));
	TestFalse(TEXT("In its empty centre: not"), Clipped->IsPointVisibleOnClip(PointAt(Frame, FVector2D(0.0, 0.0))));
	return true;
}

#endif
