// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUITextData.h"
#include "DreamTweenManager.h"
#include "DreamTweener.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Event/DreamPointerEventData.h"
#include "Extensions/2DLineRenderer/Dream2DLineChildrenAsPoints.h"
#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Extensions/DreamRetainerBox.h"
#include "Extensions/DreamStaticMesh.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "Extensions/Effects/DreamPixelSort.h"
#include "Extensions/Lyrics/DreamLyricsData.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithEase.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithWave.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "DreamScopedWorld.h"

/*
 * The DreamGUIExtensions audit's crashes and broken behaviours, each pinned where it happened.
 *
 * Most of these were states the module's own code could reach and nothing exercised: a glyph with no vertices yet
 * under a text animation, a render-target surface whose canvas lost its target, a static mesh handed a new mesh after
 * its widget left the canvas, a children-as-points line whose child count changed, a retainer box on a canvas that was
 * not already in RenderTarget mode, an interaction still sending input to the canvas its surface stopped showing.
 * The headless rig reaches all of them: a game world with a tween manager and a screen-space root canvas, and
 * render-target canvases on surfaces in that world (DreamDriverWorld::MakeRenderTargetMesh).
 */
namespace DreamExtensionStabilityTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	/** One texel per canvas unit per centimetre of surface. */
	const FIntPoint TargetSize(400, 300);
	constexpr int32 VertsPerGlyph = 4;

	/** InCount glyph quads ten units square and twenty apart, in the UI plane (Y across, Z up), all white. */
	void BuildGlyphs(FDreamUIGeometry& OutGeometry, int32 InCount)
	{
		OutGeometry.Clear();
		for (int32 Glyph = 0; Glyph < InCount; ++Glyph)
		{
			const float Left = Glyph * 20.0f;
			const FVector3f Corners[VertsPerGlyph] = {
				FVector3f(0.0f, Left, 0.0f),
				FVector3f(0.0f, Left + 10.0f, 0.0f),
				FVector3f(0.0f, Left, 10.0f),
				FVector3f(0.0f, Left + 10.0f, 10.0f),
			};
			for (const FVector3f& Corner : Corners)
			{
				OutGeometry.OriginVertices.Add(FDreamUIOriginVertexData(Corner));
				OutGeometry.Vertices.Add(FDreamUIMeshVertex(Corner, FColor::White));
			}
		}
	}

	void AddChar(TArray<FDreamUITextCharProperty>& OutChars, int32 InStartVertIndex, int32 InVertCount)
	{
		FDreamUITextCharProperty Char;
		Char.CharIndex = OutChars.Num();
		Char.StartVertIndex = InStartVertIndex;
		Char.VertCount = InVertCount;
		OutChars.Add(Char);
	}

	/** The tweens InOuter has running: a tween is outered to the object it was started for, and a killed one is over. */
	TArray<UDreamTweener*> LiveTweensOutered(UObject* InOuter)
	{
		TArray<UObject*> Inner;
		GetObjectsWithOuter(InOuter, Inner, EGetObjectsFlags::None);
		TArray<UDreamTweener*> Tweens;
		for (UObject* Object : Inner)
		{
			UDreamTweener* Tween = Cast<UDreamTweener>(Object);
			if (IsValid(Tween) && !Tween->IsMarkedToKill())
			{
				Tweens.Add(Tween);
			}
		}
		return Tweens;
	}

	/**
	 * Mesh data for a static mesh visual: a fan of InCorners corners on a circle of InRadius in the UI plane, written
	 * into the cache's private arrays the way the editor's import fills them.
	 */
	UDreamUIStaticMeshCacheData* MakeFanMesh(int32 InCorners, float InRadius)
	{
		UDreamUIStaticMeshCacheData* Cache = NewObject<UDreamUIStaticMeshCacheData>(GetTransientPackage(), NAME_None, RF_Transient);
		const UClass* Class = UDreamUIStaticMeshCacheData::StaticClass();
		FArrayProperty* VertexProperty = FindFProperty<FArrayProperty>(Class, TEXT("VertexData"));
		FArrayProperty* IndexProperty = FindFProperty<FArrayProperty>(Class, TEXT("IndexData"));
		if (VertexProperty == nullptr || IndexProperty == nullptr || InCorners < 3)
		{
			return nullptr;
		}
		TArray<FDreamUIStaticMeshVertex>& Vertices = *VertexProperty->ContainerPtrToValuePtr<TArray<FDreamUIStaticMeshVertex>>(Cache);
		TArray<uint32>& Indices = *IndexProperty->ContainerPtrToValuePtr<TArray<uint32>>(Cache);
		for (int32 Corner = 0; Corner < InCorners; ++Corner)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * Corner / InCorners;
			const FVector Position(0.0, InRadius * FMath::Cos(Angle), InRadius * FMath::Sin(Angle));
			const FVector2D UV(0.5 + 0.5 * FMath::Cos(Angle), 0.5 - 0.5 * FMath::Sin(Angle));
			Vertices.Add(FDreamUIStaticMeshVertex(Position, FVector::RightVector, FVector::ForwardVector, FColor::White,
				UV, FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector));
		}
		for (int32 Corner = 1; Corner + 1 < InCorners; ++Corner)
		{
			Indices.Add(0u);
			Indices.Add(static_cast<uint32>(Corner));
			Indices.Add(static_cast<uint32>(Corner + 1));
		}
		return Cache;
	}

	/**
	 * InFrames whole frames: the rig's frame, then the submission that ends an engine frame. The submission is where a
	 * canvas takes the draw calls its batching made and hands its mesh's sections out -- a direct mesh is given its own
	 * there -- and the rig's pump stops short of it: it runs the UI manager's tick, not the world's end-of-frame updates.
	 */
	void DrawFrames(FDreamDriverRig& InRig, int32 InFrames)
	{
		UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InRig.GetWorld());
		for (int32 Frame = 0; Frame < InFrames; ++Frame)
		{
			InRig.PumpFrames(1);
			if (Manager != nullptr)
			{
				Manager->SubmitCanvasDrawCall();
			}
		}
	}

	/**
	 * The section a static mesh visual holds, read through its protected members: a pointer to a member formed in a
	 * class derived from the visual's, where the members are accessible, applied to the visual. Nothing is ever
	 * constructed from this class.
	 */
	struct FStaticMeshSectionAccess : public UDreamStaticMesh
	{
		static bool HoldsSection(const UDreamStaticMesh* InVisual)
		{
			const auto HeldMeshMember = &FStaticMeshSectionAccess::Mesh;
			const auto HeldSectionMember = &FStaticMeshSectionAccess::MeshSection;
			return (InVisual->*HeldMeshMember).IsValid() && (InVisual->*HeldSectionMember).IsValid();
		}
		/** How many vertices the held section was last filled with, or INDEX_NONE when none is held. */
		static int32 SectionVertexCount(const UDreamStaticMesh* InVisual)
		{
			const auto HeldSectionMember = &FStaticMeshSectionAccess::MeshSection;
			const TSharedPtr<FDreamUIRenderSection_DirectMesh> PinnedSection = (InVisual->*HeldSectionMember).Pin();
			return PinnedSection.IsValid() ? PinnedSection->ValidVerticesNum : INDEX_NONE;
		}
	};

	/** A ray straight through a surface at a point of its own plane, from 300 cm in front to 300 cm behind it. */
	void RayThrough(const USceneComponent* InSurface, double InLocalY, double InLocalZ, FVector& OutStart, FVector& OutEnd)
	{
		const FTransform& SurfaceTransform = InSurface->GetComponentTransform();
		OutStart = SurfaceTransform.TransformPosition(FVector(-300.0, InLocalY, InLocalZ));
		OutEnd = SurfaceTransform.TransformPosition(FVector(300.0, InLocalY, InLocalZ));
	}

	/** The world hit a world pointer gets on a surface's middle: the world raycaster reports no face. */
	FDreamUIHitResultContainer HitOnMiddleOf(const USceneComponent* InSurface)
	{
		FDreamUIHitResultContainer Hit;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		RayThrough(InSurface, 0.0, 0.0, Start, End);
		Hit.RayOrigin = Start;
		Hit.RayDirection = (End - Start).GetSafeNormal();
		Hit.RayEnd = End;
		Hit.HitResult.FaceIndex = INDEX_NONE;
		Hit.HitResult.Location = InSurface->GetComponentLocation();
		Hit.HitResult.ImpactPoint = Hit.HitResult.Location;
		return Hit;
	}

	/** A TTML document whose one line sits under InDivDepth nested divs. */
	FString NestedTTML(int32 InDivDepth)
	{
		FString Xml;
		Xml.Reserve(InDivDepth * 11 + 160);
		Xml += TEXT("<tt xmlns=\"http://www.w3.org/ns/ttml\"><body>");
		for (int32 Level = 0; Level < InDivDepth; ++Level)
		{
			Xml += TEXT("<div>");
		}
		Xml += TEXT("<p begin=\"00:01.000\" end=\"00:02.000\">deep</p>");
		for (int32 Level = 0; Level < InDivDepth; ++Level)
		{
			Xml += TEXT("</div>");
		}
		Xml += TEXT("</body></tt>");
		return Xml;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityTextAnimationEmptyCharTest,
	"DreamGUI.MeshModifier.TextAnimation.ACharacterWithNoVerticesIsPassedOverByEveryProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A glyph still on its way from the rasterizer is counted as painted and given no vertices: its char property has a
 * VertCount of 0 and, as the last character, a StartVertIndex of the vertex count -- one past the end. The rotation and
 * scale properties averaged each character's centre starting from its first vertex, and read past the end of the
 * array there. Every built-in property runs here over two glyphs followed by such a character and by one whose range
 * runs off the end: the glyphs with vertices are still animated, the other two are passed over, and nothing outside
 * the geometry is read (an out-of-range read stops the run).
 */
bool FDreamStabilityTextAnimationEmptyCharTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;

	TArray<FDreamUITextCharProperty> Chars;
	AddChar(Chars, 0, VertsPerGlyph);
	AddChar(Chars, VertsPerGlyph, VertsPerGlyph);
	AddChar(Chars, 2 * VertsPerGlyph, 0);
	AddChar(Chars, VertsPerGlyph + 2, VertsPerGlyph);
	const int32 VertexCount = 2 * VertsPerGlyph;

	FDreamMeshModifierTextAnimation_SelectResult Selection;
	Selection.StartCharIndex = 0;
	Selection.EndCharCount = Chars.Num();
	Selection.LerpValueArray.Init(1.0f, Chars.Num());

	UObject* Outer = GetTransientPackage();
	UDreamMeshModifierTextAnimation_PositionProperty* Position = NewObject<UDreamMeshModifierTextAnimation_PositionProperty>(Outer);
	Position->SetPosition(FVector(0.0, 5.0, 5.0));
	UDreamMeshModifierTextAnimation_RotationProperty* Rotation = NewObject<UDreamMeshModifierTextAnimation_RotationProperty>(Outer);
	Rotation->SetRotator(FRotator(0.0, 0.0, 90.0));
	UDreamMeshModifierTextAnimation_ScaleProperty* Scale = NewObject<UDreamMeshModifierTextAnimation_ScaleProperty>(Outer);
	Scale->SetScale(FVector(2.0));
	UDreamMeshModifierTextAnimation_AlphaProperty* Alpha = NewObject<UDreamMeshModifierTextAnimation_AlphaProperty>(Outer);
	Alpha->SetAlpha(0.5f);
	UDreamMeshModifierTextAnimation_PositionWaveProperty* PositionWave = NewObject<UDreamMeshModifierTextAnimation_PositionWaveProperty>(Outer);
	PositionWave->SetPosition(FVector(0.0, 5.0, 5.0));
	UDreamMeshModifierTextAnimation_RotationWaveProperty* RotationWave = NewObject<UDreamMeshModifierTextAnimation_RotationWaveProperty>(Outer);
	RotationWave->SetRotator(FRotator(0.0, 0.0, 90.0));
	UDreamMeshModifierTextAnimation_ScaleWaveProperty* ScaleWave = NewObject<UDreamMeshModifierTextAnimation_ScaleWaveProperty>(Outer);
	ScaleWave->SetScale(FVector(2.0));

	struct FPropertyCase
	{
		const TCHAR* Name;
		UDreamMeshModifierTextAnimation_Property* Property;
		/** Moves the glyphs, rather than colouring them. */
		bool bMoves;
	};
	// The random properties as they come: their default ranges scatter, turn, scale and tint. The waves have no clock
	// here and stand where their cycle starts, where every character but the first is still off its rest pose.
	const FPropertyCase Cases[] =
	{
		{ TEXT("Position"), Position, true },
		{ TEXT("PositionRandom"), NewObject<UDreamMeshModifierTextAnimation_PositionRandomProperty>(Outer), true },
		{ TEXT("Rotation"), Rotation, true },
		{ TEXT("RotationRandom"), NewObject<UDreamMeshModifierTextAnimation_RotationRandomProperty>(Outer), true },
		{ TEXT("Scale"), Scale, true },
		{ TEXT("ScaleRandom"), NewObject<UDreamMeshModifierTextAnimation_ScaleRandomProperty>(Outer), true },
		{ TEXT("Alpha"), Alpha, false },
		{ TEXT("Color"), NewObject<UDreamMeshModifierTextAnimation_ColorProperty>(Outer), false },
		{ TEXT("ColorRandom"), NewObject<UDreamMeshModifierTextAnimation_ColorRandomProperty>(Outer), false },
		{ TEXT("PositionWave"), PositionWave, true },
		{ TEXT("RotationWave"), RotationWave, true },
		{ TEXT("ScaleWave"), ScaleWave, true },
	};

	for (const FPropertyCase& Case : Cases)
	{
		FDreamUIGeometry Geometry;
		BuildGlyphs(Geometry, 2);
		const TArray<FDreamUIOriginVertexData> PositionsBefore = Geometry.OriginVertices;
		const TArray<FDreamUIMeshVertex> VerticesBefore = Geometry.Vertices;

		Case.Property->ApplyPropertyToCharacters(Chars, Selection, &Geometry);

		if (!TestEqual(FString::Printf(TEXT("%s leaves the vertex count alone"), Case.Name), Geometry.OriginVertices.Num(), VertexCount)
			|| !TestEqual(FString::Printf(TEXT("%s leaves the drawn vertex count alone"), Case.Name), Geometry.Vertices.Num(), VertexCount))
		{
			continue;
		}
		bool bAllFinite = true;
		bool bSecondGlyphChanged = false;
		for (int32 VertIndex = 0; VertIndex < VertexCount; ++VertIndex)
		{
			const FVector3f& Moved = Geometry.OriginVertices[VertIndex].Position;
			bAllFinite &= !Moved.ContainsNaN();
			if (VertIndex >= VertsPerGlyph)
			{
				bSecondGlyphChanged |= Case.bMoves
					? !Moved.Equals(PositionsBefore[VertIndex].Position, 0.01f)
					: Geometry.Vertices[VertIndex].Color != VerticesBefore[VertIndex].Color;
			}
		}
		TestTrue(FString::Printf(TEXT("%s leaves every vertex position a number"), Case.Name), bAllFinite);
		TestTrue(FString::Printf(TEXT("%s still animates the glyph that has vertices"), Case.Name), bSecondGlyphChanged);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityTextAnimationSwapTest,
	"DreamGUI.MeshModifier.TextAnimation.AWaveSwappedInAtRunTimeStartsAndTheOneItReplacedStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A wave property is driven by a tween it starts in Init, which the text animation calls on register and pairs with
 * Deinit on unregister. SetProperties and SetProperty did neither: a wave assigned at run time never moved, and the
 * one it replaced kept its tween -- outered to the property, holding it alive, marking the text dirty every frame --
 * for the rest of the run.
 */
bool FDreamStabilityTextAnimationSwapTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	if (!TestNotNull(TEXT("The rig's world has a tween manager"), UDreamTweenManager::GetDreamTweenInstance(Rig.GetWorld())))
	{
		return false;
	}
	UDreamWidget* Label = Rig.MakeWidget(TEXT("Label"), nullptr, FVector2D(200.0, 40.0));
	UDreamText* Text = Label != nullptr ? Label->CreateNewVisual<UDreamText>() : nullptr;
	UDreamMeshModifierTextAnimation* Animation = Text != nullptr
		? Cast<UDreamMeshModifierTextAnimation>(Text->AddMeshModifier(UDreamMeshModifierTextAnimation::StaticClass()))
		: nullptr;
	if (!TestNotNull(TEXT("A text animation on a text"), Animation))
	{
		return false;
	}
	UDreamMeshModifierTextAnimation_PositionWaveProperty* First = NewObject<UDreamMeshModifierTextAnimation_PositionWaveProperty>(Animation);
	UDreamMeshModifierTextAnimation_PositionWaveProperty* Second = NewObject<UDreamMeshModifierTextAnimation_PositionWaveProperty>(Animation);

	Animation->SetProperties(TArray<UDreamMeshModifierTextAnimation_Property*>{ First });
	const TArray<UDreamTweener*> FirstTweens = LiveTweensOutered(First);
	if (!TestEqual(TEXT("A wave assigned at run time is started: it has the tween that moves it"), FirstTweens.Num(), 1))
	{
		return false;
	}

	Animation->SetProperty(0, Second);
	TestTrue(TEXT("The wave it replaced is wound down: its tween is killed"), FirstTweens[0]->IsMarkedToKill());
	TestEqual(TEXT("and the wave that replaced it is started"), LiveTweensOutered(Second).Num(), 1);

	// A property already listed is not started again, however often it is listed.
	Animation->SetProperties(TArray<UDreamMeshModifierTextAnimation_Property*>{ Second, Second });
	TestEqual(TEXT("Listed twice, the wave still runs one tween"), LiveTweensOutered(Second).Num(), 1);

	Animation->SetProperties(TArray<UDreamMeshModifierTextAnimation_Property*>());
	TestEqual(TEXT("Cleared, no wave runs a tween"), LiveTweensOutered(Second).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityCylinderWithoutTargetTest,
	"DreamGUI.Extensions.RenderTargetSurface.ACylinderWhoseCanvasHasNoTargetIsAMissAndKeepsNoBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The world raycaster passes no face index, so on a cylinder the interaction always takes the walk over the cylinder's
 * vertices. Shown a canvas without a target, the surface has no vertices -- and kept the body of the last surface it
 * had, which went on catching world traces and sent them into that walk over an empty array.
 */
bool FDreamStabilityCylinderWithoutTargetTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const DreamDriverWorld::FDreamRenderTargetMesh Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("Screen"), FTransform(FVector(300.0, 0.0, 0.0)), TargetSize);
	if (!TestTrue(TEXT("A render-target canvas and the surface that shows it"), Screen.IsComplete()))
	{
		return false;
	}
	UDreamUIRenderTargetGeometrySource* Surface = Screen.Surface;
	Surface->SetGeometryMode(EDreamUIRenderTargetGeometryMode::Cylinder);

	// Off the middle, which on an even number of segments is a seam between two of them.
	const int32 NoFace = INDEX_NONE;
	FVector RayStart = FVector::ZeroVector;
	FVector RayEnd = FVector::ZeroVector;
	RayThrough(Surface, 37.0, 21.0, RayStart, RayEnd);
	FVector2D HitUV = FVector2D::ZeroVector;
	TestTrue(TEXT("With its target the cylinder is built"), Surface->GetMeshVertices().Num() > 0);
	TestTrue(TEXT("a ray across it lands on it"),
		IDreamUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(Surface, NoFace, Surface->GetComponentLocation(), RayStart, RayEnd, HitUV));
	TestNotNull(TEXT("and it has a body for world traces to find"), Surface->GetBodySetup());

	// The rig's root canvas draws to the screen and has no target.
	Surface->SetCanvas(Rig.RootCanvas());
	TestEqual(TEXT("Shown a canvas with no target, the surface has no mesh"), Surface->GetMeshVertices().Num(), 0);
	TestFalse(TEXT("so a ray that reaches it anyway is a miss"),
		IDreamUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(Surface, NoFace, Surface->GetComponentLocation(), RayStart, RayEnd, HitUV));
	TestNull(TEXT("and the old surface's body is gone, so world traces stop finding it"), Surface->GetBodySetup());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityCylinderArcTest,
	"DreamGUI.Extensions.RenderTargetSurface.ACylinderArcOfZeroOrBelowKeepsItsBoundsFiniteAndTheRightWayOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The arc setter kept the sign of the angle and clamped its size, and the sign of 0 is 0: an arc of 0 stayed 0, and the
 * width and thickness divided by it, so the bounds -- culling, the body -- were NaN. A negative arc bends the surface
 * the other way, and gave a negative thickness, so a box turned inside out.
 */
bool FDreamStabilityCylinderArcTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const DreamDriverWorld::FDreamRenderTargetMesh Screen = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("Screen"), FTransform(FVector(300.0, 0.0, 0.0)), TargetSize);
	if (!TestTrue(TEXT("A render-target canvas and the surface that shows it"), Screen.IsComplete()))
	{
		return false;
	}
	UDreamUIRenderTargetGeometrySource* Surface = Screen.Surface;
	Surface->SetGeometryMode(EDreamUIRenderTargetGeometryMode::Cylinder);

	Surface->SetCylinderArcAngle(0.0f);
	TestEqual(TEXT("An arc of 0 is taken as the smallest arc there is"), Surface->GetCylinderArcAngle(), 1.0f);
	const FBoxSphereBounds Flat = Surface->CalcBounds(FTransform::Identity);
	TestFalse(TEXT("and its bounds are numbers"), Flat.Origin.ContainsNaN() || Flat.BoxExtent.ContainsNaN() || FMath::IsNaN(Flat.SphereRadius));
	TestFalse(TEXT("as the component's are"), Surface->Bounds.Origin.ContainsNaN() || Surface->Bounds.BoxExtent.ContainsNaN());
	TestEqual(TEXT("as wide as the target"), Flat.BoxExtent.Y * 2.0, (double)TargetSize.X, 1.0);

	Surface->SetCylinderArcAngle(-90.0f);
	TestEqual(TEXT("A negative arc keeps its sign"), Surface->GetCylinderArcAngle(), -90.0f);
	const FBoxSphereBounds Bent = Surface->CalcBounds(FTransform::Identity);
	TestTrue(TEXT("and gives a box the right way out"), Bent.BoxExtent.X > 0.0 && Bent.BoxExtent.Y > 0.0 && Bent.BoxExtent.Z > 0.0);
	TestTrue(TEXT("on the side the surface bends to"), Bent.Origin.X < 0.0);
	double MostForward = -UE_BIG_NUMBER;
	for (const FDynamicMeshVertex& Vertex : Surface->GetMeshVertices())
	{
		MostForward = FMath::Max(MostForward, (double)Vertex.Position.X);
	}
	TestTrue(TEXT("which is where its vertices are"), Surface->GetMeshVertices().Num() > 0 && MostForward <= 0.001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityInteractionFollowsCanvasTest,
	"DreamGUI.Interaction.RenderTarget.AHitGoesToTheCanvasTheSurfaceShowsNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The interaction kept the first canvas it found on its surface until that canvas died. Handed another canvas while the
 * first was still alive, the surface showed the new one and every pointer went on to the old one: clicks landed on
 * widgets nobody could see.
 */
bool FDreamStabilityInteractionFollowsCanvasTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const DreamDriverWorld::FDreamRenderTargetMesh ScreenA = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("ScreenA"), FTransform(FVector(300.0, 0.0, 0.0)), TargetSize);
	const DreamDriverWorld::FDreamRenderTargetMesh ScreenB = DreamDriverWorld::MakeRenderTargetMesh(Rig, TEXT("ScreenB"), FTransform(FVector(300.0, 0.0, 1000.0)), TargetSize);
	if (!TestTrue(TEXT("A first render-target surface"), ScreenA.IsComplete()) || !TestTrue(TEXT("and a second"), ScreenB.IsComplete()))
	{
		return false;
	}
	// Twice the canvas each way, so the middle of the canvas is under it wherever it is anchored.
	const FVector2D CoverCanvas(2.0 * TargetSize.X, 2.0 * TargetSize.Y);
	UDreamWidget* OnA = Rig.MakeWidget(TEXT("OnA"), ScreenA.CanvasRoot, CoverCanvas);
	UDreamWidget* OnB = Rig.MakeWidget(TEXT("OnB"), ScreenB.CanvasRoot, CoverCanvas);
	if (!TestNotNull(TEXT("A widget filling the first canvas"), OnA) || !TestNotNull(TEXT("and one filling the second"), OnB))
	{
		return false;
	}
	Rig.PumpFrames(2);

	UDreamPointerEventData* Pointer = NewObject<UDreamPointerEventData>(GetTransientPackage());
	FDreamUIHitResultContainer Inner;
	TestTrue(TEXT("A ray on the first surface reaches a canvas"), ScreenA.Interaction->ResolveNestedHit(HitOnMiddleOf(ScreenA.Surface), Pointer, Inner));
	TestTrue(TEXT("the one it shows"), Inner.HitResult.Widget.Get() == OnA);

	// The first canvas stays alive; the surface shows the second now.
	ScreenA.Surface->SetCanvas(ScreenB.Canvas);
	FDreamUIHitResultContainer InnerAfter;
	TestTrue(TEXT("Shown another canvas, the surface still answers"), ScreenA.Interaction->ResolveNestedHit(HitOnMiddleOf(ScreenA.Surface), Pointer, InnerAfter));
	TestTrue(TEXT("with a widget of the canvas it shows now"), InnerAfter.HitResult.Widget.Get() == OnB);
	TestTrue(TEXT("whose canvas is the one the interaction reports"), ScreenA.Interaction->GetSurfaceCanvas() == ScreenB.Canvas);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityStaticMeshLeftCanvasTest,
	"DreamGUI.Extensions.StaticMesh.AMeshSetAfterItsWidgetLeftTheCanvasWaitsForTheNextCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A static mesh draws into a section its canvas's mesh hands it, and kept that section -- valid, pooled, still tagged
 * to it -- after its widget left the canvas. SetMesh then wrote into it, through the render canvas the widget no longer
 * had. The section is the canvas's: leaving the canvas lets go of it, and the next canvas hands over one of its own.
 */
bool FDreamStabilityStaticMeshLeftCanvasTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Host = Rig.MakeWidget(TEXT("MeshHost"), nullptr, FVector2D(100.0, 100.0), FVector2D(100.0, 50.0));
	UDreamStaticMesh* StaticMesh = Host != nullptr ? Host->CreateNewVisual<UDreamStaticMesh>() : nullptr;
	UDreamUIStaticMeshCacheData* Square = MakeFanMesh(4, 50.0f);
	UDreamUIStaticMeshCacheData* Triangle = MakeFanMesh(3, 50.0f);
	if (!TestNotNull(TEXT("A static mesh visual"), StaticMesh) || !TestNotNull(TEXT("a square for it"), Square) || !TestNotNull(TEXT("and a triangle"), Triangle))
	{
		return false;
	}
	const TStrongObjectPtr<UDreamUIStaticMeshCacheData> KeepSquare(Square);
	const TStrongObjectPtr<UDreamUIStaticMeshCacheData> KeepTriangle(Triangle);
	StaticMesh->SetReplaceMaterial(UMaterial::GetDefaultMaterial(MD_Surface));
	StaticMesh->SetMesh(Square);
	DrawFrames(Rig, 3);
	if (!TestTrue(TEXT("Drawn, the mesh holds a section of its canvas's mesh"), FStaticMeshSectionAccess::HoldsSection(StaticMesh)))
	{
		return false;
	}
	TestEqual(TEXT("filled with the square"), FStaticMeshSectionAccess::SectionVertexCount(StaticMesh), 4);

	Host->SetParent(nullptr);
	TestFalse(TEXT("Leaving the canvas, it lets go of that section"), FStaticMeshSectionAccess::HoldsSection(StaticMesh));
	StaticMesh->SetMesh(Triangle);
	TestTrue(TEXT("A mesh set with no canvas is kept for later"), StaticMesh->GetMeshCache() == Triangle);

	Host->SetParent(Rig.Root());
	DrawFrames(Rig, 3);
	TestTrue(TEXT("Back on a canvas, it is handed a section of that canvas's mesh"), FStaticMeshSectionAccess::HoldsSection(StaticMesh));
	TestEqual(TEXT("and draws the mesh it was given meanwhile"), FStaticMeshSectionAccess::SectionVertexCount(StaticMesh), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityLineChildCountTest,
	"DreamGUI.Extensions.Line2D.AChildrenAsPointsLineRebuildsItsStripWhenThePointCountChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A child added to or taken from a children-as-points line moves only its points' positions as far as the line is told,
 * so the triangles, UVs and colours were kept from the strip before. Closed (ConnectStartAndEnd) and one point shorter,
 * the strip kept triangles naming the two vertices it no longer had, which a mesh raycast then read past the end of;
 * one point longer, the new vertices drew with no colour. A new point count is a new strip, written whole.
 */
bool FDreamStabilityLineChildCountTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* LineWidget = Rig.MakeWidget(TEXT("Line"), nullptr, FVector2D(400.0, 200.0));
	if (!TestNotNull(TEXT("A widget for the line"), LineWidget))
	{
		return false;
	}
	const FVector2D Corners[] = { FVector2D(-150.0, -75.0), FVector2D(150.0, -75.0), FVector2D(150.0, 75.0), FVector2D(-150.0, 75.0) };
	TArray<UDreamWidget*> Points;
	for (const FVector2D& Corner : Corners)
	{
		Points.Add(Rig.MakeWidget(FString::Printf(TEXT("Point%d"), Points.Num()), LineWidget, FVector2D(10.0, 10.0), Corner));
	}
	UDream2DLineChildrenAsPoints* Line = LineWidget->CreateNewVisual<UDream2DLineChildrenAsPoints>();
	if (!TestNotNull(TEXT("A children-as-points line"), Line) || !TestFalse(TEXT("over four points"), Points.Contains(nullptr)))
	{
		return false;
	}
	Line->SetEndType(EDream2DLineRenderer_EndType::ConnectStartAndEnd);
	Rig.PumpFrames(2);

	// Two vertices a point, and a closed strip's six indices a point.
	const auto CheckStrip = [this, Line](int32 InPointCount, const TCHAR* InWhen)
	{
		const FDreamUIGeometry* Geometry = Line->GetGeometry();
		if (!TestNotNull(FString::Printf(TEXT("%s the line has geometry"), InWhen), Geometry))
		{
			return;
		}
		const int32 VertexCount = Geometry->Vertices.Num();
		TestEqual(FString::Printf(TEXT("%s it has two vertices a point"), InWhen), VertexCount, 2 * InPointCount);
		TestEqual(FString::Printf(TEXT("%s and six indices a point"), InWhen), Geometry->Triangles.Num(), 6 * InPointCount);
		bool bIndicesInRange = true;
		for (const FDreamUIMeshIndex Index : Geometry->Triangles)
		{
			bIndicesInRange &= static_cast<int32>(Index) < VertexCount;
		}
		TestTrue(FString::Printf(TEXT("%s every index names one of its vertices"), InWhen), bIndicesInRange);
		bool bAllColoured = true;
		for (const FDreamUIMeshVertex& Vertex : Geometry->Vertices)
		{
			bAllColoured &= Vertex.Color.A > 0;
		}
		TestTrue(FString::Printf(TEXT("%s every vertex is coloured"), InWhen), bAllColoured);
	};
	CheckStrip(4, TEXT("Over four points"));

	// One point goes and another moves. The line hears of the move (its children's moves mark its positions dirty)
	// and of nothing else: the dirty flag a move raises is marked here as the move's hook marks it.
	Points[3]->DestroyWidget();
	Points[3] = nullptr;
	Points[0]->SetAnchoredPosition(FVector2D(-160.0, -75.0));
	Line->MarkVertexPositionDirty();
	Rig.PumpFrames(2);
	CheckStrip(3, TEXT("Over three points"));

	Points.Add(Rig.MakeWidget(TEXT("Point4"), LineWidget, FVector2D(10.0, 10.0), FVector2D(0.0, 90.0)));
	Points.Add(Rig.MakeWidget(TEXT("Point5"), LineWidget, FVector2D(10.0, 10.0), FVector2D(-150.0, 75.0)));
	Line->MarkVertexPositionDirty();
	Rig.PumpFrames(2);
	CheckStrip(5, TEXT("Over five points"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityRetainerBoxTest,
	"DreamGUI.Extensions.RetainerBox.RetainsIntoATextureThePreviewerDrawsAndHandsTheCanvasBackAsItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The retainer forced its canvas to render to a target and left the canvas's render mode alone, and a child canvas
 * defaults to WorldSpace: it reported that as its actual mode, never drew its target, the parent skipped the forced
 * subtree and the previewer had no texture -- the subtree vanished. It now sets the mode with the flag, and puts both
 * back when it goes. With no DisplayVisual it says once that nothing will show the texture.
 */
bool FDreamStabilityRetainerBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Panel = Rig.MakeWidget(TEXT("Retained"), nullptr, FVector2D(200.0, 100.0), FVector2D(-150.0, 0.0));
	UDreamWidget* Content = Panel != nullptr ? Rig.MakeWidget(TEXT("Content"), Panel, FVector2D(100.0, 50.0)) : nullptr;
	UDreamCanvas* PanelCanvas = Panel != nullptr ? Panel->AddComponent<UDreamCanvas>() : nullptr;
	UDreamWidget* Display = Rig.MakeWidget(TEXT("Display"), nullptr, FVector2D(200.0, 100.0), FVector2D(150.0, 0.0));
	UDreamCanvasRenderTargetPreviewer* Previewer = Display != nullptr ? Display->CreateNewVisual<UDreamCanvasRenderTargetPreviewer>() : nullptr;
	if (!TestNotNull(TEXT("Something to retain"), Content) || !TestNotNull(TEXT("on a canvas of its own"), PanelCanvas)
		|| !TestNotNull(TEXT("and a previewer beside it"), Previewer))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const bool bForcedBefore = PanelCanvas->GetForceRenderToTarget();
	const EDreamRenderMode ModeBefore = PanelCanvas->GetRenderMode();
	const EDreamCanvasRenderTargetUpdateMode UpdateModeBefore = PanelCanvas->GetRenderTargetUpdateMode();

	// Registered with no display: said once, however often the composite is applied again.
	AddExpectedMessagePlain(TEXT("has no DisplayVisual"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	UDreamRetainerBox* Retainer = Panel->AddComponent<UDreamRetainerBox>();
	if (!TestNotNull(TEXT("A retainer box on the canvas's widget"), Retainer))
	{
		return false;
	}
	Retainer->SetGroupRenderOpacity(0.9f);
	FProperty* DisplayProperty = UDreamRetainerBox::StaticClass()->FindPropertyByName(TEXT("DisplayVisual"));
	if (!TestNotNull(TEXT("The retainer box still has a DisplayVisual property"), DisplayProperty))
	{
		return false;
	}
	*DisplayProperty->ContainerPtrToValuePtr<TWeakObjectPtr<UDreamCanvasRenderTargetPreviewer>>(Retainer) = Previewer;
	// A change to the composite applies all of it, the display included.
	Retainer->SetGroupRenderOpacity(0.5f);
	Rig.PumpFrames(4);

	TestTrue(TEXT("The retained canvas renders to its own target"), PanelCanvas->GetForceRenderToTarget());
	TestEqual(TEXT("and reports RenderTarget as its actual mode"), (int32)PanelCanvas->GetActualRenderMode(), (int32)EDreamRenderMode::RenderTarget);
	UTextureRenderTarget2D* Target = PanelCanvas->GetRenderTarget();
	TestNotNull(TEXT("It made itself a target to draw into"), Target);
	TestTrue(TEXT("The previewer shows the retained canvas"), Previewer->GetPreviewCanvas() == PanelCanvas);
	const FDreamUIGeometry* PreviewGeometry = Previewer->GetGeometry();
	TestTrue(TEXT("and draws its target"), PreviewGeometry != nullptr && Target != nullptr && PreviewGeometry->Texture.Get() == Target);

	Retainer->DestroyComponent();
	TestTrue(TEXT("Taken off, the retainer hands the canvas back as forced as it was"), PanelCanvas->GetForceRenderToTarget() == bForcedBefore);
	TestEqual(TEXT("in the render mode it had"), (int32)PanelCanvas->GetRenderMode(), (int32)ModeBefore);
	TestEqual(TEXT("updating as it did"), (int32)PanelCanvas->GetRenderTargetUpdateMode(), (int32)UpdateModeBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityRetainerBoxEditorWorldTest,
	"DreamGUI.Extensions.RetainerBox.LeavesItsCanvasAloneInAnEditorWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Behaviours register in editor worlds too, and what the retainer writes are the canvas's saved properties: an asset
 * saved while it had them set kept the forced state as the canvas's own. Only a game world is retained.
 */
bool FDreamStabilityRetainerBoxEditorWorldTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld EditorWorld(EWorldType::Editor);
	if (!TestNotNull(TEXT("An editor world"), EditorWorld.World))
	{
		return false;
	}
	UDreamWidget* Root = NewObject<UDreamWidget>(EditorWorld.World, NAME_None, RF_Public | RF_Transactional);
	Root->SetDisplayName(TEXT("Root"));
	Root->SetWidth(400.0f);
	Root->SetHeight(300.0f);
	Root->OnRegister();
	Root->AddComponent<UDreamCanvas>();
	UDreamWidget* Panel = NewObject<UDreamWidget>(EditorWorld.World, NAME_None, RF_Public | RF_Transactional);
	Panel->SetDisplayName(TEXT("Panel"));
	Panel->SetWidth(200.0f);
	Panel->SetHeight(100.0f);
	Panel->OnRegister();
	Panel->TrySetParent(Root, false);
	UDreamCanvas* PanelCanvas = Panel->AddComponent<UDreamCanvas>();
	if (!TestNotNull(TEXT("A canvas on the panel"), PanelCanvas))
	{
		Root->DestroyWidget();
		return false;
	}
	const bool bForcedBefore = PanelCanvas->GetForceRenderToTarget();
	const EDreamRenderMode ModeBefore = PanelCanvas->GetRenderMode();
	const EDreamCanvasRenderTargetUpdateMode UpdateModeBefore = PanelCanvas->GetRenderTargetUpdateMode();

	TestNotNull(TEXT("A retainer box on the panel"), Panel->AddComponent<UDreamRetainerBox>());
	TestTrue(TEXT("The canvas is not forced to a target"), PanelCanvas->GetForceRenderToTarget() == bForcedBefore);
	TestEqual(TEXT("its render mode is its own"), (int32)PanelCanvas->GetRenderMode(), (int32)ModeBefore);
	TestEqual(TEXT("and so is its update mode"), (int32)PanelCanvas->GetRenderTargetUpdateMode(), (int32)UpdateModeBefore);
	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityLyricsDepthTest,
	"DreamGUI.Lyrics.TTMLNestedPastTheDepthLimitIsAnErrorNotAStackOverflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The TTML reader descends one call per element, and so does every walk over what it read. Lyrics come from files and
 * downloads, and a document nested a few hundred thousand deep ran the stack out. Elements nest at most 256 deep;
 * past that the document is refused with an error.
 */
bool FDreamStabilityLyricsDepthTest::RunTest(const FString& Parameters)
{
	using namespace DreamExtensionStabilityTestLocal;
	FDreamLyrics Lyrics;
	FString Error;
	TestTrue(TEXT("A line two hundred divs deep is read"), UDreamLyricsLibrary::ParseTTML(NestedTTML(200), Lyrics, Error));
	TestTrue(TEXT("as the one line it is"), Lyrics.Lines.Num() == 1 && Lyrics.Lines[0].GetText() == TEXT("deep"));

	TestFalse(TEXT("Three hundred deep is refused"), UDreamLyricsLibrary::ParseTTML(NestedTTML(300), Lyrics, Error));
	TestTrue(TEXT("with an error that says why"), Error.Contains(TEXT("nested deeper than 256")));
	TestEqual(TEXT("and no lines"), Lyrics.Lines.Num(), 0);

	TestFalse(TEXT("A hundred thousand deep is refused the same way"), UDreamLyricsLibrary::ParseTTML(NestedTTML(100000), Lyrics, Error));
	TestTrue(TEXT("with the same error"), Error.Contains(TEXT("nested deeper than 256")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStabilityPixelSortPassesTest,
	"DreamGUI.Extensions.PixelSortKeepsItsPassCountInTheRangeItsPropertyDeclares",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * MaxSortPasses is declared 1 to 512 and the details panel keeps to that; the setter did not.
 */
bool FDreamStabilityPixelSortPassesTest::RunTest(const FString& Parameters)
{
	UDreamPixelSort* Sort = NewObject<UDreamPixelSort>(GetTransientPackage());
	Sort->SetMaxSortPasses(64);
	TestEqual(TEXT("A count in range is taken as given"), Sort->GetMaxSortPasses(), 64);
	Sort->SetMaxSortPasses(0);
	TestEqual(TEXT("None is at least one"), Sort->GetMaxSortPasses(), 1);
	Sort->SetMaxSortPasses(100000);
	TestEqual(TEXT("and a great many is the most there may be"), Sort->GetMaxSortPasses(), 512);
	return true;
}

#endif
