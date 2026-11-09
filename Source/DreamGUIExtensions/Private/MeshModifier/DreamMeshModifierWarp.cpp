// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MeshModifier/DreamMeshModifierWarp.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"

UDreamMeshModifierWarp::UDreamMeshModifierWarp()
{
	DeclareTransformChangedUnused(StaticClass());
	FDreamMeshDeform::MakeRestLattice(LatticeColumns, LatticeRows, LatticePoints);
}

#if WITH_EDITOR
void UDreamMeshModifierWarp::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName Name = PropertyChangedEvent.GetMemberPropertyName();
	if (Name == GET_MEMBER_NAME_CHECKED(UDreamMeshModifierWarp, LatticeColumns) || Name == GET_MEMBER_NAME_CHECKED(UDreamMeshModifierWarp, LatticeRows))
	{
		FDreamMeshDeform::MakeRestLattice(LatticeColumns, LatticeRows, LatticePoints);
	}
	FitLattice();
	if (Name == GET_MEMBER_NAME_CHECKED(UDreamMeshModifierWarp, PhaseSpeed))
	{
		SetCanExecuteTick(PhaseSpeed != 0.0f);
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamMeshModifierWarp::OnRegister()
{
	Super::OnRegister();
	FitLattice();
	SetCanExecuteTick(PhaseSpeed != 0.0f);
}

void UDreamMeshModifierWarp::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const bool bWaves = Mode == EDreamWarpMode::Style && (Style == EDreamWarpStyle::Flag || Style == EDreamWarpStyle::Wave);
	if (PhaseSpeed != 0.0f && bWaves && GetEnable())
	{
		// Kept within one turn, so that a flag that waves for hours does not lose its precision.
		Phase = FMath::Frac(Phase + PhaseSpeed * DeltaTime);
		MarkDirty();
	}
}

void UDreamMeshModifierWarp::MarkDirty()
{
	if (UDreamVisualBatchMesh* Mesh = GetVisualBatchMesh())
	{
		// Everything: a warp at rest leaves the mesh as the visual made it, a bent one cuts it, and the visual rebuilds
		// in place from the memory the last update left.
		Mesh->MarkVerticesDirty(true, true, true, true);
	}
}

void UDreamMeshModifierWarp::FitLattice()
{
	LatticeColumns = FMath::Clamp(LatticeColumns, FDreamMeshDeform::MinLatticeSize, FDreamMeshDeform::MaxLatticeSize);
	LatticeRows = FMath::Clamp(LatticeRows, FDreamMeshDeform::MinLatticeSize, FDreamMeshDeform::MaxLatticeSize);
	if (LatticePoints.Num() != LatticeColumns * LatticeRows)
	{
		FDreamMeshDeform::MakeRestLattice(LatticeColumns, LatticeRows, LatticePoints);
	}
}

FDreamMeshDeform::FStyle UDreamMeshModifierWarp::MakeStyle() const
{
	FDreamMeshDeform::FStyle Out;
	Out.Style = Style;
	Out.bVertical = Orientation == EDreamWarpOrientation::Vertical;
	Out.Bend = Bend * 0.01f;
	Out.HorizontalDistortion = HorizontalDistortion * 0.01f;
	Out.VerticalDistortion = VerticalDistortion * 0.01f;
	Out.Phase = Phase;
	return Out;
}

bool UDreamMeshModifierWarp::IsAtRest() const
{
	if (Mode == EDreamWarpMode::Style)
	{
		return MakeStyle().IsIdentity();
	}
	if (LatticePoints.Num() != LatticeColumns * LatticeRows)
	{
		return true;
	}
	TArray<FVector2D> Rest;
	FDreamMeshDeform::MakeRestLattice(LatticeColumns, LatticeRows, Rest);
	for (int32 Index = 0; Index < Rest.Num(); ++Index)
	{
		if (!LatticePoints[Index].Equals(Rest[Index], 1.0e-6))
		{
			return false;
		}
	}
	return true;
}

FVector2f UDreamMeshModifierWarp::WarpPoint(FVector2f InLocalPoint, FVector2f InMin, FVector2f InMax) const
{
	if (Mode == EDreamWarpMode::Lattice)
	{
		return FDreamMeshDeform::EvaluateLattice(LatticePoints, LatticeColumns, LatticeRows, InLocalPoint, InMin, InMax);
	}
	return FDreamMeshDeform::WarpPoint(MakeStyle(), InLocalPoint, (InMin + InMax) * 0.5f, (InMax - InMin) * 0.5f);
}

FBox2f UDreamMeshModifierWarp::GetWarpBox(const FDreamUIGeometry& InGeometry) const
{
	const UDreamWidget* Widget = GetWidget();
	if (Bounds == EDreamWarpBounds::Widget && Widget != nullptr)
	{
		return FBox2f(FVector2f(Widget->GetLocalSpaceLeft(), Widget->GetLocalSpaceBottom()), FVector2f(Widget->GetLocalSpaceRight(), Widget->GetLocalSpaceTop()));
	}
	return FDreamMeshDeform::MeasureBounds(InGeometry);
}

