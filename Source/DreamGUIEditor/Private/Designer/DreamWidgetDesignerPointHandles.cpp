// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetDesignerViewportClient.h"
#include "DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Core/Components/DreamWidget.h"
#include "Extensions/DreamPath.h"
#include "MeshModifier/DreamMeshModifierWarp.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "SceneView.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDesignerPointHandles"

/*
 * The points of a warp's lattice and of a path, dragged in the designer's viewport. A drag moves the PREVIEW's object as
 * the pointer goes, so the bend follows the hand without a rebuild, and writes the property's whole array to the template
 * through the preview host -- the details panel's own route -- once when it starts (for undo) and once when it ends.
 */
namespace DreamDesignerPointsLocal
{
	/** How near the pointer has to be to a point to grab it, in viewport pixels. */
	constexpr double GrabRadius = 7.0;

	bool WorldToPixel(const FSceneView& InView, const FVector& InWorldPoint, FVector2D& OutPixel)
	{
		const FVector4 Screen = InView.WorldToScreen(InWorldPoint);
		if (Screen.W <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		return InView.ScreenToPixel(Screen, OutPixel);
	}

	FVector LocalToWorld(const UDreamWidget* InWidget, const FVector2D& InLocal)
	{
		return InWidget->GetWorldTransform().TransformPosition(FVector(0.0, InLocal.X, InLocal.Y));
	}

	FProperty* PointsProperty(bool bInLattice)
	{
		return bInLattice
			? FindFProperty<FProperty>(UDreamMeshModifierWarp::StaticClass(), TEXT("LatticePoints"))
			: FindFProperty<FProperty>(UDreamPath::StaticClass(), TEXT("Points"));
	}

	/** The dragged array written to the template, or only snapshotted there for undo with bInIsModify. */
	bool Migrate(FDreamWidgetBlueprintEditor* InDesigner, UObject* InOwner, bool bInLattice, bool bInIsModify)
	{
		FProperty* Property = PointsProperty(bInLattice);
		const TSharedPtr<FDreamWidgetPreviewHost> Host = InDesigner != nullptr ? InDesigner->GetPreviewHost() : nullptr;
		if (Property == nullptr || !Host.IsValid() || InOwner == nullptr)
		{
			return false;
		}
		FEditPropertyChain Chain;
		Chain.AddHead(Property);
		Chain.SetActivePropertyNode(Property);
		Chain.SetActiveMemberPropertyNode(Property);
		return Host->MigratePropertyToTemplate(InOwner, Chain, bInIsModify);
	}
}

void FDreamWidgetDesignerViewportClient::CollectDesignerPoints(TArray<FDesignerPoint>& OutPoints) const
{
	OutPoints.Reset();
	const TSharedPtr<FDreamWidgetBlueprintEditor> Designer = DesignerPtr.Pin();
	if (!Designer.IsValid() || Designer->GetSelectedWidgets().Num() != 1)
	{
		return;
	}
	UDreamWidget* Selected = Designer->GetSelectedWidgets()[0].Get();
	if (!IsValid(Selected) || Designer->IsWidgetLockedForInteraction(Selected))
	{
		return;
	}
	for (UDreamUIBehaviour* Component : Selected->GetComponents(UDreamMeshModifierWarp::StaticClass()))
	{
		const UDreamMeshModifierWarp* Warp = Cast<UDreamMeshModifierWarp>(Component);
		const FBox2f Box = Warp != nullptr ? Warp->GetLastWarpBox() : FBox2f(ForceInit);
		if (Warp == nullptr || !Warp->GetEnable() || Warp->GetMode() != EDreamWarpMode::Lattice || !Box.bIsValid
			|| Warp->GetLatticePoints().Num() != Warp->GetLatticeColumns() * Warp->GetLatticeRows())
		{
			continue;
		}
		const FVector2f Size = Box.GetSize();
		for (int32 Index = 0; Index < Warp->GetLatticePoints().Num(); ++Index)
		{
			const FVector2D& Point = Warp->GetLatticePoints()[Index];
			FDesignerPoint& Out = OutPoints.AddDefaulted_GetRef();
			Out.Owner = const_cast<UDreamMeshModifierWarp*>(Warp);
			Out.Widget = Selected;
			Out.Index = Index;
			Out.bLattice = true;
			Out.Local = FVector2D(Box.Min.X + Point.X * Size.X, Box.Max.Y - Point.Y * Size.Y);
		}
	}
	if (UDreamPath* Path = Cast<UDreamPath>(Selected->GetVisual()))
	{
		for (int32 Index = 0; Index < Path->GetPoints().Num(); ++Index)
		{
			FDesignerPoint& Out = OutPoints.AddDefaulted_GetRef();
			Out.Owner = Path;
			Out.Widget = Selected;
			Out.Index = Index;
			Out.bLattice = false;
			Out.Local = Path->GetPoints()[Index];
		}
	}
}

bool FDreamWidgetDesignerViewportClient::HitTestDesignerPoint(const FVector2D& InPixel, FDesignerPoint& OutPoint) const
{
	using namespace DreamDesignerPointsLocal;
	if (!Viewport)
	{
		return false;
	}
	TArray<FDesignerPoint> Points;
	CollectDesignerPoints(Points);
	if (Points.Num() == 0)
	{
		return false;
	}
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = const_cast<FDreamWidgetDesignerViewportClient*>(this)->CalcSceneView(&ViewFamily);
	if (View == nullptr)
	{
		return false;
	}
	double Nearest = GrabRadius;
	bool bFound = false;
	for (const FDesignerPoint& Point : Points)
	{
		FVector2D Pixel;
		if (Point.Widget.IsValid() && WorldToPixel(*View, LocalToWorld(Point.Widget.Get(), Point.Local), Pixel))
		{
			const double Distance = FVector2D::Distance(Pixel, InPixel);
			if (Distance <= Nearest)
			{
				Nearest = Distance;
				OutPoint = Point;
				bFound = true;
			}
		}
	}
	return bFound;
}

void FDreamWidgetDesignerViewportClient::BeginDesignerPointDrag(const FDesignerPoint& InPoint)
{
	using namespace DreamDesignerPointsLocal;
	UObject* Owner = InPoint.Owner.Get();
	if (Owner == nullptr || !InPoint.Widget.IsValid())
	{
		return;
	}
	DraggedDesignerPoint = InPoint;
	if (const UDreamMeshModifierWarp* Warp = Cast<UDreamMeshModifierWarp>(Owner))
	{
		DraggedPointsBefore = Warp->GetLatticePoints();
	}
	else if (const UDreamPath* Path = Cast<UDreamPath>(Owner))
	{
		DraggedPointsBefore = Path->GetPoints();
	}
	DesignerPointTransaction = MakeUnique<FScopedTransaction>(InPoint.bLattice
		? LOCTEXT("MoveLatticePoint", "Move Lattice Point")
		: LOCTEXT("MovePathPoint", "Move Path Point"));
	// The template's array as it is now goes into the undo entry before anything changes it.
	Migrate(DesignerPtr.Pin().Get(), Owner, InPoint.bLattice, true);
	bDesignerPointDragging = true;
	bDesignerPointChanged = false;
}

void FDreamWidgetDesignerViewportClient::UpdateDesignerPointDrag()
{
	UObject* Owner = DraggedDesignerPoint.Owner.Get();
	UDreamWidget* Held = DraggedDesignerPoint.Widget.Get();
	if (Owner == nullptr || Held == nullptr || !Viewport)
	{
		FinishDesignerPointDrag(true);
		return;
	}
	FVector World;
	if (!IntersectDesignerPlane(FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY()), Held->GetWorldTransform(), World))
	{
		return;
	}
	const FVector Local = Held->GetWorldTransform().InverseTransformPosition(World);
	const FVector2D Point(Local.Y, Local.Z);
	if (UDreamMeshModifierWarp* Warp = Cast<UDreamMeshModifierWarp>(Owner))
	{
		const FBox2f Box = Warp->GetLastWarpBox();
		if (!Box.bIsValid || Warp->GetLatticeColumns() <= 0)
		{
			return;
		}
		const FVector2f Size(FMath::Max(Box.GetSize().X, KINDA_SMALL_NUMBER), FMath::Max(Box.GetSize().Y, KINDA_SMALL_NUMBER));
		const FVector2D Measured((Point.X - Box.Min.X) / Size.X, (Box.Max.Y - Point.Y) / Size.Y);
		const int32 Index = DraggedDesignerPoint.Index;
		if (Warp->GetLatticePoints().IsValidIndex(Index) && !Warp->GetLatticePoints()[Index].Equals(Measured, 1.0e-5))
		{
			Warp->SetLatticePoint(Index % Warp->GetLatticeColumns(), Index / Warp->GetLatticeColumns(), Measured);
			bDesignerPointChanged = true;
		}
	}
	else if (UDreamPath* Path = Cast<UDreamPath>(Owner))
	{
		const int32 Index = DraggedDesignerPoint.Index;
		if (Path->GetPoints().IsValidIndex(Index) && !Path->GetPoints()[Index].Equals(Point, 1.0e-3))
		{
			Path->SetPoint(Index, Point);
			bDesignerPointChanged = true;
		}
	}
}

