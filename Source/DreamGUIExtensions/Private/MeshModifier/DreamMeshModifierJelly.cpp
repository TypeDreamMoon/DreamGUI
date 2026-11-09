// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MeshModifier/DreamMeshModifierJelly.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"
#include "Event/DreamPointerEventData.h"

namespace DreamMeshModifierJellyLocal
{
	/** Offsets and speeds under this many local units (a second) count as none. */
	constexpr float RestTolerance = 0.01f;
	/** An acceleration past this (local units a second squared) is a jump, not a move: a widget placed somewhere at once. */
	constexpr float MaxAcceleration = 200000.0f;
}

void FDreamJellySprings::Reset(int32 InColumns, int32 InRows)
{
	Columns = FMath::Clamp(InColumns, FDreamMeshDeform::MinLatticeSize, FDreamMeshDeform::MaxLatticeSize);
	Rows = FMath::Clamp(InRows, FDreamMeshDeform::MinLatticeSize, FDreamMeshDeform::MaxLatticeSize);
	const int32 Count = Columns * Rows;
	Offset.Init(FVector2f::ZeroVector, Count);
	Velocity.Init(FVector2f::ZeroVector, Count);
	Target.Init(FVector2f::ZeroVector, Count);
	Weight.Init(1.0f, Count);
	Remainder = 0.0f;
}

void FDreamJellySprings::SetAnchor(EDreamJellyAnchor InAnchor)
{
	Weight.SetNum(Columns * Rows);
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			// The lattice's own measure: 0,0 top left, 1,1 bottom right.
			const float U = static_cast<float>(Column) / (Columns - 1);
			const float V = static_cast<float>(Row) / (Rows - 1);
			float Share = 1.0f;
			switch (InAnchor)
			{
			case EDreamJellyAnchor::Centre: Share = FVector2f(U - 0.5f, V - 0.5f).Size() / UE_INV_SQRT_2; break;
			case EDreamJellyAnchor::Bottom: Share = 1.0f - V; break;
			case EDreamJellyAnchor::Top: Share = V; break;
			case EDreamJellyAnchor::Left: Share = U; break;
			case EDreamJellyAnchor::Right: Share = 1.0f - U; break;
			case EDreamJellyAnchor::None: default: Share = 1.0f; break;
			}
			Weight[Row * Columns + Column] = FMath::Clamp(Share, 0.0f, 1.0f);
		}
	}
}

int32 FDreamJellySprings::Advance(float InDeltaSeconds, FVector2f InAcceleration)
{
	const int32 Count = Columns * Rows;
	if (InDeltaSeconds <= 0.0f || Offset.Num() != Count || Velocity.Num() != Count || Target.Num() != Count || Weight.Num() != Count)
	{
		return 0;
	}
	Remainder += InDeltaSeconds;
	int32 Steps = FMath::FloorToInt(Remainder / StepSeconds);
	if (Steps > MaxStepsPerAdvance)
	{
		Steps = MaxStepsPerAdvance;
		Remainder = 0.0f;
	}
	else
	{
		Remainder -= Steps * StepSeconds;
	}
	const float Omega = 2.0f * PI * FMath::Max(Frequency, 0.01f);
	const float Stiffness = Omega * Omega;
	const float Damping = 2.0f * FMath::Max(DampingRatio, 0.0f) * Omega;
	const float Limit = FMath::Max(MaxOffset, 0.0f);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// Semi-implicit Euler: the velocity first, then the offset from the new velocity.
			const FVector2f Pull = (Target[Index] - Offset[Index]) * Stiffness - Velocity[Index] * Damping - InAcceleration * Weight[Index];
			Velocity[Index] += Pull * StepSeconds;
			Offset[Index] += Velocity[Index] * StepSeconds;
			const float Length = Offset[Index].Size();
			if (Length > Limit && Length > 0.0f)
			{
				const FVector2f Out = Offset[Index] / Length;
				Offset[Index] = Out * Limit;
				const float Outward = FVector2f::DotProduct(Velocity[Index], Out);
				if (Outward > 0.0f)
				{
					Velocity[Index] -= Out * Outward;
				}
			}
		}
	}
	return Steps;
}

