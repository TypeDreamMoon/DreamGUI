// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraSystem.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "DreamNiagaraMeshBuilder.h"
#include "DreamNiagaraVertexLayout.h"
#include "Event/DreamPointerEventData.h"

#include "Algo/StableSort.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "TextureResource.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamNiagara, Log, All);

const FName UDreamNiagaraSystem::WidgetSizeParameter(TEXT("DreamUI_WidgetSize"));
const FName UDreamNiagaraSystem::PointerPositionParameter(TEXT("DreamUI_PointerPosition"));
const FName UDreamNiagaraSystem::PointerInsideParameter(TEXT("DreamUI_PointerInside"));
const FName UDreamNiagaraSystem::TargetPositionParameter(TEXT("DreamUI_TargetPosition"));

namespace DreamNiagaraSystemLocal
{
	/** The most fixed steps a frame takes, so that a long frame does not stall the next one. */
	constexpr int32 MaxStepsPerFrame = 8;
	/** The plugin's fly-to template, spawned by BurstTo when no flight system is set. */
	const TCHAR* const FlightTemplatePath = TEXT("/DreamGUI/Niagara/NS_DreamUI_FlyTo.NS_DreamUI_FlyTo");

	/** The simulation's axes onto the widget's plane (X out of it, Y right, Z up), InScale canvas units to a unit. Row vectors. */
	FMatrix PlaneMatrix(EDreamNiagaraPlane InPlane, double InScale)
	{
		FMatrix Matrix = FMatrix::Identity;
		const FVector Origin = FVector::ZeroVector;
		if (InPlane == EDreamNiagaraPlane::XY)
		{
			// X right, Y up, Z out of the plane: a turn of the axes, so nothing is mirrored.
			const FVector X(0.0, InScale, 0.0), Y(0.0, 0.0, InScale), Z(InScale, 0.0, 0.0);
			Matrix.SetAxes(&X, &Y, &Z, &Origin);
		}
		else
		{
			// X right, Z up, Y into the plane.
			const FVector X(0.0, InScale, 0.0), Y(-InScale, 0.0, 0.0), Z(0.0, 0.0, InScale);
			Matrix.SetAxes(&X, &Y, &Z, &Origin);
		}
		return Matrix;
	}

	FMatrix44f ToFloat(const FMatrix& InMatrix)
	{
		return FMatrix44f(InMatrix);
	}

	/**
	 * A white texture whose alpha falls off as the engine's default particle materials' does: from the centre of a disc for a
	 * sprite, from the middle of the band for a ribbon (across V). Made once, kept for the process.
	 */
	UTexture2D* SoftTexture(bool bInBand)
	{
		static UTexture2D* Disc = nullptr;
		static UTexture2D* Band = nullptr;
		UTexture2D*& Made = bInBand ? Band : Disc;
		if (Made != nullptr)
		{
			return Made;
		}
		constexpr int32 Size = 64;
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8, bInBand ? TEXT("DreamUI_ParticleSoftBand") : TEXT("DreamUI_ParticleSoftDisc"));
		if (Texture == nullptr)
		{
			return nullptr;
		}
		Texture->SRGB = false;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				const float U = (X + 0.5f) / Size * 2.0f - 1.0f;
				const float V = (Y + 0.5f) / Size * 2.0f - 1.0f;
				const float Distance = bInBand ? FMath::Abs(V) : FMath::Sqrt(U * U + V * V);
				// The exponential falloff of the engine's radial gradient, at the density its default sprite uses.
				const float Alpha = FMath::Clamp(1.0f - Distance, 0.0f, 1.0f);
				Pixels[Y * Size + X] = FColor(255, 255, 255, static_cast<uint8>(FMath::Pow(Alpha, 1.6f) * 255.0f + 0.5f));
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		// Rooted, and never let go of: it is the process's, as an engine default texture is.
		Texture->AddToRoot();
		Made = Texture;
		return Texture;
	}
}

UDreamNiagaraSystem::UDreamNiagaraSystem(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Particles are rarely what a click is aimed at: the widget's rect answers when it is a raycast target at all.
	bRaycastTarget = false;
}

bool UDreamNiagaraSystem::HaveValidData()const
{
	// A system to simulate, or only a flight system for BurstTo.
	return (System != nullptr || FlightSystem != nullptr) && GetSegmentCount() > 0;
}

void UDreamNiagaraSystem::DescribeIfStale()const
{
	if (DescribedSystem.Get() != System.Get() || (System != nullptr && RendererInfo.Num() == 0 && !DescribedSystem.IsValid()))
	{
		DreamNiagaraEngine::DescribeSystem(System, RendererInfo);
		DescribedSystem = System.Get();
	}
	if (DescribedFlightSystem.Get() != FlightSystem.Get())
	{
		DreamNiagaraEngine::DescribeSystem(FlightSystem, FlightRendererInfo);
		DescribedFlightSystem = FlightSystem.Get();
	}
}

const TArray<FDreamNiagaraRendererInfo>& UDreamNiagaraSystem::GetRendererInfo()const
{
	DescribeIfStale();
	return RendererInfo;
}