void FDreamWidgetDesignerViewportClient::FinishDesignerPointDrag(bool bCancel)
{
	using namespace DreamDesignerPointsLocal;
	UObject* Owner = DraggedDesignerPoint.Owner.Get();
	if (bCancel || !bDesignerPointChanged)
	{
		if (bDesignerPointChanged)
		{
			if (UDreamMeshModifierWarp* Warp = Cast<UDreamMeshModifierWarp>(Owner))
			{
				Warp->SetLatticePoints(DraggedPointsBefore);
			}
			else if (UDreamPath* Path = Cast<UDreamPath>(Owner))
			{
				Path->SetPoints(DraggedPointsBefore);
			}
		}
		if (DesignerPointTransaction.IsValid())
		{
			DesignerPointTransaction->Cancel();
		}
	}
	else if (Owner != nullptr)
	{
		Migrate(DesignerPtr.Pin().Get(), Owner, DraggedDesignerPoint.bLattice, false);
		FlushTemplateChangesNow();
	}
	DesignerPointTransaction.Reset();
	bDesignerPointDragging = false;
	bDesignerPointChanged = false;
	DraggedDesignerPoint = FDesignerPoint();
	DraggedPointsBefore.Reset();
}

void FDreamWidgetDesignerViewportClient::DrawDesignerPoints(FSceneView& View, FCanvas& Canvas) const
{
	using namespace DreamDesignerPointsLocal;
	TArray<FDesignerPoint> Points;
	CollectDesignerPoints(Points);
	if (Points.Num() == 0)
	{
		return;
	}
	const float DpiScale = Canvas.GetDPIScale();
	TArray<FVector2D> Pixels;
	TArray<bool> Visible;
	Pixels.SetNum(Points.Num());
	Visible.SetNum(Points.Num());
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		Visible[Index] = Points[Index].Widget.IsValid() && WorldToPixel(View, LocalToWorld(Points[Index].Widget.Get(), Points[Index].Local), Pixels[Index]);
		Pixels[Index] /= DpiScale;
	}
	auto Line = [&](int32 InA, int32 InB, const FLinearColor& InColour)
	{
		if (Points.IsValidIndex(InA) && Points.IsValidIndex(InB) && Visible[InA] && Visible[InB])
		{
			FCanvasLineItem Item(Pixels[InA], Pixels[InB]);
			Item.SetColor(InColour);
			Item.LineThickness = 1.0f;
			Canvas.DrawItem(Item);
		}
	};
	const FLinearColor LatticeColour(0.95f, 0.45f, 0.9f, 0.85f);
	const FLinearColor PathColour(0.3f, 0.9f, 1.0f, 0.85f);
	// The control net of each lattice: every point joined to the next across and the next down.
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		const FDesignerPoint& Point = Points[Index];
		if (Point.bLattice)
		{
			const UDreamMeshModifierWarp* Warp = Cast<UDreamMeshModifierWarp>(Point.Owner.Get());
			const int32 Columns = Warp != nullptr ? Warp->GetLatticeColumns() : 0;
			if (Columns > 0)
			{
				const int32 Column = Point.Index % Columns;
				if (Column + 1 < Columns)
				{
					Line(Index, Index + 1, LatticeColour);
				}
				if (Index + Columns < Points.Num() && Points[Index + Columns].Owner == Point.Owner)
				{
					Line(Index, Index + Columns, LatticeColour);
				}
			}
		}
		else if (Index + 1 < Points.Num() && !Points[Index + 1].bLattice)
		{
			Line(Index, Index + 1, PathColour);
		}
	}
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (!Visible[Index])
		{
			continue;
		}
		const bool bHeld = bDesignerPointDragging && DraggedDesignerPoint.Owner == Points[Index].Owner && DraggedDesignerPoint.Index == Points[Index].Index;
		const FVector2D Half(bHeld ? 5.0 : 4.0, bHeld ? 5.0 : 4.0);
		FCanvasTileItem Tile(Pixels[Index] - Half, Half * 2.0, bHeld ? FLinearColor::White : (Points[Index].bLattice ? LatticeColour : PathColour));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(Tile);
	}
}

#undef LOCTEXT_NAMESPACE
