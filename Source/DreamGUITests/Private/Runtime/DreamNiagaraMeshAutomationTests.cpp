// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Interfaces/IPluginManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "DreamNiagaraMeshBuilder.h"
#include "DreamNiagaraParticles.h"
#include "DreamNiagaraVertexLayout.h"

/*
 * Particles into vertices, without a simulation: the builder fed by hand, and every rule of how a particle's vertex is laid out
 * held to the shader that reads it. What the simulation adds -- reading its particles, placing them on a widget -- is in
 * DreamNiagaraSystemAutomationTests.cpp.
 */
namespace DreamNiagaraMeshTestLocal
{
	bool Near(float A, float B, float Tolerance = 1e-3f)
	{
		return FMath::Abs(A - B) <= Tolerance;
	}

	bool Near(const FVector2f& A, const FVector2f& B, float Tolerance = 1e-3f)
	{
		return Near(A.X, B.X, Tolerance) && Near(A.Y, B.Y, Tolerance);
	}

	bool Near(const FVector3f& A, const FVector3f& B, float Tolerance = 1e-3f)
	{
		return Near(A.X, B.X, Tolerance) && Near(A.Y, B.Y, Tolerance) && Near(A.Z, B.Z, Tolerance);
	}

	FString Join(const TArray<int32>& InValues)
	{
		FString Out;
		for (int32 Value : InValues)
		{
			Out += Out.IsEmpty() ? FString::FromInt(Value) : FString::Printf(TEXT(" %d"), Value);
		}
		return Out;
	}

	FString Describe(const FVector2f& V) { return FString::Printf(TEXT("(%.4f, %.4f)"), V.X, V.Y); }
	FString Describe(const FVector3f& V) { return FString::Printf(TEXT("(%.4f, %.4f, %.4f)"), V.X, V.Y, V.Z); }

	/** The plane as the widget's, one unit to one canvas unit, simulation X right and Z up: the XZ mapping. */
	FDreamNiagaraBuildSpace XZSpace(float InScale = 1.0f)
	{
		FDreamNiagaraBuildSpace Space;
		Space.SimToPlane = FMatrix44f(
			FPlane4f(0.0f, InScale, 0.0f, 0.0f),
			FPlane4f(-InScale, 0.0f, 0.0f, 0.0f),
			FPlane4f(0.0f, 0.0f, InScale, 0.0f),
			FPlane4f(0.0f, 0.0f, 0.0f, 1.0f));
		Space.SizeScale = InScale;
		return Space;
	}

	/** Arrays kept for a segment, as a section keeps them. */
	struct FTarget
	{
		TArray<FDreamUIMeshVertex> Vertices;
		TArray<FDreamUIMeshIndex> Indices;
		FDreamNiagaraMeshTarget Target;
		FTarget()
		{
			Target.Vertices = &Vertices;
			Target.Indices = &Indices;
		}
		void Reset()
		{
			Target.NumVertices = 0;
			Target.NumIndices = 0;
			Target.bGrew = false;
			Target.Bounds = FBox(ForceInit);
		}
	};

	/** The canvas position (right, up) of a vertex. */
	FVector2f At(const FDreamUIMeshVertex& InVertex)
	{
		return FVector2f(InVertex.Position.Y, InVertex.Position.Z);
	}