namespace DreamNiagaraSystemLocal
{
	/** The drawn renderers of InInfo, in Niagara's renderer order: its sort hint, then the emitter's and the renderer's place. */
	void DrawnInOrder(const TArray<FDreamNiagaraRendererInfo>& InInfo, TArray<int32>& OutOrder)
	{
		OutOrder.Reset();
		for (int32 Index = 0; Index < InInfo.Num(); ++Index)
		{
			if (InInfo[Index].IsDrawn())
			{
				OutOrder.Add(Index);
			}
		}
		Algo::StableSort(OutOrder, [&InInfo](int32 A, int32 B) { return InInfo[A].SortOrderHint < InInfo[B].SortOrderHint; });
	}
}

int32 UDreamNiagaraSystem::GetSegmentCount()const
{
	if (System == nullptr && FlightSystem == nullptr)
	{
		return 0;
	}
	DescribeIfStale();
	int32 Count = 0;
	for (const FDreamNiagaraRendererInfo& Info : RendererInfo)
	{
		Count += Info.IsDrawn() ? 1 : 0;
	}
	for (const FDreamNiagaraRendererInfo& Info : FlightRendererInfo)
	{
		Count += Info.IsDrawn() ? 1 : 0;
	}
	return Count;
}

void UDreamNiagaraSystem::RebuildSegments()
{
	using namespace DreamNiagaraSystemLocal;
	DescribeIfStale();
	TArray<int32> Order;
	TArray<FSegment> Made;
	DrawnInOrder(RendererInfo, Order);
	for (int32 Renderer : Order)
	{
		FSegment& Segment = Made.AddDefaulted_GetRef();
		Segment.Renderer = Renderer;
	}
	DrawnInOrder(FlightRendererInfo, Order);
	for (int32 Renderer : Order)
	{
		FSegment& Segment = Made.AddDefaulted_GetRef();
		Segment.Renderer = Renderer;
		Segment.bFlight = true;
	}
	Segments = MoveTemp(Made);
	bSegmentsStale = false;
}

const FDreamNiagaraRendererInfo* UDreamNiagaraSystem::RendererOfSegment(const FSegment& InSegment)const
{
	const TArray<FDreamNiagaraRendererInfo>& Info = InSegment.bFlight ? FlightRendererInfo : RendererInfo;
	return Info.IsValidIndex(InSegment.Renderer) ? &Info[InSegment.Renderer] : nullptr;
}

int32 UDreamNiagaraSystem::GetRendererOfSegment(int32 InSegment)const
{
	return Segments.IsValidIndex(InSegment) && !Segments[InSegment].bFlight ? Segments[InSegment].Renderer : INDEX_NONE;
}

int32 UDreamNiagaraSystem::GetSegmentParticleCount(int32 InSegment)const
{
	return Segments.IsValidIndex(InSegment) ? Segments[InSegment].LastCount : 0;
}

const FDreamNiagaraRendererOverride* UDreamNiagaraSystem::FindOverride(const FDreamNiagaraRendererInfo& InRenderer)const
{
	return RendererOverrides.FindByPredicate([&InRenderer](const FDreamNiagaraRendererOverride& InOverride)
	{
		return InOverride.Emitter == InRenderer.EmitterName && InOverride.Renderer == InRenderer.RendererIndex;
	});
}

bool UDreamNiagaraSystem::GetSegmentShading(int32 InSegment, FDreamUIDirectMeshShading& OutShading)const
{
	// The canvas asks while it builds its draw calls, which is where a segment list gone stale is made again: the count the
	// canvas prepared with and the list agree from here.
	if (bSegmentsStale || !Segments.IsValidIndex(InSegment))
	{
		const_cast<UDreamNiagaraSystem*>(this)->RebuildSegments();
	}
	const FDreamNiagaraRendererInfo* Renderer = Segments.IsValidIndex(InSegment) ? RendererOfSegment(Segments[InSegment]) : nullptr;
	if (Renderer == nullptr)
	{
		OutShading = FDreamUIDirectMeshShading();
		return true;
	}
	const FDreamNiagaraRendererOverride* Override = FindOverride(*Renderer);
	const EDreamNiagaraShading Shading = Override != nullptr && Override->Shading != EDreamNiagaraShading::Default ? Override->Shading : DefaultShading;
	if (Shading == EDreamNiagaraShading::Material)
	{
		UMaterialInterface* UIMaterial = Override != nullptr ? Override->Material.Get() : nullptr;
		if (UIMaterial == nullptr)
		{
			const TObjectPtr<UMaterialInterface>* Mapped = MaterialMap.Find(Renderer->Material.Get());
			UIMaterial = Mapped != nullptr ? Mapped->Get() : nullptr;
		}
		if (UIMaterial != nullptr)
		{
			OutShading.bBuiltIn = false;
			OutShading.Material = UIMaterial;
			OutShading.Texture = Override != nullptr && Override->Texture != nullptr ? Override->Texture.Get() : Renderer->InferredTexture.Get();
			return true;
		}
	}
	OutShading.bBuiltIn = true;
	OutShading.Texture = Override != nullptr && Override->Texture != nullptr ? Override->Texture.Get() : Renderer->InferredTexture.Get();
	if (!OutShading.Texture.IsValid())
	{
		// The engine's default particle materials draw a soft disc (and a ribbon soft at its edges) without a texture: the
		// built-in shader is given one that draws the same.
		OutShading.Texture = DreamNiagaraSystemLocal::SoftTexture(Renderer->Kind == EDreamNiagaraRendererKind::Ribbon);
	}
	OutShading.BlendMode = Override != nullptr && Override->bOverrideBlendMode ? Override->BlendMode : (bOverrideBlendMode ? BlendMode : Renderer->InferredBlendMode);
	return true;
}

