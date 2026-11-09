// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/MetaData.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "DreamNiagaraEngineAccess.h"
#include "DreamNiagaraSystem.h"
#include "DreamNiagaraVertexLayout.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A Niagara system on a widget: its renderers become the widget's segments, its particles are read after each step and placed
 * where the widget is, it is told what DreamGUI knows (the widget's size, the pointer), its bursts come and go, and its
 * simulation lives exactly as long as the widget is registered. The system is the plugin's test system
 * (DFX/Tests/NS_DreamUITest_Particles.dfs), whose particles are all placed by their index; its time only moves when a test
 * says so (a fixed step, and Advance), so every number below is the same on every run.
 */
namespace DreamNiagaraSystemTestLocal
{
	const TCHAR* const TestSystemPath = TEXT("/DreamGUI/Tests/Niagara/NS_DreamUITest_Particles.NS_DreamUITest_Particles");
	const TCHAR* const UnsupportedSystemPath = TEXT("/DreamGUI/Tests/Niagara/NS_DreamUITest_Unsupported.NS_DreamUITest_Unsupported");
	constexpr float Step = 1.0f / 30.0f;

	UNiagaraSystem* LoadSystem(FAutomationTestBase& InTest, const TCHAR* InPath, bool bInMustRun = true)
	{
		UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, InPath);
		if (!InTest.TestNotNull(*FString::Printf(TEXT("%s loads"), InPath), System))
		{
			return nullptr;
		}
		// Compiled where it was made, and again here when this editor's keys say its scripts are not the ones it has.
		System->WaitForCompilationComplete();
		if (bInMustRun && !DreamNiagaraEngine::IsReadyToRun(System))
		{
			System->RequestCompile(false);
			System->WaitForCompilationComplete();
		}
		if (bInMustRun)
		{
			InTest.TestTrue(*FString::Printf(TEXT("The system is ready to run (valid: %d)"), System->IsValid() ? 1 : 0), DreamNiagaraEngine::IsReadyToRun(System));
		}
		return System;
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

	/** A game world, a screen-space canvas, and a widget on it at (100, 50) drawing the test system, whose time steps are fixed. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;
		UDreamWidget* Holder = nullptr;
		UDreamNiagaraSystem* Visual = nullptr;

		FStage(FAutomationTestBase& InTest, const TCHAR* InSystemPath = TestSystemPath)
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(World.World, nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas != nullptr)
			{
				Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
				// No viewport here, so a screen-space canvas has no rect to speak of: nothing is culled by it.
				Canvas->SetCullElementsOutsideCanvas(false);
			}
			// A simulation runs only where the application can render (FApp::CanEverRender): the tests that read particles are
			// the Rhi preset's, and ask for it; the rest need the system described, not run.
			UNiagaraSystem* System = LoadSystem(InTest, InSystemPath, FApp::CanEverRender());
			Holder = MakeWidget(World.World, Root, TEXT("Particles"), FVector2D(200.0, 160.0), FVector2D(100.0, 50.0));
			Visual = Holder->CreateNewVisual<UDreamNiagaraSystem>();
			if (Visual != nullptr)
			{
				// A step the test takes, never the frame's: the world here does not tick.
				SetFixedStep(Step);
				Visual->SetSystem(System);
			}
		}
		bool IsUsable() const { return Manager != nullptr && Canvas != nullptr && Visual != nullptr && Visual->GetSystem() != nullptr && Visual->GetNiagaraComponent() != nullptr; }
		void SetFixedStep(float InStep)
		{
			FProperty* Property = UDreamNiagaraSystem::StaticClass()->FindPropertyByName(TEXT("FixedTimeStep"));
			if (FFloatProperty* Float = CastField<FFloatProperty>(Property))
			{
				Float->SetPropertyValue_InContainer(Visual, InStep);
			}
		}
		void Frames(int32 InCount = 3) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(Step);
				Manager->SubmitCanvasDrawCall();
			}
		}
		TArray<const FDreamUIDrawCall*> Segments() const
		{
			TArray<const FDreamUIDrawCall*> Out;
			for (const FDreamUIDrawCall& DrawCall : Canvas->GetCurrentDrawCalls())
			{
				if (DrawCall.Type == EDreamUIDrawCallType::DirectMesh && DrawCall.DirectMeshVisualObject.Get() == Visual)
				{
					Out.Add(&DrawCall);
				}
			}
			return Out;
		}
		TSharedPtr<FDreamUIRenderSection_DirectMesh> Section(int32 InSegment) const
		{
			return Visual->GetMeshSegment(InSegment);
		}
	};

	/** The centre of sprite InSprite of a segment's section, on the canvas: its four corners' average. */
	FVector2D SpriteCentre(const FDreamUIRenderSection_DirectMesh& InSection, int32 InSprite)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector3f& Position = InSection.Vertices[InSprite * 4 + Corner].Position;
			Sum += FVector2D(Position.Y, Position.Z);
		}
		return Sum / 4.0;
	}

	int32 CountLiveComponents(UWorld* InWorld)
	{
		int32 Count = 0;
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			if (It->GetWorld() == InWorld && It->IsRegistered() && IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraSegmentsTest,
	"DreamGUI.Niagara.EachSpriteAndRibbonRendererIsASegmentOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraSegmentsTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	FStage Stage(*this);
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	Stage.Frames();
	const TArray<FDreamNiagaraRendererInfo>& Renderers = Stage.Visual->GetRendererInfo();
	TestEqual(TEXT("Three renderers"), Renderers.Num(), 3);
	int32 Sprites = 0, Ribbons = 0;
	for (const FDreamNiagaraRendererInfo& Info : Renderers)
	{
		TestTrue(*FString::Printf(TEXT("%s.%d is drawn"), *Info.EmitterName.ToString(), Info.RendererIndex), Info.IsDrawn());
		Sprites += Info.Kind == EDreamNiagaraRendererKind::Sprite ? 1 : 0;
		Ribbons += Info.Kind == EDreamNiagaraRendererKind::Ribbon ? 1 : 0;
		// What the built-in shader takes from the engine's default materials: their texture, and the blend their own blending gives.
		UTexture* Texture = nullptr;
		EDreamUIBlendMode Blend = EDreamUIBlendMode::Alpha;
		DreamNiagaraEngine::InferShading(Info.Material.Get(), Texture, Blend);
		TestTrue(*FString::Printf(TEXT("%s takes its material's texture, or none (the engine's defaults draw procedurally)"), *Info.EmitterName.ToString()), Info.InferredTexture.Get() == Texture);
		TestEqual(*FString::Printf(TEXT("%s blends as its material does"), *Info.EmitterName.ToString()), Info.InferredBlendMode, Info.Material.IsValid() && Info.Material->GetBlendMode() == BLEND_Additive ? EDreamUIBlendMode::Additive : EDreamUIBlendMode::Alpha);
	}
	TestEqual(TEXT("Two sprite renderers"), Sprites, 2);
	TestEqual(TEXT("One ribbon renderer"), Ribbons, 1);
	TestTrue(TEXT("Grid's renderer reads a 2 by 2 sheet, blended"), Renderers[0].Sprite.SubImageSize == FIntPoint(2, 2) && Renderers[0].Sprite.bSubImageBlend);
	TestTrue(TEXT("Trail's renderer cuts every segment in four"), Renderers[1].Ribbon.MaxTessellation == 4 && Renderers[1].Ribbon.bConstantTessellation);
	TestTrue(TEXT("Grid is in the system's local space, Fall in its world"), Renderers[0].bLocalSpace && !Renderers[2].bLocalSpace);

	const TArray<const FDreamUIDrawCall*> Segments = Stage.Segments();
	TestEqual(TEXT("Three draw calls for the one widget"), Segments.Num(), 3);
	for (int32 Index = 0; Index < Segments.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Draw call %d is segment %d"), Index, Index), Segments[Index]->DirectMeshSegment, Index);
		TestTrue(*FString::Printf(TEXT("Segment %d is shaded by the canvas, with the built-in shader"), Index), Segments[Index]->bDirectMeshShaded && Segments[Index]->bDirectMeshBuiltIn);
		TestTrue(*FString::Printf(TEXT("Segment %d has a texture: its material's, or the soft default for a material that has none"), Index), Segments[Index]->Texture.IsValid());
		TestTrue(*FString::Printf(TEXT("Segment %d has a section of its own"), Index), Stage.Section(Index).IsValid() && Stage.Section(Index)->Segment == Index);
	}
	TestEqual(TEXT("The widget record is marked a particle's"), Stage.Visual->GetWidgetPropertyMark(), DreamNiagaraVertexLayout::ParticleMark);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraReadTest,
	"DreamGUI.Niagara.ASystemsParticlesAreReadAfterItsStepAndPlacedOnTheWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraReadTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	FStage Stage(*this);
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	Stage.Frames(1);
	Stage.Visual->Advance(2.0f * Step, Step);
	Stage.Frames(1);
	TestEqual(TEXT("Grid draws its 16 sprites"), Stage.Visual->GetSegmentParticleCount(0), 16);
	TestEqual(TEXT("Trail draws its 8 points"), Stage.Visual->GetSegmentParticleCount(1), 8);
	TestEqual(TEXT("Fall draws its 6 sprites"), Stage.Visual->GetSegmentParticleCount(2), 6);
	TSharedPtr<FDreamUIRenderSection_DirectMesh> Grid = Stage.Section(0);
	TSharedPtr<FDreamUIRenderSection_DirectMesh> Trail = Stage.Section(1);
	if (!TestTrue(TEXT("The sections are there"), Grid.IsValid() && Trail.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Four vertices to a sprite"), Grid->ValidVerticesNum, 64);
	TestEqual(TEXT("Six indices to a sprite"), Grid->ValidTriangleIndicesNum, 96);
	// The widget's centre is at (100, 50) on the canvas; sprite 0 is the grid's bottom left, (-60, -60) from it, on the XZ plane.
	const FVector2D First = SpriteCentre(*Grid, 0);
	TestTrue(*FString::Printf(TEXT("Sprite 0 is at (40, -10) on the canvas (%s)"), *First.ToString()), First.Equals(FVector2D(40.0, -10.0), 0.05));
	const FVector2D Last = SpriteCentre(*Grid, 15);
	TestTrue(*FString::Printf(TEXT("Sprite 15 is at (160, 110) (%s)"), *Last.ToString()), Last.Equals(FVector2D(160.0, 110.0), 0.05));
	// Frame blending: sprite 1 is index 1.5 -- frame 1 blending half way to frame 2.
	float Scale = 0.0f, Blend = 0.0f;
	DreamNiagaraVertexLayout::Unpack(Grid->Vertices[4].TextureCoordinate[3].X, Scale, Blend);
	TestTrue(*FString::Printf(TEXT("Sprite 1 blends half way to the next frame (%.3f)"), Blend), FMath::IsNearlyEqual(Blend, 0.5f, 0.01f));
	TestEqual(TEXT("Every vertex names the widget's record"), Grid->Vertices[10].TextureCoordinate[1].X, static_cast<float>(Stage.Visual->GetWidgetPropertyDataStartPosition()));
	// The ribbon: 8 points cut in four make 29 points, two vertices each.
	TestEqual(TEXT("The ribbon's vertices"), Trail->ValidVerticesNum, 58);
	TestEqual(TEXT("...and its triangles"), Trail->ValidTriangleIndicesNum, 28 * 6);
	// Its box, in the world, holds every sprite and point.
	TestTrue(TEXT("The grid's section is boxed round its sprites"), Grid->BoundingBox.IsValid && Grid->BoundingBox.GetSize().Y >= 140.0);

	// Moved, the widget takes its local-space sprites with it at the next frame.
	Stage.Holder->SetAnchoredPosition(FVector2D(-50.0, 0.0));
	Stage.Frames(2);
	const FVector2D Moved = SpriteCentre(*Stage.Section(0), 0);
	TestTrue(*FString::Printf(TEXT("Sprite 0 follows the widget to (-110, -60) (%s)"), *Moved.ToString()), Moved.Equals(FVector2D(-110.0, -60.0), 0.05));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraSpaceTest,
	"DreamGUI.Niagara.TheSimulationsPlaneIsTheWidgetsOneUnitAPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraSpaceTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	FStage Stage(*this);
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	Stage.Frames(2);
	// XZ: the simulation's X is the canvas's right (Y), its Z the canvas's up (Z), one unit a pixel.
	TestTrue(TEXT("XZ: simulation X is right"), Stage.Visual->SimToCanvas(FVector(10.0, 0.0, 0.0)).Equals(FVector(0.0, 10.0, 0.0)));
	TestTrue(TEXT("XZ: simulation Z is up"), Stage.Visual->SimToCanvas(FVector(0.0, 0.0, 10.0)).Equals(FVector(0.0, 0.0, 10.0)));
	TestTrue(TEXT("XZ: simulation Y goes into the plane"), Stage.Visual->SimToCanvas(FVector(0.0, 10.0, 0.0)).Equals(FVector(-10.0, 0.0, 0.0)));
	TestTrue(TEXT("Back again"), Stage.Visual->CanvasToSim(FVector(0.0, 30.0, -20.0)).Equals(FVector(30.0, 0.0, -20.0)));
	// The component stands where the widget does, in the simulation's axes.
	const FVector At = Stage.Visual->GetNiagaraComponent()->GetComponentLocation();
	TestTrue(*FString::Printf(TEXT("The component is at the widget's centre, (100, 0, 50) in the simulation (%s)"), *At.ToString()), At.Equals(FVector(100.0, 0.0, 50.0), 0.01));

	// XY: Y is up instead, and two pixels to a unit.
	Stage.Visual->SetPlane(EDreamNiagaraPlane::XY);
	Stage.Visual->SetPixelsPerUnit(2.0f);
	TestTrue(TEXT("XY: simulation X is right, two pixels a unit"), Stage.Visual->SimToCanvas(FVector(10.0, 0.0, 0.0)).Equals(FVector(0.0, 20.0, 0.0)));
	TestTrue(TEXT("XY: simulation Y is up"), Stage.Visual->SimToCanvas(FVector(0.0, 10.0, 0.0)).Equals(FVector(0.0, 0.0, 20.0)));
	TestTrue(TEXT("XY: simulation Z comes out of the plane"), Stage.Visual->SimToCanvas(FVector(0.0, 0.0, 10.0)).Equals(FVector(20.0, 0.0, 0.0)));
	Stage.Frames(1);
	const FVector AtXY = Stage.Visual->GetNiagaraComponent()->GetComponentLocation();
	TestTrue(*FString::Printf(TEXT("XY: the component is at (50, 25, 0) in the simulation (%s)"), *AtXY.ToString()), AtXY.Equals(FVector(50.0, 25.0, 0.0), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraParametersTest,
	"DreamGUI.Niagara.ASystemThatDeclaresTheUIParametersIsGivenThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraParametersTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	FStage Stage(*this);
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UNiagaraSystem* System = Stage.Visual->GetSystem();
	TestEqual(TEXT("It declares the widget's size as a Vector2"), DreamNiagaraEngine::FindUserParameter(System, UDreamNiagaraSystem::WidgetSizeParameter), EDreamNiagaraParameterType::Vector2);
	TestEqual(TEXT("...the pointer as a Position"), DreamNiagaraEngine::FindUserParameter(System, UDreamNiagaraSystem::PointerPositionParameter), EDreamNiagaraParameterType::Position);
	TestEqual(TEXT("...whether it is inside as a bool"), DreamNiagaraEngine::FindUserParameter(System, UDreamNiagaraSystem::PointerInsideParameter), EDreamNiagaraParameterType::Bool);
	TestEqual(TEXT("...and no target"), DreamNiagaraEngine::FindUserParameter(System, UDreamNiagaraSystem::TargetPositionParameter), EDreamNiagaraParameterType::None);

	// The pointer at (20, 30) on the widget: (120, 80) on the canvas, which is the simulation's world in its axes.
	Stage.Visual->SetPointerPositionOverride(FVector2D(20.0, 30.0), true);
	Stage.Frames(2);
	UNiagaraComponent* Component = Stage.Visual->GetNiagaraComponent();
	const FNiagaraParameterStore& Store = Component->GetOverrideParameters();
	const FVector2f Size = Store.GetParameterValue<FVector2f>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("User.DreamUI_WidgetSize")));
	TestTrue(*FString::Printf(TEXT("The widget's size (%s)"), *Size.ToString()), Size.Equals(FVector2f(200.0f, 160.0f)));
	const FVector3f Pointer = Store.GetParameterValue<FVector3f>(FNiagaraVariable(FNiagaraTypeDefinition::GetPositionDef(), TEXT("User.DreamUI_PointerPosition")));
	TestTrue(*FString::Printf(TEXT("The pointer, in the simulation (%s)"), *Pointer.ToString()), Pointer.Equals(FVector3f(120.0f, 0.0f, 80.0f), 0.01f));
	const FNiagaraBool Inside = Store.GetParameterValue<FNiagaraBool>(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("User.DreamUI_PointerInside")));
	TestTrue(TEXT("Inside"), Inside.GetValue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraFixedStepTest,
	"DreamGUI.Niagara.TheSameStepsFromTheStartGiveTheSameParticles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraFixedStepTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	FStage Stage(*this);
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	auto Run = [&Stage](int32 InSteps)
	{
		// Paused between, so that no frame steps it on: only the steps asked for here.
		Stage.Visual->SetPaused(true);
		Stage.Visual->Activate(true);
		Stage.Visual->Advance(InSteps * Step, Step);
		Stage.Frames(1);
		TArray<FVector2D> Centres;
		TSharedPtr<FDreamUIRenderSection_DirectMesh> Fall = Stage.Section(2);
		for (int32 Sprite = 0; Fall.IsValid() && Sprite < Stage.Visual->GetSegmentParticleCount(2); ++Sprite)
		{
			Centres.Add(SpriteCentre(*Fall, Sprite));
		}
		return Centres;
	};
	Stage.Frames(1);
	const TArray<FVector2D> First = Run(20);
	const TArray<FVector2D> Again = Run(20);
	const TArray<FVector2D> Further = Run(40);
	TestEqual(TEXT("Six falling sprites"), First.Num(), 6);
	bool bSame = First.Num() == Again.Num();
	bool bMoved = First.Num() == Further.Num();
	for (int32 Index = 0; Index < First.Num() && bSame && bMoved; ++Index)
	{
		bSame &= First[Index].Equals(Again[Index], 1e-3);
		bMoved &= !First[Index].Equals(Further[Index], 1.0);
	}
	if (First.Num() > 0 && Again.Num() > 0 && Further.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("Sprite 0 after 20, 20 and 40 steps: %s, %s, %s"), *First[0].ToString(), *Again[0].ToString(), *Further[0].ToString()));
	}
	TestTrue(TEXT("Twenty steps from the start, twice: the same places"), bSame);
	TestTrue(TEXT("Forty steps: further along"), bMoved);
	// Thrown right and up at 30, falling at 100: after 20 steps of a 30th, a world-space sprite is right of and below where it
	// started, about (40 + 10i) * 0.667 right.
	if (First.Num() == 6)
	{
		const FVector2D Start(100.0, 50.0);
		TestTrue(*FString::Printf(TEXT("Sprite 0 has gone right (%s)"), *First[0].ToString()), First[0].X > Start.X + 20.0);
		TestTrue(*FString::Printf(TEXT("Sprite 5 has gone further (%s)"), *First[5].ToString()), First[5].X > First[0].X + 20.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraBurstTest,
	"DreamGUI.Niagara.ABurstIsASpawnBesideTheSystemDrawnInItsSegmentsUntilItEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamNiagaraBurstTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	FStage Stage(*this);
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	Stage.Frames(1);
	Stage.Visual->Advance(Step, Step);
	const int32 Id = Stage.Visual->BurstAt(FVector2D(-30.0, 20.0));
	TestTrue(TEXT("A burst is made"), Id != INDEX_NONE);
	TestEqual(TEXT("One burst alive"), Stage.Visual->GetBurstCount(), 1);
	Stage.Visual->Advance(Step, Step);
	Stage.Frames(1);
	TestEqual(TEXT("Grid draws its own 16 and the burst's 16"), Stage.Visual->GetSegmentParticleCount(0), 32);
	TSharedPtr<FDreamUIRenderSection_DirectMesh> Grid = Stage.Section(0);
	if (Grid.IsValid() && Grid->ValidVerticesNum >= 128)
	{
		// The burst's sprite 0 is its grid's bottom left, (-60, -60) from where it stands: (-30, 20) on the widget.
		const FVector2D BurstFirst = SpriteCentre(*Grid, 16);
		TestTrue(*FString::Printf(TEXT("The burst's sprite 0 is at (10, 10) on the canvas (%s)"), *BurstFirst.ToString()), BurstFirst.Equals(FVector2D(10.0, 10.0), 0.05));
	}
	// Deactivated and cleared, the burst ends and is let go of.
	UNiagaraComponent* BurstComponent = nullptr;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		if (It->GetOuter() == Stage.Visual && *It != Stage.Visual->GetNiagaraComponent())
		{
			BurstComponent = *It;
		}
	}
	if (TestNotNull(TEXT("The burst has a component of its own"), BurstComponent))
	{
		BurstComponent->DeactivateImmediate();
		Stage.Frames(4);
		TestEqual(TEXT("The burst is gone"), Stage.Visual->GetBurstCount(), 0);
		TestTrue(TEXT("...and its component destroyed"), !IsValid(BurstComponent) || !BurstComponent->IsRegistered());
		TestEqual(TEXT("Grid draws its own 16 again"), Stage.Visual->GetSegmentParticleCount(0), 16);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraLifetimeTest,
	"DreamGUI.Niagara.TheSimulationLivesAsLongAsItsWidgetIsRegisteredOnAnyCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	TWeakObjectPtr<UNiagaraComponent> Weak;
	UWorld* WorldSeen = nullptr;
	{
		FStage Stage(*this);
		if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
		{
			return false;
		}
		WorldSeen = Stage.World.World;
		Stage.Frames(2);
		UNiagaraComponent* Component = Stage.Visual->GetNiagaraComponent();
		Weak = Component;
		TestTrue(TEXT("The component is registered with the widget's world"), Component->IsRegistered() && Component->GetWorld() == Stage.World.World);
		TestNull(TEXT("...attached to nothing"), Component->GetAttachParent());
		// Not its rendering turned off, which Niagara hands its proxy only as it ticks: no proxy at all. Under -nullrhi nothing
		// activates and nothing would have one; the play-session test of the effects sample holds the scene to it on a GPU.
		TestNull(TEXT("...given no proxy by the scene"), Component->SceneProxy);
		TestFalse(TEXT("...and out of the scalability's reach"), Component->GetAllowScalability());
		TestEqual(TEXT("One simulation in the world"), CountLiveComponents(Stage.World.World), 1);

		// Another canvas: the same simulation, its segments supplied by the new canvas.
		UDreamWidget* OtherRoot = MakeWidget(Stage.World.World, nullptr, TEXT("OtherRoot"), FVector2D(400.0, 300.0));
		UDreamCanvas* Other = OtherRoot->AddComponent<UDreamCanvas>();
		// In the world: a second screen-space root of the same layer is a mistake the manager reports.
		Other->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);
		Other->SetCullElementsOutsideCanvas(false);
		Stage.Holder->TrySetParent(OtherRoot, false);
		Stage.Frames(3);
		TestTrue(TEXT("The widget draws on the other canvas"), Stage.Holder->GetRenderCanvas() == Other);
		TestTrue(TEXT("...with the same simulation"), Stage.Visual->GetNiagaraComponent() == Component);
		int32 OnOther = 0;
		for (const FDreamUIDrawCall& DrawCall : Other->GetCurrentDrawCalls())
		{
			OnOther += DrawCall.Type == EDreamUIDrawCallType::DirectMesh && DrawCall.DirectMeshVisualObject.Get() == Stage.Visual ? 1 : 0;
		}
		TestEqual(TEXT("...its three segments the other canvas's draw calls"), OnOther, 3);
		TestTrue(TEXT("...and supplied by it"), Stage.Section(0).IsValid());
		int32 OnFirst = 0;
		for (const FDreamUIDrawCall& DrawCall : Stage.Canvas->GetCurrentDrawCalls())
		{
			OnFirst += DrawCall.Type == EDreamUIDrawCallType::DirectMesh ? 1 : 0;
		}
		TestEqual(TEXT("...and none left on the first"), OnFirst, 0);

		// Destroyed, the widget takes its simulation with it.
		Stage.Holder->DestroyWidget();
		Stage.Frames(1);
		TestTrue(TEXT("The component is destroyed with the widget"), !Weak.IsValid() || !Weak->IsRegistered());
		TestEqual(TEXT("No simulation left in the world"), CountLiveComponents(Stage.World.World), 0);
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("Nothing of it outlives its world"), Weak.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraReportTest,
	"DreamGUI.Niagara.WhatIsNotDrawnIsReportedWithWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraReportTest::RunTest(const FString& Parameters)
{
	using namespace DreamNiagaraSystemTestLocal;
	UNiagaraSystem* System = LoadSystem(*this, UnsupportedSystemPath, /*bInMustRun*/ false);
	if (System == nullptr)
	{
		return false;
	}
	TArray<FDreamNiagaraRendererInfo> Renderers;
	DreamNiagaraEngine::DescribeSystem(System, Renderers);
	TestEqual(TEXT("Four renderers"), Renderers.Num(), 4);
	int32 Drawn = 0;
	for (const FDreamNiagaraRendererInfo& Info : Renderers)
	{
		Drawn += Info.IsDrawn() ? 1 : 0;
		if (!Info.IsDrawn())
		{
			TestTrue(*FString::Printf(TEXT("%s.%d says why it is not drawn"), *Info.EmitterName.ToString(), Info.RendererIndex), Info.Notes.Num() > 0);
		}
	}
	TestEqual(TEXT("Only the CPU sprite is drawn"), Drawn, 1);
	const FDreamNiagaraRendererInfo* Gpu = Renderers.FindByPredicate([](const FDreamNiagaraRendererInfo& Info) { return Info.EmitterName == TEXT("OnTheGPU"); });
	TestTrue(TEXT("The GPU emitter's sprite is reported as on the GPU"), Gpu != nullptr && !Gpu->IsDrawn() && Gpu->Notes.Num() > 0 && Gpu->Notes[0].ToString().Contains(TEXT("GPU")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamNiagaraGeneratedPathsTest,
	"DreamGUI.Niagara.TheGeneratedSystemsNameNoMachinesPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamNiagaraGeneratedPathsTest::RunTest(const FString& Parameters)
{
	// DreamFX stamps where a generated asset's source was, the machine's full path among it; the committed ones have that
	// taken off (DreamGUI.GeneratedAssets.ForgetFullPaths).
	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssetsByPath(TEXT("/DreamGUI"), Assets, /*bRecursive*/ true);
	int32 Systems = 0;
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath != UNiagaraSystem::StaticClass()->GetClassPathName())
		{
			continue;
		}
		UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset.GetAsset());
		if (System == nullptr)
		{
			continue;
		}
		++Systems;
		FMetaData& MetaData = System->GetPackage()->GetMetaData();
		TestFalse(*FString::Printf(TEXT("%s carries no full path"), *Asset.PackageName.ToString()), MetaData.HasValue(System, TEXT("DreamFX.SourceFullPath")));
	}
	TestTrue(*FString::Printf(TEXT("The plugin's systems are found (%d)"), Systems), Systems >= 7);
	return true;
}

#endif