bool FDreamJellySprings::IsSettled(float InTolerance) const
{
	for (int32 Index = 0; Index < Offset.Num(); ++Index)
	{
		if (!Target.IsValidIndex(Index) || !Velocity.IsValidIndex(Index)
			|| (Offset[Index] - Target[Index]).Size() > InTolerance || Velocity[Index].Size() > InTolerance)
		{
			return false;
		}
	}
	return true;
}

bool FDreamJellySprings::IsAtRest(float InTolerance) const
{
	if (!IsSettled(InTolerance))
	{
		return false;
	}
	for (int32 Index = 0; Index < Offset.Num(); ++Index)
	{
		if (Offset[Index].Size() > InTolerance || Target[Index].Size() > InTolerance)
		{
			return false;
		}
	}
	return true;
}

void FDreamJellySprings::MakeLattice(FVector2f InMin, FVector2f InMax, TArray<FVector2D>& OutPoints) const
{
	FDreamMeshDeform::MakeRestLattice(Columns, Rows, OutPoints);
	const FVector2f Size(FMath::Max(InMax.X - InMin.X, KINDA_SMALL_NUMBER), FMath::Max(InMax.Y - InMin.Y, KINDA_SMALL_NUMBER));
	for (int32 Index = 0; Index < OutPoints.Num() && Index < Offset.Num(); ++Index)
	{
		// Local offsets have y up; the lattice's measure has it down.
		OutPoints[Index] += FVector2D(Offset[Index].X / Size.X, -Offset[Index].Y / Size.Y);
	}
}

UDreamMeshModifierJelly::UDreamMeshModifierJelly()
{
	DeclareTransformChangedUnused(StaticClass());
	Springs.Reset(Columns, Rows);
}

#if WITH_EDITOR
void UDreamMeshModifierJelly::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	ConfigureSprings();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamMeshModifierJelly::OnRegister()
{
	Super::OnRegister();
	ConfigureSprings();
	LocationSamples = 0;
}

void UDreamMeshModifierJelly::OnEnable()
{
	Super::OnEnable();
	// A widget switched back on may be somewhere else now; that is not a move to wobble at.
	LocationSamples = 0;
}

void UDreamMeshModifierJelly::ConfigureSprings()
{
	if (Springs.Columns != Columns || Springs.Rows != Rows || Springs.Offset.Num() != Columns * Rows)
	{
		Springs.Reset(Columns, Rows);
	}
	Columns = Springs.Columns;
	Rows = Springs.Rows;
	Springs.Frequency = Frequency;
	Springs.DampingRatio = DampingRatio;
	Springs.SetAnchor(Anchor);
	const FBox2f Box = GetBox(nullptr);
	Springs.MaxOffset = MaxStretch * (Box.bIsValid ? Box.GetSize().GetMax() : 0.0f);
}

FBox2f UDreamMeshModifierJelly::GetBox(const FDreamUIGeometry* InGeometry) const
{
	if (Bounds == EDreamWarpBounds::Geometry && InGeometry != nullptr)
	{
		return FDreamMeshDeform::MeasureBounds(*InGeometry);
	}
	if (const UDreamWidget* Widget = GetWidget())
	{
		return FBox2f(FVector2f(Widget->GetLocalSpaceLeft(), Widget->GetLocalSpaceBottom()), FVector2f(Widget->GetLocalSpaceRight(), Widget->GetLocalSpaceTop()));
	}
	return FBox2f(ForceInit);
}

FVector2f UDreamMeshModifierJelly::ToLocal(const FVector& InWorldPoint) const
{
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return FVector2f::ZeroVector;
	}
	const FVector Local = Widget->GetWorldTransform().InverseTransformPosition(InWorldPoint);
	return FVector2f(Local.Y, Local.Z);
}