uint8 UDreamNiagaraSystem::GetWidgetPropertyMark()const
{
	return DreamNiagaraVertexLayout::ParticleMark;
}

//~ The component

void UDreamNiagaraSystem::OnRegister()
{
	Super::OnRegister();
	bSegmentsStale = true;
	EnsureComponent();
}

void UDreamNiagaraSystem::OnUnregister()
{
	DestroyComponents();
	Super::OnUnregister();
}

void UDreamNiagaraSystem::BeginDestroy()
{
	// Unregistered first, in the normal course, which destroyed the components; a collection that finds them here only lets go.
	Component = nullptr;
	Bursts.Reset();
	BurstComponents.Reset();
	Super::BeginDestroy();
}

void UDreamNiagaraSystem::OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas)
{
	// The new canvas supplies its own sections as it builds its draw calls; the simulation goes on as it was.
	Super::OnRenderCanvasChanged(InOldCanvas, InNewCanvas);
	bSegmentsStale = true;
}

bool UDreamNiagaraSystem::DrivesTime()const
{
	const UWorld* World = GetWorld();
	// A world that is not a game's -- the designer's preview, the level editor -- does not tick a component of its own: the
	// element steps the simulation as its canvas draws.
	return FixedTimeStep > 0.0f || World == nullptr || !World->IsGameWorld();
}

UNiagaraComponent* UDreamNiagaraSystem::MakeComponent(UNiagaraSystem* InSystem, const FVector2D& InLocalPosition)const
{
	UWorld* World = GetWorld();
	if (World == nullptr || InSystem == nullptr)
	{
		return nullptr;
	}
	UNiagaraComponent* Made = NewObject<UNiagaraComponent>(const_cast<UDreamNiagaraSystem*>(this), NAME_None, DreamUI::RuntimeObjectFlags);
	Made->SetAutoActivate(false);
	Made->SetAutoDestroy(false);
	// Simulated only: nothing of it goes into the scene, and nothing culls it for not being seen.
	Made->SetRenderingEnabled(false);
	Made->SetAllowScalability(false);
	Made->SetUsingAbsoluteLocation(true);
	Made->SetUsingAbsoluteRotation(true);
	Made->SetUsingAbsoluteScale(true);
	Made->SetAsset(InSystem);
	Made->SetRandomSeedOffset(RandomSeedOffset);
	ConfigureTime(Made);
	PlaceComponent(Made, InLocalPosition);
	Made->RegisterComponentWithWorld(World);
	return Made;
}

void UDreamNiagaraSystem::ConfigureTime(UNiagaraComponent* InComponent)const
{
	if (InComponent == nullptr)
	{
		return;
	}
	// Solo, so that the instance ticks with its component (or with Advance) and not batched with the world's systems.
	InComponent->SetForceSolo(true);
	if (DrivesTime())
	{
		InComponent->SetComponentTickEnabled(false);
		InComponent->SetCustomTimeDilation(1.0f);
	}
	else
	{
		InComponent->PrimaryComponentTick.bTickEvenWhenPaused = bTickWhenGamePaused;
		InComponent->SetComponentTickEnabled(true);
		InComponent->SetCustomTimeDilation(TimeDilation);
		InComponent->SetPaused(bPaused);
	}
}

void UDreamNiagaraSystem::EnsureComponent()
{
	if (Component != nullptr || System == nullptr || GetWidget() == nullptr || GetWorld() == nullptr)
	{
		return;
	}
	Component = MakeComponent(System, FVector2D::ZeroVector);
	if (Component != nullptr && bAutoActivate)
	{
		Activate(true);
	}
}

void UDreamNiagaraSystem::DestroyComponents()
{
	if (Component != nullptr)
	{
		Component->DestroyComponent();
		Component = nullptr;
	}
	for (UNiagaraComponent* Burst : BurstComponents)
	{
		if (Burst != nullptr)
		{
			Burst->DestroyComponent();
		}
	}
	Bursts.Reset();
	BurstComponents.Reset();
}

void UDreamNiagaraSystem::SetSystem(UNiagaraSystem* InSystem)
{
	if (System == InSystem)
	{
		return;
	}
	DestroyComponents();
	System = InSystem;
	bSegmentsStale = true;
	EnsureComponent();
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(true);
	}
}

void UDreamNiagaraSystem::StartWhenReady(UNiagaraComponent* InComponent, bool bInReset)const
{
	if (InComponent == nullptr)
	{
		return;
	}
	// Niagara waits for a system that is not ready yet by ticking the component until it is. Where this element steps the
	// simulation itself, it asks again instead (StepSimulation), and the component's own tick goes off once it runs.
	InComponent->Activate(bInReset);
	if (DrivesTime() && InComponent->IsActive())
	{
		InComponent->SetComponentTickEnabled(false);
	}
}

