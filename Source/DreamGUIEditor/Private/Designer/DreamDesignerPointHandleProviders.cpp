// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerPointHandleProviders.h"

#include "Core/Components/DreamWidget.h"
#include "Designer/DreamDesignerHandles.h"
#include "Extensions/DreamPath.h"
#include "MeshModifier/DreamMeshModifierWarp.h"

#define LOCTEXT_NAMESPACE "DreamDesignerPointHandles"

/*
 * The points of a warp's lattice and of a path, as viewport handles: a lattice's point a handle per control point, joined to
 * the next across and the next down; a path's a handle per point, joined in order.
 */
namespace DreamDesignerPointHandlesLocal
{
	/** A warp's handle: which of the widget's warps it is, and which of its points, in one number. */
	constexpr int32 PointsPerWarp = 4096;

	TArray<UDreamMeshModifierWarp*> GetLatticeWarps(UDreamWidget* InWidget)
	{
		TArray<UDreamMeshModifierWarp*> Warps;
		for (UDreamUIBehaviour* Component : InWidget->GetComponents(UDreamMeshModifierWarp::StaticClass()))
		{
			Warps.Add(Cast<UDreamMeshModifierWarp>(Component));
		}
		return Warps;
	}

	/** A warp whose lattice is drawn: on, in lattice mode, its box measured and its points all there. */
	bool IsLatticeShown(const UDreamMeshModifierWarp* InWarp)
	{
		return InWarp != nullptr && InWarp->GetEnable() && InWarp->GetMode() == EDreamWarpMode::Lattice && InWarp->GetLastWarpBox().bIsValid
			&& InWarp->GetLatticePoints().Num() == InWarp->GetLatticeColumns() * InWarp->GetLatticeRows();
	}

	/** A lattice point (0..1 across its box from the left, 0..1 down from the top) in the widget's local space, and back. */
	FVector2D LatticeToLocal(const UDreamMeshModifierWarp* InWarp, const FVector2D& InPoint)
	{
		const FBox2f Box = InWarp->GetLastWarpBox();
		const FVector2f Size = Box.GetSize();
		return FVector2D(Box.Min.X + InPoint.X * Size.X, Box.Max.Y - InPoint.Y * Size.Y);
	}

	FVector2D LocalToLattice(const UDreamMeshModifierWarp* InWarp, const FVector2D& InLocal)
	{
		const FBox2f Box = InWarp->GetLastWarpBox();
		const FVector2f Size(FMath::Max(Box.GetSize().X, KINDA_SMALL_NUMBER), FMath::Max(Box.GetSize().Y, KINDA_SMALL_NUMBER));
		return FVector2D((InLocal.X - Box.Min.X) / Size.X, (Box.Max.Y - InLocal.Y) / Size.Y);
	}

	const FLinearColor LatticeColour(0.95f, 0.45f, 0.9f, 0.85f);
	const FLinearColor PathColour(0.3f, 0.9f, 1.0f, 0.85f);