void UDreamMeshModifierJelly::MarkDirty()
{
	if (UDreamVisualBatchMesh* Mesh = GetVisualBatchMesh())
	{
		Mesh->MarkVerticesDirty(true, true, true, true);
	}
}

void UDreamMeshModifierJelly::UpdateTargets()
{
	const FBox2f Box = GetBox(nullptr);
	const int32 Count = Springs.Columns * Springs.Rows;
	if (!Box.bIsValid || Springs.Target.Num() != Count)
	{
		return;
	}
	const FVector2f Size = Box.GetSize();
	const float Reach = FMath::Max(Size.GetMax() * 0.5f, KINDA_SMALL_NUMBER);
	for (int32 Row = 0; Row < Springs.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Springs.Columns; ++Column)
		{
			const int32 Index = Row * Springs.Columns + Column;
			const FVector2f Rest(Box.Min.X + Size.X * Column / (Springs.Columns - 1), Box.Max.Y - Size.Y * Row / (Springs.Rows - 1));
			FVector2f Target = FVector2f::ZeroVector;
			if (bPressed || bDragging)
			{
				Target += (PressPoint - Rest) * PressSquash;
			}
			if (bDragging)
			{
				const float Near = FVector2f::Distance(Rest, PressPoint) / Reach;
				Target += (DragPoint - PressPoint) * (DragStretch * FMath::Exp(-Near * Near));
			}
			Springs.Target[Index] = Target;
		}
	}
}

void UDreamMeshModifierJelly::Simulate(float InDeltaSeconds, FVector2f InAcceleration)
{
	ConfigureSprings();
	UpdateTargets();
	const bool bWasSettled = Springs.IsSettled(DreamMeshModifierJellyLocal::RestTolerance);
	Springs.Advance(InDeltaSeconds, InAcceleration);
	if (!bWasSettled || !Springs.IsSettled(DreamMeshModifierJellyLocal::RestTolerance) || bDrawnBent != !Springs.IsAtRest(DreamMeshModifierJellyLocal::RestTolerance))
	{
		MarkDirty();
	}
}

void UDreamMeshModifierJelly::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr || !GetEnable() || DeltaTime <= 0.0f)
	{
		return;
	}
	FVector2f Acceleration = FVector2f::ZeroVector;
	const FVector Location = Widget->GetWorldTransform().GetLocation();
	if (LocationSamples > 0)
	{
		const FVector WorldVelocity = (Location - LastLocation) / DeltaTime;
		if (LocationSamples > 1)
		{
			const FVector Local = Widget->GetWorldTransform().InverseTransformVector((WorldVelocity - LastVelocity) / DeltaTime);
			Acceleration = FVector2f(Local.Y, Local.Z) * Inertia;
			if (Acceleration.Size() > DreamMeshModifierJellyLocal::MaxAcceleration)
			{
				Acceleration = FVector2f::ZeroVector;
			}
		}
		LastVelocity = WorldVelocity;
	}
	LastLocation = Location;
	LocationSamples = FMath::Min(LocationSamples + 1, 2);
	if (Acceleration.IsNearlyZero() && !bPressed && !bDragging && Springs.IsAtRest(DreamMeshModifierJellyLocal::RestTolerance) && !bDrawnBent)
	{
		return;
	}
	Simulate(DeltaTime, Acceleration);
}

void UDreamMeshModifierJelly::Poke(FVector2D InVelocity)
{
	ConfigureSprings();
	for (int32 Index = 0; Index < Springs.Velocity.Num(); ++Index)
	{
		Springs.Velocity[Index] += FVector2f(InVelocity) * Springs.Weight[Index];
	}
	MarkDirty();
}

void UDreamMeshModifierJelly::SetPressed(bool bInPressed, FVector2D InLocalPoint)
{
	bPressed = bInPressed;
	if (bInPressed)
	{
		PressPoint = FVector2f(InLocalPoint);
		DragPoint = PressPoint;
	}
	MarkDirty();
}