void UDreamNiagaraSystem::Activate(bool bReset)
{
	EnsureComponent();
	bWantsActive = true;
	if (bReset)
	{
		// From the start: what a fixed step had left over belongs to the run before.
		StepRemainder = 0.0f;
	}
	bActivationResets = bReset;
	if (Component == nullptr)
	{
		return;
	}
	WriteUIParameters(Component, System, nullptr);
	if (bReset && Component->IsActive())
	{
		// From the start, now: a running instance told to reset does it at its next tick, which a simulation this element
		// steps never has; it is stopped at once and started again instead.
		Component->DeactivateImmediate();
	}
	StartWhenReady(Component, bReset);
	if (WarmupTime > 0.0f && Component->IsActive())
	{
		Advance(WarmupTime, FixedTimeStep > 0.0f ? FixedTimeStep : 1.0f / 30.0f);
	}
}

void UDreamNiagaraSystem::Deactivate()
{
	bWantsActive = false;
	if (Component != nullptr)
	{
		Component->Deactivate();
	}
}

void UDreamNiagaraSystem::ResetSystem()
{
	if (Component != nullptr)
	{
		Component->ResetSystem();
	}
}

bool UDreamNiagaraSystem::IsActive()const
{
	return Component != nullptr && Component->IsActive();
}

void UDreamNiagaraSystem::SetPaused(bool bInPaused)
{
	bPaused = bInPaused;
	if (!DrivesTime())
	{
		if (Component != nullptr)
		{
			Component->SetPaused(bPaused);
		}
		for (const FBurst& Burst : Bursts)
		{
			if (Burst.Component != nullptr)
			{
				Burst.Component->SetPaused(bPaused);
			}
		}
	}
}

void UDreamNiagaraSystem::SetTimeDilation(float InTimeDilation)
{
	TimeDilation = FMath::Max(0.0f, InTimeDilation);
	if (!DrivesTime())
	{
		if (Component != nullptr)
		{
			Component->SetCustomTimeDilation(TimeDilation);
		}
		for (const FBurst& Burst : Bursts)
		{
			if (Burst.Component != nullptr)
			{
				Burst.Component->SetCustomTimeDilation(TimeDilation);
			}
		}
	}
}

void UDreamNiagaraSystem::Advance(float InSeconds, float InStep)
{
	if (InSeconds <= 0.0f || InStep <= UE_SMALL_NUMBER)
	{
		return;
	}
	const int32 Steps = FMath::Max(1, FMath::FloorToInt(InSeconds / InStep + UE_KINDA_SMALL_NUMBER));
	auto AdvanceOne = [this, Steps, InStep](UNiagaraComponent* InComponent)
	{
		if (InComponent == nullptr)
		{
			return;
		}
		// A paused instance takes no steps: the pause is lifted for these and put back.
		const bool bWasPaused = InComponent->IsPaused();
		if (bWasPaused)
		{
			InComponent->SetPaused(false);
		}
		InComponent->AdvanceSimulation(Steps, InStep);
		if (bWasPaused)
		{
			InComponent->SetPaused(true);
		}
	};
	AdvanceOne(Component);
	for (const FBurst& Burst : Bursts)
	{
		AdvanceOne(Burst.Component);
	}
}

void UDreamNiagaraSystem::StepSimulation()
{
	if (!DrivesTime() || LastSteppedFrame == GFrameCounter)
	{
		return;
	}
	// Asked to run and not running yet, its system not ready when it was: tried again.
	if (bWantsActive && Component != nullptr && !Component->IsActive())
	{
		StartWhenReady(Component, bActivationResets);
	}
	if (bPaused)
	{
		return;
	}
	LastSteppedFrame = GFrameCounter;
	const UWorld* World = GetWorld();
	float Delta = FApp::GetDeltaTime();
	if (World != nullptr && World->IsGameWorld())
	{
		Delta = World->IsPaused() ? (bTickWhenGamePaused ? FApp::GetDeltaTime() : 0.0f) : World->GetDeltaSeconds();
	}
	Delta *= TimeDilation;
	if (Delta <= 0.0f)
	{
		return;
	}
	auto StepOne = [](UNiagaraComponent* InComponent, int32 InSteps, float InStep)
	{
		if (InComponent != nullptr && InComponent->IsActive() && InSteps > 0)
		{
			InComponent->AdvanceSimulation(InSteps, InStep);
		}
	};
	int32 Steps = 1;
	float Step = Delta;
	if (FixedTimeStep > 0.0f)
	{
		StepRemainder += Delta;
		Steps = FMath::Min(FMath::FloorToInt(StepRemainder / FixedTimeStep), DreamNiagaraSystemLocal::MaxStepsPerFrame);
		StepRemainder = FMath::Max(0.0f, StepRemainder - Steps * FixedTimeStep);
		// What is left past the most steps a frame takes is dropped, rather than owed to the frames after.
		StepRemainder = FMath::Min(StepRemainder, FixedTimeStep);
		Step = FixedTimeStep;
	}
	StepOne(Component, Steps, Step);
	for (const FBurst& Burst : Bursts)
	{
		StepOne(Burst.Component, Steps, Step);
	}
}

//~ Space

FMatrix UDreamNiagaraSystem::SimToWidgetMatrix()const
{
	return DreamNiagaraSystemLocal::PlaneMatrix(Plane, FMath::Max(PixelsPerUnit, 0.001f));
}

