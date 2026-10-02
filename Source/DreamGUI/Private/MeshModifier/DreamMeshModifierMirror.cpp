// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MeshModifier/DreamMeshModifierMirror.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"

UDreamMeshModifierMirror::UDreamMeshModifierMirror()
{
	// A modifier works when the geometry is built, never each frame or when its widget moves.
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
}

void UDreamMeshModifierMirror::ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
{
	const bool bReflects = bMirrorHorizontally || bMirrorVertically;
	// Only the winding of a one-axis reflection changes; a reflection through both axes is a half turn.
	OutTriangleIndices = bMirrorHorizontally != bMirrorVertically;
	OutVertexPosition = bReflects;
	OutUV = false;
	OutColor = false;
}

void UDreamMeshModifierMirror::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	const UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return;
	}
	// The rect's centre, not the pivot: SBorder flips its brush about the middle of the box it draws in, wherever the
	// widget happens to be anchored from.
	const FVector2D Centre = Widget->GetLocalSpaceCenter();
	MirrorGeometry(InGeometry, FVector2f(static_cast<float>(Centre.X), static_cast<float>(Centre.Y)), bMirrorHorizontally, bMirrorVertically);
}

void UDreamMeshModifierMirror::MirrorGeometry(FDreamUIGeometry& InGeometry, const FVector2f& InCentre, bool bInHorizontally, bool bInVertically)
{
	if (!bInHorizontally && !bInVertically)
	{
		return;
	}
	for (FDreamUIOriginVertexData& Vertex : InGeometry.OriginVertices)
	{
		// The tangent runs along the texture's U, and U now runs the other way along whichever axis was reflected.
		if (bInHorizontally)
		{
			Vertex.Position.Y = 2.0f * InCentre.X - Vertex.Position.Y;
			Vertex.Tangent.Y = -Vertex.Tangent.Y;
		}
		if (bInVertically)
		{
			Vertex.Position.Z = 2.0f * InCentre.Y - Vertex.Position.Z;
			Vertex.Tangent.Z = -Vertex.Tangent.Z;
		}
	}
	if (bInHorizontally != bInVertically)
	{
		// One reflection turns every triangle over. Swapping two corners of each turns it back to face the way it was
		// built to, so a material that culls back faces still draws it.
		TArray<FDreamUIMeshIndex>& Triangles = InGeometry.Triangles;
		for (int32 Index = 0; Index + 2 < Triangles.Num(); Index += 3)
		{
			Swap(Triangles[Index + 1], Triangles[Index + 2]);
		}
	}
}

void UDreamMeshModifierMirror::SetMirrorHorizontally(bool Value)
{
	if (bMirrorHorizontally != Value)
	{
		bMirrorHorizontally = Value;
		// Fresh positions and fresh triangles: what this modifier had reflected and rewound has to be built again.
		if (UDreamVisualBatchMesh* Mesh = GetVisualBatchMesh())
		{
			Mesh->MarkVerticesDirty(true, true, false, false);
		}
	}
}

void UDreamMeshModifierMirror::SetMirrorVertically(bool Value)
{
	if (bMirrorVertically != Value)
	{
		bMirrorVertically = Value;
		if (UDreamVisualBatchMesh* Mesh = GetVisualBatchMesh())
		{
			Mesh->MarkVerticesDirty(true, true, false, false);
		}
	}
}