	/** The signed area of a triangle on the plane: positive is anticlockwise, as DreamGUI's quads are wound. */
	float Area(const FTarget& InTarget, int32 InFirstIndex)
	{
		const FVector2f A = At(InTarget.Vertices[InTarget.Indices[InFirstIndex]]);
		const FVector2f B = At(InTarget.Vertices[InTarget.Indices[InFirstIndex + 1]]);
		const FVector2f C = At(InTarget.Vertices[InTarget.Indices[InFirstIndex + 2]]);
		return 0.5f * ((B.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (B.Y - A.Y));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraVertexLayoutTest,
	"DreamGUI.Niagara.TheVertexLayoutIsTheShadersOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraVertexLayoutTest::RunTest(const FString& Parameters)
{
	// The three #define DREAMUI_PARTICLE_* of DreamUIParticle.ush against the constants the vertices are written with.
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
	if (!TestNotNull(TEXT("The plugin is found"), Plugin.Get()))
	{
		return false;
	}
	const FString Path = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/Private/DreamUIParticle.ush"));
	FString Source;
	if (!TestTrue(*FString::Printf(TEXT("%s reads"), *Path), FFileHelper::LoadFileToString(Source, *Path)))
	{
		return false;
	}
	TMap<FString, double> Defines;
	FRegexMatcher Matcher(FRegexPattern(TEXT("#define\\s+(DREAMUI_PARTICLE_\\w+)\\s+([0-9.]+)")), Source);
	while (Matcher.FindNext())
	{
		Defines.Add(Matcher.GetCaptureGroup(1), FCString::Atod(*Matcher.GetCaptureGroup(2)));
	}
	TestEqual(TEXT("Every constant the shader has is one of the three"), Defines.Num(), 3);
	TestEqual(TEXT("DREAMUI_PARTICLE_MARK"), Defines.FindRef(TEXT("DREAMUI_PARTICLE_MARK")), static_cast<double>(DreamNiagaraVertexLayout::ParticleMark));
	TestEqual(TEXT("DREAMUI_PARTICLE_SCALE_STEPS"), Defines.FindRef(TEXT("DREAMUI_PARTICLE_SCALE_STEPS")), static_cast<double>(DreamNiagaraVertexLayout::ScaleSteps));
	TestEqual(TEXT("DREAMUI_PARTICLE_BLEND_RANGE"), Defines.FindRef(TEXT("DREAMUI_PARTICLE_BLEND_RANGE")), static_cast<double>(DreamNiagaraVertexLayout::BlendRange));
	// And the mark is one no other element of the built-in shader takes (0 sprites, 1 to 3 glyphs, 4 rect blocks).
	TestTrue(TEXT("The mark is past every other element's"), DreamNiagaraVertexLayout::ParticleMark > 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraPackTest,
	"DreamGUI.Niagara.AColoursScaleAndAFrameBlendPackAndComeBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraPackTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMeshTestLocal;
	const float Scales[] = { 1.0f, 1.0625f, 2.0f, 7.5f, 64.0f, 255.0f };
	const float Blends[] = { 0.0f, 0.001f, 0.25f, 0.5f, 0.9375f, 0.999f };
	for (float Scale : Scales)
	{
		for (float Blend : Blends)
		{
			float OutScale = 0.0f, OutBlend = 0.0f;
			DreamNiagaraVertexLayout::Unpack(DreamNiagaraVertexLayout::Pack(Scale, Blend), OutScale, OutBlend);
			TestTrue(*FString::Printf(TEXT("Scale %.4f, blend %.4f: the scale comes back (%.4f)"), Scale, Blend, OutScale), Near(OutScale, Scale, 1e-4f));
			TestTrue(*FString::Printf(TEXT("Scale %.4f, blend %.4f: the blend comes back (%.4f)"), Scale, Blend, OutBlend), Near(OutBlend, Blend, 2e-3f));
		}
	}
	// A colour no brighter than 1 is kept as it is; a brighter one by the smallest step that brings it to 1.
	TestEqual(TEXT("A plain colour has no scale"), DreamNiagaraVertexLayout::ScaleFor(FLinearColor(0.2f, 1.0f, 0.5f, 1.0f)), 1.0f);
	const FLinearColor Bright(3.0f, 1.5f, 0.3f, 0.75f);
	const float Scale = DreamNiagaraVertexLayout::ScaleFor(Bright);
	TestTrue(TEXT("A bright colour's scale is at least its brightest channel"), Scale >= 3.0f && Scale <= 3.0f + 1.0f / DreamNiagaraVertexLayout::ScaleSteps);
	const FColor Encoded = DreamNiagaraVertexLayout::EncodeColor(Bright, Scale);
	const FLinearColor Back = FLinearColor::FromSRGBColor(Encoded) * Scale;
	TestTrue(*FString::Printf(TEXT("The colour times its scale is the colour (%.3f, %.3f, %.3f)"), Back.R, Back.G, Back.B),
		Near(Back.R, Bright.R, 0.05f) && Near(Back.G, Bright.G, 0.05f) && Near(Back.B, Bright.B, 0.02f));
	TestEqual(TEXT("Alpha is not scaled"), Encoded.A, static_cast<uint8>(FMath::RoundToInt(0.75f * 255.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraSpriteQuadTest,
	"DreamGUI.Niagara.ASpriteIsAQuadOfItsSizeAboutItsPivotTurnedClockwiseByItsRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraSpriteQuadTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMeshTestLocal;
	FDreamNiagaraVertexCommon Common;
	Common.WidgetRecord = 7.0f;
	FDreamNiagaraSpriteSettings Settings;
	TArray<FDreamNiagaraSpriteParticle> Particles;
	FDreamNiagaraSpriteParticle& Plain = Particles.AddDefaulted_GetRef();
	// At simulation (10, 0, 20): on the XZ plane, right 10 and up 20, scaled by 2.
	Plain.Position = FVector3f(10.0f, 5.0f, 20.0f);
	Plain.Size = FVector2f(8.0f, 4.0f);
	Plain.Color = FLinearColor(1.0f, 0.5f, 0.25f, 1.0f);
	FDreamNiagaraSpriteParticle& Pivoted = Particles.AddDefaulted_GetRef();
	Pivoted.Size = FVector2f(10.0f, 10.0f);
	Pivoted.PivotOffset = FVector2f(0.0f, 0.0f);
	FDreamNiagaraSpriteParticle& Turned = Particles.AddDefaulted_GetRef();
	Turned.Size = FVector2f(10.0f, 2.0f);
	Turned.Rotation = 90.0f;

	FTarget Out;
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, Settings, XZSpace(2.0f), Common, Out.Target);
	TestEqual(TEXT("Four vertices to a sprite"), Out.Target.NumVertices, 12);
	TestEqual(TEXT("Six indices to a sprite"), Out.Target.NumIndices, 18);
	TestTrue(TEXT("The arrays grew from nothing"), Out.Target.bGrew);
	TestEqual(TEXT("Grown by tiers: 64 at the least"), Out.Vertices.Num(), 64);

	// The plain one: centred on its position (the depth flattened), 16 by 8 canvas units, corners bottom left first.
	const FVector2f Centre(20.0f, 40.0f);
	TestTrue(*FString::Printf(TEXT("Bottom left %s"), *Describe(At(Out.Vertices[0]))), Near(At(Out.Vertices[0]), Centre + FVector2f(-8.0f, -4.0f)));
	TestTrue(*FString::Printf(TEXT("Bottom right %s"), *Describe(At(Out.Vertices[1]))), Near(At(Out.Vertices[1]), Centre + FVector2f(8.0f, -4.0f)));
	TestTrue(*FString::Printf(TEXT("Top left %s"), *Describe(At(Out.Vertices[2]))), Near(At(Out.Vertices[2]), Centre + FVector2f(-8.0f, 4.0f)));
	TestTrue(*FString::Printf(TEXT("Top right %s"), *Describe(At(Out.Vertices[3]))), Near(At(Out.Vertices[3]), Centre + FVector2f(8.0f, 4.0f)));
	TestEqual(TEXT("Flattened onto the plane"), Out.Vertices[0].Position.X, 0.0f);
	TestTrue(TEXT("Texture coordinates from the top left: (0, 0) at the top left corner"), Near(Out.Vertices[2].TextureCoordinate[0], FVector2f(0.0f, 0.0f)));
	TestTrue(TEXT("...(1, 1) at the bottom right"), Near(Out.Vertices[1].TextureCoordinate[0], FVector2f(1.0f, 1.0f)));
	TestEqual(TEXT("Every vertex names the widget's record"), Out.Vertices[5].TextureCoordinate[1].X, 7.0f);
	TestEqual(TEXT("The colour, in sRGB"), Out.Vertices[0].Color, FLinearColor(1.0f, 0.5f, 0.25f, 1.0f).ToFColor(true));
	for (int32 Triangle = 0; Triangle < 6; ++Triangle)
	{
		TestTrue(*FString::Printf(TEXT("Triangle %d is wound anticlockwise"), Triangle), Area(Out, Triangle * 3) > 0.0f);
	}

	// The pivoted one: its position is its top left corner (the pivot at (0, 0) in the sprite's own coordinates).
	TestTrue(*FString::Printf(TEXT("A pivot at the top left: the top left corner on the position %s"), *Describe(At(Out.Vertices[6]))), Near(At(Out.Vertices[6]), FVector2f(0.0f, 0.0f)));
	TestTrue(*FString::Printf(TEXT("...and the bottom right one size away, down and right %s"), *Describe(At(Out.Vertices[5]))), Near(At(Out.Vertices[5]), FVector2f(20.0f, -20.0f)));

	// The turned one: 90 degrees clockwise, its width now runs down the screen and its top points right.
	TestTrue(*FString::Printf(TEXT("Turned: the top left corner is at the top right %s"), *Describe(At(Out.Vertices[10]))), Near(At(Out.Vertices[10]), FVector2f(2.0f, 10.0f)));
	TestTrue(*FString::Printf(TEXT("Turned: the bottom right corner is at the bottom left %s"), *Describe(At(Out.Vertices[9]))), Near(At(Out.Vertices[9]), FVector2f(-2.0f, -10.0f)));

	const FBox Box = Out.Target.Bounds;
	TestTrue(TEXT("The box holds every corner"), Box.IsValid && Near(static_cast<float>(Box.Max.Y), 28.0f) && Near(static_cast<float>(Box.Max.Z), 44.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraSubImageTest,
	"DreamGUI.Niagara.ASpriteSheetFrameAndItsBlendTowardsTheNextAreWhereNiagaraPutsThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraSubImageTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMeshTestLocal;
	// A 4 by 2 sheet: frame 5 is the second column of the second row; frame 7 the last; frame 8 runs past the bottom.
	const FIntPoint Sheet(4, 2);
	TestTrue(TEXT("Frame 0's top left"), Near(FDreamNiagaraMeshBuilder::SubImageUV(0.0f, Sheet, FVector2f(0.0f, 0.0f)), FVector2f(0.0f, 0.0f)));
	TestTrue(TEXT("Frame 5's top left"), Near(FDreamNiagaraMeshBuilder::SubImageUV(5.0f, Sheet, FVector2f(0.0f, 0.0f)), FVector2f(0.25f, 0.5f)));
	TestTrue(TEXT("Frame 5's bottom right"), Near(FDreamNiagaraMeshBuilder::SubImageUV(5.0f, Sheet, FVector2f(1.0f, 1.0f)), FVector2f(0.5f, 1.0f)));
	TestTrue(TEXT("Frame 7's top left"), Near(FDreamNiagaraMeshBuilder::SubImageUV(7.0f, Sheet, FVector2f(0.0f, 0.0f)), FVector2f(0.75f, 0.5f)));
	TestTrue(TEXT("Frame 8 wraps its column and runs on its row, as in Niagara"), Near(FDreamNiagaraMeshBuilder::SubImageUV(8.0f, Sheet, FVector2f(0.0f, 0.0f)), FVector2f(0.0f, 1.0f)));

	FDreamNiagaraSpriteSettings Settings;
	Settings.SubImageSize = Sheet;
	Settings.bSubImageBlend = true;
	TArray<FDreamNiagaraSpriteParticle> Particles;
	Particles.AddDefaulted_GetRef().SubImageIndex = 2.25f;
	Particles.AddDefaulted_GetRef().SubImageIndex = 3.75f;
	FTarget Out;
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	// Sprite 0: frame 2 in UV0, frame 3 in UV2, a quarter of the way; its top left corner is vertex 2.
	TestTrue(*FString::Printf(TEXT("Frame 2 in UV0 %s"), *Describe(Out.Vertices[2].TextureCoordinate[0])), Near(Out.Vertices[2].TextureCoordinate[0], FVector2f(0.5f, 0.0f)));
	TestTrue(*FString::Printf(TEXT("Frame 3 in UV2 %s"), *Describe(Out.Vertices[2].TextureCoordinate[2])), Near(Out.Vertices[2].TextureCoordinate[2], FVector2f(0.75f, 0.0f)));
	float Scale = 0.0f, Blend = 0.0f;
	DreamNiagaraVertexLayout::Unpack(Out.Vertices[2].TextureCoordinate[3].X, Scale, Blend);
	TestTrue(*FString::Printf(TEXT("Blended a quarter of the way (%.4f)"), Blend), Near(Blend, 0.25f, 2e-3f));
	TestEqual(TEXT("A plain colour keeps a scale of 1"), Scale, 1.0f);
	// Sprite 1: frame 3 (the first row's last) blends towards frame 4, the second row's first.
	TestTrue(*FString::Printf(TEXT("Frame 3 in UV0 %s"), *Describe(Out.Vertices[6].TextureCoordinate[0])), Near(Out.Vertices[6].TextureCoordinate[0], FVector2f(0.75f, 0.0f)));
	TestTrue(*FString::Printf(TEXT("Frame 4 in UV2 %s"), *Describe(Out.Vertices[6].TextureCoordinate[2])), Near(Out.Vertices[6].TextureCoordinate[2], FVector2f(0.0f, 0.5f)));
	DreamNiagaraVertexLayout::Unpack(Out.Vertices[6].TextureCoordinate[3].X, Scale, Blend);
	TestTrue(*FString::Printf(TEXT("Blended three quarters of the way (%.4f)"), Blend), Near(Blend, 0.75f, 2e-3f));

	// Without the renderer's blend, a frame's fraction is not a blend.
	Settings.bSubImageBlend = false;
	Out.Reset();
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	DreamNiagaraVertexLayout::Unpack(Out.Vertices[2].TextureCoordinate[3].X, Scale, Blend);
	TestEqual(TEXT("No blend without the renderer's"), Blend, 0.0f);
	TestFalse(TEXT("Rebuilt in the arrays it has: nothing grows"), Out.Target.bGrew);

	// A dynamic parameter goes where a converted material reads it: x in UV1.y, y in UV3.y, z and w in UV4.
	Particles[0].DynamicParameter = FVector4f(0.1f, 0.2f, 0.3f, 0.4f);
	Out.Reset();
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	TestTrue(TEXT("The dynamic parameter's four values"), Near(Out.Vertices[0].TextureCoordinate[1].Y, 0.1f) && Near(Out.Vertices[0].TextureCoordinate[3].Y, 0.2f)
		&& Near(Out.Vertices[0].UV4, FVector2f(0.3f, 0.4f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraSortTest,
	"DreamGUI.Niagara.SpritesAreDrawnInTheirRenderersSortOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraSortTest::RunTest(const FString& Parameters)
{
	TArray<FDreamNiagaraSpriteParticle> Particles;
	const float Keys[] = { 0.5f, 0.1f, 0.9f, 0.1f, 0.3f };
	for (float Key : Keys)
	{
		Particles.AddDefaulted_GetRef().SortKey = Key;
	}
	TArray<int32> Order;
	FDreamNiagaraMeshBuilder::SortSprites(Particles, EDreamNiagaraSortMode::None, Order);
	TestEqual(TEXT("Unsorted: the simulation's order"), DreamNiagaraMeshTestLocal::Join(Order), FString(TEXT("0 1 2 3 4")));
	FDreamNiagaraMeshBuilder::SortSprites(Particles, EDreamNiagaraSortMode::Ascending, Order);
	TestEqual(TEXT("Ascending, ties in the simulation's order"), DreamNiagaraMeshTestLocal::Join(Order), FString(TEXT("1 3 4 0 2")));
	FDreamNiagaraMeshBuilder::SortSprites(Particles, EDreamNiagaraSortMode::Descending, Order);
	TestEqual(TEXT("Descending, ties in the simulation's order"), DreamNiagaraMeshTestLocal::Join(Order), FString(TEXT("2 0 4 1 3")));

	// The vertices follow: the first quad drawn is the lowest key's.
	for (int32 Index = 0; Index < Particles.Num(); ++Index)
	{
		Particles[Index].Position = FVector3f(static_cast<float>(Index) * 100.0f, 0.0f, 0.0f);
	}
	FDreamNiagaraSpriteSettings Settings;
	Settings.SortMode = EDreamNiagaraSortMode::Ascending;
	DreamNiagaraMeshTestLocal::FTarget Out;
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, Settings, DreamNiagaraMeshTestLocal::XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	const float FirstCentre = 0.5f * (Out.Vertices[0].Position.Y + Out.Vertices[1].Position.Y);
	TestTrue(*FString::Printf(TEXT("The first quad is particle 1's (centre %.1f)"), FirstCentre), DreamNiagaraMeshTestLocal::Near(FirstCentre, 100.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraAlignmentTest,
	"DreamGUI.Niagara.AnAlignedSpritesTopPointsAlongItsVelocityOrItsAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraAlignmentTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMeshTestLocal;
	TArray<FDreamNiagaraSpriteParticle> Particles;
	FDreamNiagaraSpriteParticle& Moving = Particles.AddDefaulted_GetRef();
	// Moving right on the screen (simulation +X), tall and thin, its rotation not read.
	Moving.Velocity = FVector3f(50.0f, 0.0f, 0.0f);
	Moving.Size = FVector2f(2.0f, 10.0f);
	Moving.Rotation = 45.0f;
	FDreamNiagaraSpriteSettings Settings;
	Settings.Alignment = EDreamNiagaraSpriteAlignment::Velocity;
	FTarget Out;
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	// The texture's top (v = 0: corners 2 and 3) is the end the sprite moves towards.
	const FVector2f Top = 0.5f * (At(Out.Vertices[2]) + At(Out.Vertices[3]));
	const FVector2f Bottom = 0.5f * (At(Out.Vertices[0]) + At(Out.Vertices[1]));
	TestTrue(*FString::Printf(TEXT("Its top is ahead %s, its bottom behind %s"), *Describe(Top), *Describe(Bottom)), Near(Top, FVector2f(5.0f, 0.0f)) && Near(Bottom, FVector2f(-5.0f, 0.0f)));

	// A custom alignment points it the same way, and then its rotation turns it.
	Particles[0].Velocity = FVector3f::ZeroVector;
	Particles[0].Alignment = FVector3f(0.0f, 0.0f, 1.0f);
	Particles[0].Rotation = 90.0f;
	Settings.Alignment = EDreamNiagaraSpriteAlignment::Custom;
	Out.Reset();
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	const FVector2f TurnedTop = 0.5f * (At(Out.Vertices[2]) + At(Out.Vertices[3]));
	TestTrue(*FString::Printf(TEXT("Aligned up, then turned clockwise: its top points right %s"), *Describe(TurnedTop)), Near(TurnedTop, FVector2f(5.0f, 0.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraRibbonTopologyTest,
	"DreamGUI.Niagara.RibbonsAreStripsGroupedByTheirIdInLinkOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraRibbonTopologyTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMeshTestLocal;
	// Two ribbons of three points each, their points held out of order and interleaved.
	TArray<FDreamNiagaraRibbonParticle> Particles;
	auto Add = [&Particles](int64 InKey, float InOrder, FVector3f InPosition)
	{
		FDreamNiagaraRibbonParticle& Point = Particles.AddDefaulted_GetRef();
		Point.RibbonKey = InKey;
		Point.LinkOrder = InOrder;
		Point.Position = InPosition;
		Point.Width = 4.0f;
	};
	Add(7, 2.0f, FVector3f(20.0f, 0.0f, 0.0f));
	Add(3, 1.0f, FVector3f(10.0f, 0.0f, 50.0f));
	Add(7, 0.0f, FVector3f(0.0f, 0.0f, 0.0f));
	Add(3, 0.0f, FVector3f(0.0f, 0.0f, 50.0f));
	Add(7, 1.0f, FVector3f(10.0f, 0.0f, 0.0f));
	Add(3, 2.0f, FVector3f(20.0f, 0.0f, 50.0f));

	TArray<TArray<int32>> Ribbons;
	FDreamNiagaraMeshBuilder::GroupRibbons(Particles, 0, Ribbons);
	TestEqual(TEXT("Two ribbons"), Ribbons.Num(), 2);
	if (Ribbons.Num() == 2)
	{
		TestEqual(TEXT("The lower key first, in link order"), Join(Ribbons[0]), FString(TEXT("3 1 5")));
		TestEqual(TEXT("Then the other, in link order"), Join(Ribbons[1]), FString(TEXT("2 4 0")));
	}
	FDreamNiagaraMeshBuilder::GroupRibbons(Particles, 1, Ribbons);
	TestEqual(TEXT("A limit of one keeps the lowest key's"), Ribbons.Num(), 1);

	FDreamNiagaraRibbonSettings Settings;
	FTarget Out;
	FDreamNiagaraMeshBuilder::BuildRibbons(Particles, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	TestEqual(TEXT("Two vertices to a point"), Out.Target.NumVertices, 12);
	TestEqual(TEXT("Two triangles to a piece, two pieces to a ribbon"), Out.Target.NumIndices, 24);
	// The first ribbon runs right along y = 50: its left edge (V = 0) is above, 2 units off.
	TestTrue(*FString::Printf(TEXT("The first point's left edge %s"), *Describe(At(Out.Vertices[0]))), Near(At(Out.Vertices[0]), FVector2f(0.0f, 52.0f)));
	TestTrue(*FString::Printf(TEXT("...and its right edge %s"), *Describe(At(Out.Vertices[1]))), Near(At(Out.Vertices[1]), FVector2f(0.0f, 48.0f)));
	TestTrue(TEXT("V is 0 on the left edge and 1 on the right"), Near(Out.Vertices[0].TextureCoordinate[0].Y, 0.0f) && Near(Out.Vertices[1].TextureCoordinate[0].Y, 1.0f));
	TestTrue(TEXT("U runs 0 to 1 along it, by point"), Near(Out.Vertices[0].TextureCoordinate[0].X, 0.0f) && Near(Out.Vertices[2].TextureCoordinate[0].X, 0.5f) && Near(Out.Vertices[4].TextureCoordinate[0].X, 1.0f));
	for (int32 Triangle = 0; Triangle < 8; ++Triangle)
	{
		TestTrue(*FString::Printf(TEXT("Triangle %d is wound anticlockwise"), Triangle), Area(Out, Triangle * 3) > 0.0f);
	}
	// No triangle joins the two ribbons.
	bool bApart = true;
	for (int32 Index = 0; Index < 12; ++Index)
	{
		bApart &= Out.Indices[Index] < 6;
	}
	for (int32 Index = 12; Index < 24; ++Index)
	{
		bApart &= Out.Indices[Index] >= 6;
	}
	TestTrue(TEXT("Each ribbon's triangles are its own"), bApart);

	// A ribbon of one point draws nothing; nothing at all is an empty segment, which keeps its buffers.
	TArray<FDreamNiagaraRibbonParticle> Lonely;
	Lonely.AddDefaulted();
	Out.Reset();
	FDreamNiagaraMeshBuilder::BuildRibbons(Lonely, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	TestEqual(TEXT("An empty segment is one triangle"), Out.Target.NumIndices, 3);
	TestFalse(TEXT("...that covers nothing"), Out.Target.Bounds.IsValid != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraRibbonTessellationTest,
	"DreamGUI.Niagara.ARibbonsSegmentsAreCutAlongACurveThroughItsPoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraRibbonTessellationTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMeshTestLocal;
	// A right angle: along the bottom, then up.
	const TArray<FVector2f> Points = { FVector2f(0.0f, 0.0f), FVector2f(10.0f, 0.0f), FVector2f(10.0f, 10.0f), FVector2f(10.0f, 20.0f) };
	for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
	{
		TestTrue(*FString::Printf(TEXT("The curve starts on point %d"), Index), Near(FDreamNiagaraMeshBuilder::CurvePoint(Points, Index, 0.0f, 0.0f), Points[Index]));
		TestTrue(*FString::Printf(TEXT("...and ends on point %d"), Index + 1), Near(FDreamNiagaraMeshBuilder::CurvePoint(Points, Index, 1.0f, 0.0f), Points[Index + 1]));
	}
	const FVector2f Bowed = FDreamNiagaraMeshBuilder::CurvePoint(Points, 1, 0.5f, 0.0f);
	TestTrue(*FString::Printf(TEXT("Round the corner the curve bows out %s"), *Describe(Bowed)), Bowed.X > 10.0f);
	const FVector2f Taut = FDreamNiagaraMeshBuilder::CurvePoint(Points, 1, 0.5f, 1.0f);
	TestTrue(*FString::Printf(TEXT("At full tension it is the straight segment %s"), *Describe(Taut)), Near(Taut, FVector2f(10.0f, 5.0f)));

	FDreamNiagaraRibbonSettings Settings;
	Settings.MaxTessellation = 8;
	Settings.TessellationAngle = 15.0f;
	TestEqual(TEXT("A straight run is not cut"), FDreamNiagaraMeshBuilder::TessellationOf(Points, 2, Settings), 1);
	TestEqual(TEXT("A segment beside the right angle is cut by its bend: 90 / 15"), FDreamNiagaraMeshBuilder::TessellationOf(Points, 0, Settings), 6);
	Settings.bConstantTessellation = true;
	TestEqual(TEXT("A constant factor cuts every segment"), FDreamNiagaraMeshBuilder::TessellationOf(Points, 2, Settings), 8);
	Settings.MaxTessellation = 1;
	TestEqual(TEXT("A factor of 1 cuts nothing"), FDreamNiagaraMeshBuilder::TessellationOf(Points, 0, Settings), 1);

	// Built: three segments cut in four make 13 points, 26 vertices, 12 pieces.
	TArray<FDreamNiagaraRibbonParticle> Particles;
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		FDreamNiagaraRibbonParticle& Point = Particles.AddDefaulted_GetRef();
		Point.Position = FVector3f(Points[Index].X, 0.0f, Points[Index].Y);
		Point.LinkOrder = static_cast<float>(Index);
	}
	Settings.MaxTessellation = 4;
	Settings.bConstantTessellation = true;
	Settings.UV0.Distribution = EDreamNiagaraRibbonUVDistribution::TiledByLength;
	Settings.UV0.TilingLength = 10.0f;
	FTarget Out;
	FDreamNiagaraMeshBuilder::BuildRibbons(Particles, Settings, XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	TestEqual(TEXT("Thirteen points, two vertices each"), Out.Target.NumVertices, 26);
	TestEqual(TEXT("Twelve pieces"), Out.Target.NumIndices, 72);
	TestTrue(*FString::Printf(TEXT("Tiled once every 10 units: 3 at the end (%.3f)"), Out.Vertices[24].TextureCoordinate[0].X), Near(Out.Vertices[24].TextureCoordinate[0].X, 3.0f));
	TestTrue(*FString::Printf(TEXT("...and 1 at the second point, ten units in (%.3f)"), Out.Vertices[8].TextureCoordinate[0].X), Near(Out.Vertices[8].TextureCoordinate[0].X, 1.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraCapacityTest,
	"DreamGUI.Niagara.ASegmentsArraysGrowByTiersAndAreWrittenInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraCapacityTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraMeshTestLocal;
	TestEqual(TEXT("64 at the least"), FDreamNiagaraMeshBuilder::GrowCapacity(1), 64);
	TestEqual(TEXT("Then powers of two"), FDreamNiagaraMeshBuilder::GrowCapacity(65), 128);
	TestEqual(TEXT("...exactly"), FDreamNiagaraMeshBuilder::GrowCapacity(1024), 1024);

	TArray<FDreamNiagaraSpriteParticle> Particles;
	// Sixteen first: 64 vertices and 96 indices, the tiers every count below fits.
	Particles.SetNum(16);
	FTarget Out;
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, FDreamNiagaraSpriteSettings(), XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	TestTrue(TEXT("The first build grows"), Out.Target.bGrew);
	const FDreamUIMeshVertex* const Before = Out.Vertices.GetData();
	for (int32 Count : { 12, 16, 3, 0, 16 })
	{
		Particles.SetNum(Count);
		Out.Reset();
		FDreamNiagaraMeshBuilder::BuildSprites(Particles, FDreamNiagaraSpriteSettings(), XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
		TestFalse(*FString::Printf(TEXT("%d sprites fit the tier"), Count), Out.Target.bGrew);
		TestEqual(*FString::Printf(TEXT("%d sprites: the vertices drawn"), Count), Out.Target.NumVertices, Count == 0 ? 3 : Count * 4);
	}
	TestTrue(TEXT("Written where they were"), Out.Vertices.GetData() == Before);
	Particles.SetNum(17);
	Out.Reset();
	FDreamNiagaraMeshBuilder::BuildSprites(Particles, FDreamNiagaraSpriteSettings(), XZSpace(), FDreamNiagaraVertexCommon(), Out.Target);
	TestTrue(TEXT("Past the tier, it grows"), Out.Target.bGrew);
	TestEqual(TEXT("...to the next"), Out.Vertices.Num(), 128);

	// Enough sprites to be built across worker threads: the same vertices as one thread writes.
	TArray<FDreamNiagaraSpriteParticle> Many;
	Many.SetNum(2000);
	for (int32 Index = 0; Index < Many.Num(); ++Index)
	{
		Many[Index].Position = FVector3f(static_cast<float>(Index % 50), 0.0f, static_cast<float>(Index / 50));
		Many[Index].Rotation = static_cast<float>(Index);
	}
	FTarget Parallel;
	FDreamNiagaraMeshBuilder::BuildSprites(Many, FDreamNiagaraSpriteSettings(), XZSpace(), FDreamNiagaraVertexCommon(), Parallel.Target);
	bool bSame = Parallel.Target.NumVertices == 8000;
	for (int32 Index = 0; Index < Many.Num() && bSame; Index += 97)
	{
		FTarget One;
		FDreamNiagaraMeshBuilder::BuildSprites(TConstArrayView<FDreamNiagaraSpriteParticle>(&Many[Index], 1), FDreamNiagaraSpriteSettings(), XZSpace(), FDreamNiagaraVertexCommon(), One.Target);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			bSame &= Near(Parallel.Vertices[Index * 4 + Corner].Position, One.Vertices[Corner].Position);
		}
	}
	TestTrue(TEXT("Built in parallel, every sprite where it is built alone"), bSame);
	return true;
}

#endif