FMatrix UDreamNiagaraSystem::WidgetToCanvasMatrix()const
{
	const UDreamWidget* Widget = GetWidget();
	const UDreamCanvas* Canvas = Widget != nullptr ? Widget->GetRenderCanvas() : nullptr;
	const UDreamWidget* CanvasWidget = Canvas != nullptr ? Canvas->GetWidget() : nullptr;
	if (Widget == nullptr || CanvasWidget == nullptr)
	{
		return FMatrix::Identity;
	}
	return Widget->GetWorldTransform().ToMatrixWithScale() * CanvasWidget->GetWorldTransform().ToMatrixWithScale().Inverse();
}

FVector UDreamNiagaraSystem::SimToCanvas(const FVector& InSimPosition)const
{
	return SimToWidgetMatrix().TransformPosition(InSimPosition);
}

FVector UDreamNiagaraSystem::CanvasToSim(const FVector& InCanvasPosition)const
{
	return SimToWidgetMatrix().InverseFast().TransformPosition(InCanvasPosition);
}

void UDreamNiagaraSystem::PlaceComponent(UNiagaraComponent* InComponent, const FVector2D& InLocalPosition)const
{
	if (InComponent == nullptr)
	{
		return;
	}
	// The component stands, in the simulation's world (the canvas, in its axes), where the widget does on the canvas: so a
	// local-space emitter's particles land on the widget, and a world-space one's are left where they were spawned.
	const FMatrix Map = SimToWidgetMatrix();
	const FMatrix Offset = FTranslationMatrix(FVector(0.0, InLocalPosition.X, InLocalPosition.Y));
	const FMatrix ComponentToSim = Map * Offset * WidgetToCanvasMatrix() * Map.InverseFast();
	InComponent->SetWorldTransform(FTransform(ComponentToSim));
}

void UDreamNiagaraSystem::WriteUIParameters(UNiagaraComponent* InComponent, const UNiagaraSystem* InSystem, const FBurst* InBurst)
{
	if (!bWriteUIParameters || InComponent == nullptr || InSystem == nullptr)
	{
		return;
	}
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return;
	}
	using EType = EDreamNiagaraParameterType;
	const double Units = FMath::Max(PixelsPerUnit, 0.001f);
	// The widget's size, in simulation units.
	{
		const EType Type = DreamNiagaraEngine::FindUserParameter(InSystem, WidgetSizeParameter);
		const FVector2D Size = Widget->GetSize() / Units;
		if (Type == EType::Vector2)
		{
			InComponent->SetVariableVec2(WidgetSizeParameter, Size);
		}
		else if (Type == EType::Vector3)
		{
			InComponent->SetVariableVec3(WidgetSizeParameter, FVector(Size.X, Size.Y, 0.0));
		}
	}
	const FMatrix WidgetToCanvas = WidgetToCanvasMatrix();
	auto WritePosition = [InComponent, InSystem](FName InName, const FVector& InSimPosition)
	{
		const EType Type = DreamNiagaraEngine::FindUserParameter(InSystem, InName);
		if (Type == EType::Position)
		{
			InComponent->SetVariablePosition(InName, InSimPosition);
		}
		else if (Type == EType::Vector3)
		{
			InComponent->SetVariableVec3(InName, InSimPosition);
		}
		else if (Type == EType::Vector2)
		{
			InComponent->SetVariableVec2(InName, FVector2D(InSimPosition.X, InSimPosition.Z));
		}
	};
	// The pointer, in the simulation's world, and whether it is over the widget.
	{
		FVector Canvas = FVector::ZeroVector;
		bool bInside = false;
		bool bKnown = false;
		if (bPointerOverride)
		{
			Canvas = WidgetToCanvas.TransformPosition(FVector(0.0, PointerOverride.X, PointerOverride.Y));
			bInside = bPointerOverrideInside;
			bKnown = true;
		}
		else if (const UDreamUIInputServices* Services = UDreamUIInputServices::Get(Widget))
		{
			if (const UDreamPointerEventData* Pointer = Services->FindPointer(0, 0))
			{
				const UDreamCanvas* RenderCanvas = Widget->GetRenderCanvas();
				const UDreamWidget* CanvasWidget = RenderCanvas != nullptr ? RenderCanvas->GetWidget() : nullptr;
				if (CanvasWidget != nullptr)
				{
					Canvas = CanvasWidget->GetWorldTransform().InverseTransformPosition(Pointer->WorldPoint);
					bInside = Pointer->EnterWidgetStack.ContainsByPredicate([Widget](const UDreamWidget* InEntry) { return InEntry == Widget; });
					bKnown = true;
				}
			}
		}
		if (bKnown)
		{
			WritePosition(PointerPositionParameter, CanvasToSim(Canvas));
			const EType InsideType = DreamNiagaraEngine::FindUserParameter(InSystem, PointerInsideParameter);
			if (InsideType == EType::Bool)
			{
				InComponent->SetVariableBool(PointerInsideParameter, bInside);
			}
			else if (InsideType == EType::Float)
			{
				InComponent->SetVariableFloat(PointerInsideParameter, bInside ? 1.0f : 0.0f);
			}
		}
	}
	// A flight's target: its centre on the canvas, in the simulation's world.
	if (InBurst != nullptr && InBurst->Target.IsValid())
	{
		const UDreamWidget* Target = InBurst->Target.Get();
		const UDreamCanvas* RenderCanvas = Widget->GetRenderCanvas();
		const UDreamWidget* CanvasWidget = RenderCanvas != nullptr ? RenderCanvas->GetWidget() : nullptr;
		if (CanvasWidget != nullptr)
		{
			const FVector2D Centre = Target->GetLocalSpaceCenter();
			const FVector World = Target->GetWorldTransform().TransformPosition(FVector(0.0, Centre.X, Centre.Y));
			WritePosition(TargetPositionParameter, CanvasToSim(CanvasWidget->GetWorldTransform().InverseTransformPosition(World)));
		}
	}
}