	class FLatticeHandles : public IDreamDesignerHandleProvider
	{
	public:
		virtual FName GetName() const override { return DreamDesignerPointHandles::LatticeProviderName; }

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			const TArray<UDreamMeshModifierWarp*> Warps = GetLatticeWarps(InWidget);
			for (int32 WarpIndex = 0; WarpIndex < Warps.Num(); ++WarpIndex)
			{
				const UDreamMeshModifierWarp* Warp = Warps[WarpIndex];
				if (!IsLatticeShown(Warp))
				{
					continue;
				}
				const int32 Columns = Warp->GetLatticeColumns();
				const TArray<FVector2D>& Points = Warp->GetLatticePoints();
				for (int32 Index = 0; Index < Points.Num(); ++Index)
				{
					const FVector2D Local = LatticeToLocal(Warp, Points[Index]);
					FDreamDesignerHandle& Handle = OutSet.AddHandle(WarpIndex * PointsPerWarp + Index, Local);
					Handle.Outline = LatticeColour;
					// The control net: every point joined to the next across and the next down.
					if (Index % Columns + 1 < Columns)
					{
						OutSet.AddLine(Local, LatticeToLocal(Warp, Points[Index + 1]), LatticeColour);
					}
					if (Index + Columns < Points.Num())
					{
						OutSet.AddLine(Local, LatticeToLocal(Warp, Points[Index + Columns]), LatticeColour);
					}
				}
			}
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			if (UDreamMeshModifierWarp* Warp = FindWarp(InWidget, InHandleId))
			{
				OutProperties.Emplace(Warp, TEXT("LatticePoints"));
			}
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			return LOCTEXT("MoveLatticePoint", "Move Lattice Point");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			UDreamMeshModifierWarp* Warp = FindWarp(InWidget, InDrag.HandleId);
			const int32 Index = InDrag.HandleId % PointsPerWarp;
			if (Warp == nullptr || !Warp->GetLatticePoints().IsValidIndex(Index) || Warp->GetLatticeColumns() <= 0)
			{
				return false;
			}
			const FVector2D Measured = LocalToLattice(Warp, InDrag.GetTarget());
			if (Warp->GetLatticePoints()[Index].Equals(Measured, 1.0e-5))
			{
				return false;
			}
			Warp->SetLatticePoint(Index % Warp->GetLatticeColumns(), Index / Warp->GetLatticeColumns(), Measured);
			return true;
		}

	private:
		static UDreamMeshModifierWarp* FindWarp(UDreamWidget* InWidget, int32 InHandleId)
		{
			if (InWidget == nullptr || InHandleId < 0)
			{
				return nullptr;
			}
			const TArray<UDreamMeshModifierWarp*> Warps = GetLatticeWarps(InWidget);
			const int32 WarpIndex = InHandleId / PointsPerWarp;
			return Warps.IsValidIndex(WarpIndex) && IsLatticeShown(Warps[WarpIndex]) ? Warps[WarpIndex] : nullptr;
		}
	};

	class FPathHandles : public IDreamDesignerHandleProvider
	{
	public:
		virtual FName GetName() const override { return DreamDesignerPointHandles::PathProviderName; }

		virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const override
		{
			const UDreamPath* Path = Cast<UDreamPath>(InWidget->GetVisual());
			if (Path == nullptr)
			{
				return;
			}
			const TArray<FVector2D>& Points = Path->GetPoints();
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				OutSet.AddHandle(Index, Points[Index]).Outline = PathColour;
			}
			OutSet.AddPolyline(Points, /*bInClosed*/ false, PathColour);
		}

		virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const override
		{
			if (UDreamPath* Path = Cast<UDreamPath>(InWidget->GetVisual()))
			{
				if (InHandleId != INDEX_NONE)
				{
					OutProperties.Emplace(Path, TEXT("Points"));
				}
			}
		}

		virtual FText GetTransactionName(int32 InHandleId) const override
		{
			return LOCTEXT("MovePathPoint", "Move Path Point");
		}

		virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) override
		{
			UDreamPath* Path = Cast<UDreamPath>(InWidget->GetVisual());
			if (Path == nullptr || !Path->GetPoints().IsValidIndex(InDrag.HandleId))
			{
				return false;
			}
			const FVector2D Target = InDrag.GetTarget();
			if (Path->GetPoints()[InDrag.HandleId].Equals(Target, 1.0e-3))
			{
				return false;
			}
			Path->SetPoint(InDrag.HandleId, Target);
			return true;
		}
	};
}

const FName DreamDesignerPointHandles::LatticeProviderName(TEXT("WarpLattice"));
const FName DreamDesignerPointHandles::PathProviderName(TEXT("PathPoints"));

TSharedRef<IDreamDesignerHandleProvider> DreamDesignerPointHandles::MakeLatticeProvider()
{
	return MakeShared<DreamDesignerPointHandlesLocal::FLatticeHandles>();
}

TSharedRef<IDreamDesignerHandleProvider> DreamDesignerPointHandles::MakePathProvider()
{
	return MakeShared<DreamDesignerPointHandlesLocal::FPathHandles>();
}

#undef LOCTEXT_NAMESPACE
