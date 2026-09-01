// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamGUI/Public/MeshModifier/DreamMeshModifierPositionAsUV.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"


UDreamMeshModifierPositionAsUV::UDreamMeshModifierPositionAsUV()
{
}

void UDreamMeshModifierPositionAsUV::ModifyUIGeometry(
	FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged
)
{
	auto DreamVisual = GetVisualBatchMesh();
	if (!DreamVisual)return;
	// UIMin and UIMax are a slider hint, not a clamp: a typed entry or a Blueprint write can leave
	// anything in the byte, and the channel indexes a fixed-size array member. Doing nothing quietly
	// is the right answer rather than logging, because this runs once per rebuild and a complaint
	// here would repeat for as long as the widget is on screen.
	if (UVChannel >= LEXUI_VERTEX_TEXCOORDINATE_COUNT)return;

	// There used to be a render-canvas gate on channels 1 to 3 here, and it is gone rather than
	// extended to channel 0. It was never an availability check -- DreamGUI has no per-canvas shader
	// channel switch for a canvas to answer with, and the canvas it fetched was never read for
	// anything else -- and against the one documented canvas/UV relationship it pointed the wrong
	// way: UV1 belongs to the canvas (see DreamCanvas.h), so "there is a canvas" is precisely when
	// writing UV1 does damage, and that was the case the gate let through. It also could not fire in
	// the running pipeline, where a canvas is a precondition for geometry updating at all. What was
	// genuinely missing is below: the widget behind the visual was dereferenced unchecked.
	auto& originVertices = InGeometry.OriginVertices;
	auto& vertices = InGeometry.Vertices;
	// The position is read out of one array and the UV written into the other. A modifier earlier in
	// the list may have grown them by different amounts, so the shorter one is the bound.
	const int32 vertexCount = FMath::Min(vertices.Num(), originVertices.Num());
	for (int32 i = 0; i < vertexCount; i++)
	{
		const auto& vert = originVertices[i].Position;
		vertices[i].TextureCoordinate[UVChannel] = FVector2f(vert.Y, vert.Z) * Scale;
	}
}

void UDreamMeshModifierPositionAsUV::SetUVChannel(uint8 Value)
{
	if (UVChannel != Value)
	{
		// Marking the UV stream matters more here than it looks: the channel being abandoned still
		// holds the last position this modifier wrote into it, and only a rebuild puts the emitter's
		// own coordinate back.
		UVChannel = Value;
		if (auto Visual = GetVisualBatchMesh())Visual->MarkVertexUVDirty();
	}
}
void UDreamMeshModifierPositionAsUV::SetScale(FVector2f Value)
{
	if (Scale != Value)
	{
		Scale = Value;
		if (auto Visual = GetVisualBatchMesh())Visual->MarkVertexUVDirty();
	}
}