void UDreamNiagaraSystem::SetPointerPositionOverride(FVector2D InLocalPosition, bool bInInside)
{
	bPointerOverride = true;
	bPointerOverrideInside = bInInside;
	PointerOverride = InLocalPosition;
	WriteUIParameters(Component, System, nullptr);
}

void UDreamNiagaraSystem::ClearPointerPositionOverride()
{
	bPointerOverride = false;
}

//~ Bursts

int32 UDreamNiagaraSystem::BurstAt(FVector2D InLocalPosition)
{
	if (System == nullptr || GetWorld() == nullptr)
	{
		return INDEX_NONE;
	}
	UNiagaraComponent* Made = MakeComponent(System, InLocalPosition);
	if (Made == nullptr)
	{
		return INDEX_NONE;
	}
	FBurst& Burst = Bursts.AddDefaulted_GetRef();
	Burst.Id = NextBurstId++;
	Burst.Component = Made;
	Burst.LocalPosition = InLocalPosition;
	BurstComponents.Add(Made);
	WriteUIParameters(Made, System, &Burst);
	StartWhenReady(Made, true);
	Burst.bStarted = Made->IsActive();
	return Burst.Id;
}

int32 UDreamNiagaraSystem::BurstTo(UDreamWidget* InTarget, FVector2D InLocalPosition)
{
	if (InTarget == nullptr || GetWorld() == nullptr)
	{
		return INDEX_NONE;
	}
	if (FlightSystem == nullptr)
	{
		FlightSystem = LoadObject<UNiagaraSystem>(nullptr, DreamNiagaraSystemLocal::FlightTemplatePath);
		if (FlightSystem == nullptr)
		{
			UE_LOG(LogDreamNiagara, Warning, TEXT("%s: BurstTo has no flight system, and the template %s did not load."), *GetPathName(), DreamNiagaraSystemLocal::FlightTemplatePath);
			return INDEX_NONE;
		}
		// Its renderers are segments of their own, after the system's.
		bSegmentsStale = true;
		if (UDreamWidget* Widget = GetWidget())
		{
			Widget->MarkCanvasUpdate(true);
		}
	}
	UNiagaraComponent* Made = MakeComponent(FlightSystem, InLocalPosition);
	if (Made == nullptr)
	{
		return INDEX_NONE;
	}
	FBurst& Burst = Bursts.AddDefaulted_GetRef();
	Burst.Id = NextBurstId++;
	Burst.Component = Made;
	Burst.bFlight = true;
	Burst.LocalPosition = InLocalPosition;
	Burst.Target = InTarget;
	BurstComponents.Add(Made);
	WriteUIParameters(Made, FlightSystem, &Burst);
	StartWhenReady(Made, true);
	Burst.bStarted = Made->IsActive();
	return Burst.Id;
}

void UDreamNiagaraSystem::UpdateBursts()
{
	for (int32 Index = Bursts.Num() - 1; Index >= 0; --Index)
	{
		FBurst& Burst = Bursts[Index];
		++Burst.Age;
		UNiagaraComponent* BurstComponent = Burst.Component;
		if (BurstComponent != nullptr && !Burst.bStarted)
		{
			// Still waiting for its system: tried again, and let go of when it never runs.
			StartWhenReady(BurstComponent, true);
			Burst.bStarted = BurstComponent->IsActive();
		}
		const bool bDone = BurstComponent == nullptr || (Burst.bStarted && Burst.Age > 2 && (BurstComponent->IsComplete() || !BurstComponent->IsActive())) || (!Burst.bStarted && Burst.Age > 600);
		if (!bDone)
		{
			PlaceComponent(BurstComponent, Burst.LocalPosition);
			WriteUIParameters(BurstComponent, Burst.bFlight ? FlightSystem.Get() : System.Get(), &Burst);
			continue;
		}
		const int32 Id = Burst.Id;
		if (BurstComponent != nullptr)
		{
			BurstComponents.Remove(BurstComponent);
			BurstComponent->DestroyComponent();
		}
		Bursts.RemoveAt(Index);
		OnBurstFinished.Broadcast(Id);
	}
}

//~ The frame's vertices