void UDreamMeshModifierJelly::DragTo(FVector2D InLocalPoint)
{
	bDragging = true;
	DragPoint = FVector2f(InLocalPoint);
	MarkDirty();
}

void UDreamMeshModifierJelly::EndDrag()
{
	bDragging = false;
	MarkDirty();
}

void UDreamMeshModifierJelly::Settle()
{
	Springs.Reset(Columns, Rows);
	ConfigureSprings();
	bPressed = false;
	bDragging = false;
	MarkDirty();
}

bool UDreamMeshModifierJelly::IsAtRest() const
{
	return !bPressed && !bDragging && Springs.IsAtRest(DreamMeshModifierJellyLocal::RestTolerance);
}

bool UDreamMeshModifierJelly::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData != nullptr)
	{
		SetPressed(true, FVector2D(ToLocal(EventData->GetWorldPointInPlane())));
	}
	return true;
}

bool UDreamMeshModifierJelly::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	SetPressed(false, FVector2D::ZeroVector);
	return true;
}

bool UDreamMeshModifierJelly::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData != nullptr)
	{
		DragTo(FVector2D(ToLocal(EventData->GetWorldPointInPlane())));
	}
	return true;
}

bool UDreamMeshModifierJelly::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData != nullptr)
	{
		DragTo(FVector2D(ToLocal(EventData->GetWorldPointInPlane())));
	}
	return true;
}

bool UDreamMeshModifierJelly::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	EndDrag();
	return true;
}

void UDreamMeshModifierJelly::ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
{
	const bool bWorks = !Springs.IsAtRest(DreamMeshModifierJellyLocal::RestTolerance);
	OutTriangleIndices = bWorks;
	OutVertexPosition = bWorks;
	OutUV = bWorks;
	OutColor = bWorks;
}

void UDreamMeshModifierJelly::ModifyUIGeometry(FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged)
{
	bDrawnBent = false;
	if (Springs.IsAtRest(DreamMeshModifierJellyLocal::RestTolerance) || InGeometry.OriginVertices.Num() == 0)
	{
		return;
	}
	const FBox2f Box = GetBox(&InGeometry);
	if (!Box.bIsValid)
	{
		return;
	}
	TArray<FVector2D> Lattice;
	Springs.MakeLattice(Box.Min, Box.Max, Lattice);
	FDreamMeshDeform::FSubdivision Cut;
	Cut.CellLength = CellLength;
	Cut.MaxVertices = MaxVertices;
	FDreamMeshDeform::Subdivide(InGeometry, Cut);
	const FVector2f Min = Box.Min;
	const FVector2f Max = Box.Max;
	const int32 LatticeColumns = Springs.Columns;
	const int32 LatticeRows = Springs.Rows;
	FDreamMeshDeform::Deform(InGeometry, [&Lattice, LatticeColumns, LatticeRows, Min, Max](FVector2f P)
	{
		return FDreamMeshDeform::EvaluateLattice(Lattice, LatticeColumns, LatticeRows, P, Min, Max);
	});
	bDrawnBent = true;
}

void UDreamMeshModifierJelly::SetFrequency(float Value)
{
	Frequency = FMath::Max(Value, 0.1f);
	Springs.Frequency = Frequency;
}

void UDreamMeshModifierJelly::SetDampingRatio(float Value)
{
	DampingRatio = FMath::Clamp(Value, 0.0f, 2.0f);
	Springs.DampingRatio = DampingRatio;
}

void UDreamMeshModifierJelly::SetAnchor(EDreamJellyAnchor Value)
{
	Anchor = Value;
	Springs.SetAnchor(Anchor);
}

void UDreamMeshModifierJelly::SetLatticeSize(int32 InColumns, int32 InRows)
{
	Columns = InColumns;
	Rows = InRows;
	ConfigureSprings();
	MarkDirty();
}