void UDreamMeshModifierWarp::ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
{
	// The cut makes new vertices from every attribute of the old ones, so it needs all of them made afresh; at rest it
	// touches nothing, and a small text keeps its coverage glyphs.
	const bool bWorks = !IsAtRest();
	OutTriangleIndices = bWorks;
	OutVertexPosition = bWorks;
	OutUV = bWorks;
	OutColor = bWorks;
}

void UDreamMeshModifierWarp::ModifyUIGeometry(FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged)
{
	if (InGeometry.OriginVertices.Num() == 0)
	{
		return;
	}
	// Measured at rest too, so that a lattice nobody has moved yet can be drawn to be moved.
	const FBox2f Box = GetWarpBox(InGeometry);
	LastWarpBox = Box;
	if (IsAtRest() || !Box.bIsValid)
	{
		return;
	}
	FDreamMeshDeform::FSubdivision Cut;
	Cut.CellLength = CellLength;
	Cut.MaxVertices = MaxVertices;
	FDreamMeshDeform::Subdivide(InGeometry, Cut);
	const FVector2f Min = Box.Min;
	const FVector2f Max = Box.Max;
	if (Mode == EDreamWarpMode::Lattice)
	{
		FDreamMeshDeform::Deform(InGeometry, [this, Min, Max](FVector2f P)
		{
			return FDreamMeshDeform::EvaluateLattice(LatticePoints, LatticeColumns, LatticeRows, P, Min, Max);
		});
	}
	else
	{
		const FDreamMeshDeform::FStyle Warp = MakeStyle();
		const FVector2f Centre = (Min + Max) * 0.5f;
		const FVector2f HalfSize = (Max - Min) * 0.5f;
		FDreamMeshDeform::Deform(InGeometry, [&Warp, Centre, HalfSize](FVector2f P)
		{
			return FDreamMeshDeform::WarpPoint(Warp, P, Centre, HalfSize);
		});
	}
}

void UDreamMeshModifierWarp::SetMode(EDreamWarpMode Value) { if (Mode != Value) { Mode = Value; MarkDirty(); } }
void UDreamMeshModifierWarp::SetStyle(EDreamWarpStyle Value) { if (Style != Value) { Style = Value; MarkDirty(); } }
void UDreamMeshModifierWarp::SetOrientation(EDreamWarpOrientation Value) { if (Orientation != Value) { Orientation = Value; MarkDirty(); } }
void UDreamMeshModifierWarp::SetBend(float Value)
{
	Value = FMath::Clamp(Value, -100.0f, 100.0f);
	if (Bend != Value) { Bend = Value; MarkDirty(); }
}
void UDreamMeshModifierWarp::SetHorizontalDistortion(float Value)
{
	Value = FMath::Clamp(Value, -100.0f, 100.0f);
	if (HorizontalDistortion != Value) { HorizontalDistortion = Value; MarkDirty(); }
}
void UDreamMeshModifierWarp::SetVerticalDistortion(float Value)
{
	Value = FMath::Clamp(Value, -100.0f, 100.0f);
	if (VerticalDistortion != Value) { VerticalDistortion = Value; MarkDirty(); }
}
void UDreamMeshModifierWarp::SetPhase(float Value) { if (Phase != Value) { Phase = Value; MarkDirty(); } }
void UDreamMeshModifierWarp::SetPhaseSpeed(float Value)
{
	PhaseSpeed = Value;
	SetCanExecuteTick(PhaseSpeed != 0.0f);
}
void UDreamMeshModifierWarp::SetLatticeSize(int32 InColumns, int32 InRows)
{
	LatticeColumns = InColumns;
	LatticeRows = InRows;
	LatticePoints.Reset();
	FitLattice();
	MarkDirty();
}
void UDreamMeshModifierWarp::SetLatticePoints(const TArray<FVector2D>& Value)
{
	if (Value.Num() == LatticeColumns * LatticeRows)
	{
		LatticePoints = Value;
		MarkDirty();
	}
}
void UDreamMeshModifierWarp::SetLatticePoint(int32 InColumn, int32 InRow, FVector2D InPoint)
{
	FitLattice();
	if (InColumn >= 0 && InColumn < LatticeColumns && InRow >= 0 && InRow < LatticeRows)
	{
		LatticePoints[InRow * LatticeColumns + InColumn] = InPoint;
		MarkDirty();
	}
}
void UDreamMeshModifierWarp::ResetLattice()
{
	FDreamMeshDeform::MakeRestLattice(LatticeColumns, LatticeRows, LatticePoints);
	MarkDirty();
}
void UDreamMeshModifierWarp::SetBounds(EDreamWarpBounds Value) { if (Bounds != Value) { Bounds = Value; MarkDirty(); } }
void UDreamMeshModifierWarp::SetCellLength(float Value)
{
	Value = FMath::Max(Value, 1.0f);
	if (CellLength != Value) { CellLength = Value; MarkDirty(); }
}
void UDreamMeshModifierWarp::SetMaxVertices(int32 Value)
{
	Value = FMath::Clamp(Value, 64, 65535);
	if (MaxVertices != Value) { MaxVertices = Value; MarkDirty(); }
}