bool UDreamNiagaraSystem::RefreshMeshSegments()
{
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr || !Mesh.IsValid())
	{
		return false;
	}
	if (bSegmentsStale)
	{
		// The segments the canvas made sections for no longer match: it builds its draw calls again.
		Widget->MarkCanvasUpdate(true);
		return false;
	}
	StepSimulation();
	DreamNiagaraEngine::FinishTick(Component);
	for (const FBurst& Burst : Bursts)
	{
		DreamNiagaraEngine::FinishTick(Burst.Component);
	}

	// The widget's record: its mark and clip; every vertex is written with its row below.
	bWidgetPropertyDataStartPositionChanged = false;
	UpdateWidgetPropertyRecord();
	FDreamNiagaraVertexCommon Common;
	Common.WidgetRecord = static_cast<float>(GetWidgetPropertyDataStartPosition());
	Common.Tint = FLinearColor::FromSRGBColor(GetFinalColor());

	const FMatrix Map = SimToWidgetMatrix();
	const FMatrix WidgetToCanvas = WidgetToCanvasMatrix();
	const FMatrix MapInverse = Map.InverseFast();
	bool bBoundsMoved = false;
	for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
	{
		FSegment& Segment = Segments[SegmentIndex];
		TSharedPtr<FDreamUIRenderSection_DirectMesh> Section = GetMeshSegment(SegmentIndex);
		const FDreamNiagaraRendererInfo* Renderer = RendererOfSegment(Segment);
		if (!Section.IsValid() || Renderer == nullptr)
		{
			continue;
		}
		FDreamNiagaraBuildSpace Space;
		Space.SimToPlane = DreamNiagaraSystemLocal::ToFloat(Map);
		Space.PlaneToCanvas = Renderer->bLocalSpace ? DreamNiagaraSystemLocal::ToFloat(WidgetToCanvas) : FMatrix44f::Identity;
		Space.SizeScale = FMath::Max(PixelsPerUnit, 0.001f);
		Space.bFlattenDepth = bFlattenDepth;

		// The instances this segment reads: the system's own and its BurstAt spawns, or the flights.
		TArray<TPair<UNiagaraComponent*, const FBurst*>, TInlineAllocator<8>> Instances;
		if (!Segment.bFlight)
		{
			Instances.Emplace(Component, nullptr);
		}
		for (const FBurst& Burst : Bursts)
		{
			if (Burst.bFlight == Segment.bFlight)
			{
				Instances.Emplace(Burst.Component, &Burst);
			}
		}
		FDreamNiagaraMeshTarget Target;
		Target.Vertices = &Section->Vertices;
		Target.Indices = &Section->TriangleIndices;
		int32 Count = 0;
		if (Renderer->Kind == EDreamNiagaraRendererKind::Sprite)
		{
			Segment.Sprites.Reset();
			for (const TPair<UNiagaraComponent*, const FBurst*>& Instance : Instances)
			{
				const int32 First = Segment.Sprites.Num();
				DreamNiagaraEngine::ReadSprites(Instance.Key, *Renderer, Segment.Sprites);
				// A burst's local-space particles are relative to where it stands on the widget.
				if (Renderer->bLocalSpace && Instance.Value != nullptr)
				{
					const FVector3f Offset(FVector(MapInverse.TransformVector(FVector(0.0, Instance.Value->LocalPosition.X, Instance.Value->LocalPosition.Y))));
					for (int32 Index = First; Index < Segment.Sprites.Num(); ++Index)
					{
						Segment.Sprites[Index].Position += Offset;
					}
				}
			}
			Count = Segment.Sprites.Num();
			FDreamNiagaraMeshBuilder::BuildSprites(Segment.Sprites, Renderer->Sprite, Space, Common, Target);
		}
		else
		{
			Segment.Ribbons.Reset();
			for (int32 InstanceIndex = 0; InstanceIndex < Instances.Num(); ++InstanceIndex)
			{
				const TPair<UNiagaraComponent*, const FBurst*>& Instance = Instances[InstanceIndex];
				const int32 First = Segment.Ribbons.Num();
				DreamNiagaraEngine::ReadRibbons(Instance.Key, *Renderer, InstanceIndex, Segment.Ribbons);
				if (Renderer->bLocalSpace && Instance.Value != nullptr)
				{
					const FVector3f Offset(FVector(MapInverse.TransformVector(FVector(0.0, Instance.Value->LocalPosition.X, Instance.Value->LocalPosition.Y))));
					for (int32 Index = First; Index < Segment.Ribbons.Num(); ++Index)
					{
						Segment.Ribbons[Index].Position += Offset;
					}
				}
			}
			Count = Segment.Ribbons.Num();
			FDreamNiagaraMeshBuilder::BuildRibbons(Segment.Ribbons, Renderer->Ribbon, Space, Common, Target);
		}
		Segment.LastCount = Count;
		if (Target.NumDropped > 0 && !Segment.bWarnedDropped)
		{
			Segment.bWarnedDropped = true;
			UE_LOG(LogDreamNiagara, Warning, TEXT("%s: renderer %d of emitter %s has %d particles more than one draw call can index; they are not drawn."),
				*GetPathName(), Renderer->RendererIndex, *Renderer->EmitterName.ToString(), Target.NumDropped);
		}
		Section->ValidVerticesNum = Target.NumVertices;
		Section->ValidTriangleIndicesNum = Target.NumIndices;
		// Canvas space here: the mesh takes it to the world. An empty segment is boxed at the widget, where it would be.
		FBox Bounds = Target.Bounds;
		if (!Bounds.IsValid)
		{
			const FVector Origin = WidgetToCanvas.GetOrigin();
			Bounds = FBox(Origin, Origin);
		}
		const FBox Before = Section->BoundingBox;
		Section->BoundingBox = Bounds;
		Mesh->UpdateDirectMeshSectionGeometry(Section.Get(), Target.bGrew);
		bBoundsMoved |= !(Section->BoundingBox == Before);
	}

	// For the next frame: where the simulation stands and what it is told of the UI.
	PlaceComponent(Component, FVector2D::ZeroVector);
	WriteUIParameters(Component, System, nullptr);
	UpdateBursts();
	return bBoundsMoved;
}

//~ Setters

void UDreamNiagaraSystem::MarkShadingChanged()
{
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->MarkCanvasUpdate(true);
	}
}

void UDreamNiagaraSystem::SetPixelsPerUnit(float InPixelsPerUnit)
{
	PixelsPerUnit = FMath::Max(InPixelsPerUnit, 0.001f);
}

void UDreamNiagaraSystem::SetPlane(EDreamNiagaraPlane InPlane)
{
	Plane = InPlane;
}

void UDreamNiagaraSystem::SetDefaultShading(EDreamNiagaraShading InShading)
{
	if (DefaultShading != InShading)
	{
		DefaultShading = InShading;
		MarkShadingChanged();
	}
}

void UDreamNiagaraSystem::SetBlendModeOverride(bool bInOverride, EDreamUIBlendMode InBlendMode)
{
	if (bOverrideBlendMode != bInOverride || BlendMode != InBlendMode)
	{
		bOverrideBlendMode = bInOverride;
		BlendMode = InBlendMode;
		MarkShadingChanged();
	}
}

void UDreamNiagaraSystem::SetRendererOverrides(const TArray<FDreamNiagaraRendererOverride>& InOverrides)
{
	RendererOverrides = InOverrides;
	MarkShadingChanged();
}

void UDreamNiagaraSystem::SetMaterialMap(const TMap<UMaterialInterface*, UMaterialInterface*>& InMap)
{
	MaterialMap.Reset();
	for (const TPair<UMaterialInterface*, UMaterialInterface*>& Entry : InMap)
	{
		MaterialMap.Add(Entry.Key, Entry.Value);
	}
	MarkShadingChanged();
}

void UDreamNiagaraSystem::SetMappedMaterial(UMaterialInterface* InParticleMaterial, UMaterialInterface* InUIMaterial)
{
	if (InParticleMaterial == nullptr)
	{
		return;
	}
	MaterialMap.Add(InParticleMaterial, InUIMaterial);
	MarkShadingChanged();
}

void UDreamNiagaraSystem::SetVariableFloat(FName InName, float InValue)
{
	if (Component != nullptr) { Component->SetVariableFloat(InName, InValue); }
}

void UDreamNiagaraSystem::SetVariableInt(FName InName, int32 InValue)
{
	if (Component != nullptr) { Component->SetVariableInt(InName, InValue); }
}

void UDreamNiagaraSystem::SetVariableBool(FName InName, bool bInValue)
{
	if (Component != nullptr) { Component->SetVariableBool(InName, bInValue); }
}

void UDreamNiagaraSystem::SetVariableVec2(FName InName, FVector2D InValue)
{
	if (Component != nullptr) { Component->SetVariableVec2(InName, InValue); }
}

void UDreamNiagaraSystem::SetVariableVec3(FName InName, FVector InValue)
{
	if (Component != nullptr) { Component->SetVariableVec3(InName, InValue); }
}

void UDreamNiagaraSystem::SetVariableLinearColor(FName InName, FLinearColor InValue)
{
	if (Component != nullptr) { Component->SetVariableLinearColor(InName, InValue); }
}

void UDreamNiagaraSystem::SetVariableObject(FName InName, UObject* InValue)
{
	if (Component != nullptr) { Component->SetVariableObject(InName, InValue); }
}

#if WITH_EDITOR
void UDreamNiagaraSystem::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UDreamNiagaraSystem, System)
		|| Name == GET_MEMBER_NAME_CHECKED(UDreamNiagaraSystem, FlightSystem)
		|| Name == GET_MEMBER_NAME_CHECKED(UDreamNiagaraSystem, RandomSeedOffset)
		|| Name == GET_MEMBER_NAME_CHECKED(UDreamNiagaraSystem, FixedTimeStep))
	{
		// The simulation is made again from what the properties say now.
		DestroyComponents();
		bSegmentsStale = true;
		EnsureComponent();
		MarkShadingChanged();
		return;
	}
	if (Name == GET_MEMBER_NAME_CHECKED(UDreamNiagaraSystem, bPaused)
		|| Name == GET_MEMBER_NAME_CHECKED(UDreamNiagaraSystem, TimeDilation)
		|| Name == GET_MEMBER_NAME_CHECKED(UDreamNiagaraSystem, bTickWhenGamePaused))
	{
		ConfigureTime(Component);
		for (const FBurst& Burst : Bursts)
		{
			ConfigureTime(Burst.Component);
		}
		return;
	}
	MarkShadingChanged();
}
#endif

DECLARE_DREAM_GUI_VISUAL("NiagaraSystem", UDreamNiagaraSystem)
