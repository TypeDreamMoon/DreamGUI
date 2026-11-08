// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "MeshModifier/DreamMeshModifierTextOnPath.h"
#include "MeshModifier/DreamMeshDeform.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIGeometry.h"

UDreamMeshModifierTextOnPath::UDreamMeshModifierTextOnPath()
{
	DeclareTransformChangedUnused(StaticClass());
}

#if WITH_EDITOR
void UDreamMeshModifierTextOnPath::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	ListenToPath();
	UpdateTicking();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void UDreamMeshModifierTextOnPath::OnRegister()
{
	Super::OnRegister();
	ListenToPath();
	UpdateTicking();
}

void UDreamMeshModifierTextOnPath::OnUnregister()
{
	StopListening();
	Super::OnUnregister();
}

UDreamPath* UDreamMeshModifierTextOnPath::GetPath() const
{
	const UDreamWidget* Widget = PathWidget.Get();
	return Widget != nullptr ? Cast<UDreamPath>(Widget->GetVisual()) : nullptr;
}

void UDreamMeshModifierTextOnPath::StopListening()
{
	if (UDreamPath* Listened = ListenedPath.Get())
	{
		Listened->OnPathChanged.Remove(PathChangedHandle);
	}
	if (UDreamWidget* Moving = ListenedPathWidget.Get())
	{
		Moving->GetTransformChangedEvent().Remove(PathMovedHandle);
	}
	if (UDreamWidget* Own = ListenedOwnWidget.Get())
	{
		Own->GetTransformChangedEvent().Remove(OwnMovedHandle);
	}
	ListenedPath.Reset();
	ListenedPathWidget.Reset();
	ListenedOwnWidget.Reset();
	PathChangedHandle.Reset();
	PathMovedHandle.Reset();
	OwnMovedHandle.Reset();
}

void UDreamMeshModifierTextOnPath::ListenToPath()
{
	const bool bFollows = Source == EDreamTextPathSource::Path && bIsRegisteredWithWidget;
	UDreamPath* WantedPath = bFollows ? GetPath() : nullptr;
	UDreamWidget* WantedPathWidget = WantedPath != nullptr ? PathWidget.Get() : nullptr;
	UDreamWidget* WantedOwn = WantedPath != nullptr ? GetWidget() : nullptr;
	if (ListenedPath.Get() == WantedPath && ListenedPathWidget.Get() == WantedPathWidget && ListenedOwnWidget.Get() == WantedOwn)
	{
		return;
	}
	StopListening();
	if (WantedPath == nullptr)
	{
		return;
	}
	// A path that changes shape, or either widget moving, moves the path in this widget's space: the text is laid out again.
	ListenedPath = WantedPath;
	PathChangedHandle = WantedPath->OnPathChanged.AddWeakLambda(this, [this]() { MarkDirty(false); });
	ListenedPathWidget = WantedPathWidget;
	PathMovedHandle = WantedPathWidget->GetTransformChangedEvent().AddWeakLambda(this, [this]() { MarkDirty(false); });
	if (WantedOwn != nullptr && WantedOwn != WantedPathWidget)
	{
		ListenedOwnWidget = WantedOwn;
		OwnMovedHandle = WantedOwn->GetTransformChangedEvent().AddWeakLambda(this, [this]() { MarkDirty(false); });
	}
}

void UDreamMeshModifierTextOnPath::UpdateTicking()
{
	// Only scrolling needs every frame; a path widget is followed by its events.
	SetCanExecuteTick(OffsetSpeed != 0.0f);
}

void UDreamMeshModifierTextOnPath::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!GetEnable() || OffsetSpeed == 0.0f)
	{
		return;
	}
	StartOffset += OffsetSpeed * DeltaTime;
	FDreamPathCurve Curve;
	if (BuildPath(Curve) && Curve.bClosed && Curve.GetLength() > 0.0f)
	{
		StartOffset = FMath::Fmod(StartOffset, Curve.GetLength());
	}
	MarkDirty(false);
}

void UDreamMeshModifierTextOnPath::MarkDirty(bool bInAll)
{
	if (UDreamVisualBatchMesh* Mesh = GetVisualBatchMesh())
	{
		if (bInAll || bBendGlyphs || bHideOutside || Cast<UDreamText>(Mesh) == nullptr)
		{
			Mesh->MarkVerticesDirty(true, true, true, true);
		}
		else
		{
			Mesh->MarkVertexPositionDirty();
		}
	}
}

bool UDreamMeshModifierTextOnPath::GetPathToLocal(FMatrix& OutMatrix) const
{
	const UDreamWidget* Widget = GetWidget();
	const UDreamWidget* Other = PathWidget.Get();
	if (Widget == nullptr || Other == nullptr)
	{
		return false;
	}
	OutMatrix = Other->GetWorldTransform().ToMatrixWithScale() * Widget->GetWorldTransform().ToInverseMatrixWithScale();
	return true;
}

bool UDreamMeshModifierTextOnPath::BuildPath(FDreamPathCurve& OutCurve) const
{
	OutCurve.Reset();
	const UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return false;
	}
	if (Source == EDreamTextPathSource::Circle)
	{
		const FVector2f Centre = FVector2f(Widget->GetLocalSpaceCenter()) + FVector2f(CircleCentre);
		OutCurve.BuildCircle(Centre, Radius, StartAngle, SweepAngle, bClockwise);
		return OutCurve.IsValid();
	}
	UDreamPath* Path = GetPath();
	FMatrix PathToLocal;
	if (Path == nullptr || !GetPathToLocal(PathToLocal))
	{
		return false;
	}
	const FDreamPathCurve& Theirs = Path->GetCurve();
	TArray<FVector2f> Moved;
	Moved.Reserve(Theirs.Points.Num());
	for (const FVector2f& Point : Theirs.Points)
	{
		const FVector Local = PathToLocal.TransformPosition(FVector(0.0, Point.X, Point.Y));
		Moved.Add(FVector2f(Local.Y, Local.Z));
	}
	OutCurve.SetPolyline(MoveTemp(Moved), Theirs.bClosed);
	return OutCurve.IsValid();
}

void UDreamMeshModifierTextOnPath::ModifierWillChangeVertexData(bool& OutTriangleIndices, bool& OutVertexPosition, bool& OutUV, bool& OutColor)
{
	// Bent glyphs are cut into columns, which makes every attribute anew; whole glyphs only move (and may be hidden). What
	// is not a text is always bent.
	const bool bCuts = bBendGlyphs || Cast<UDreamText>(GetVisualBatchMesh()) == nullptr;
	OutTriangleIndices = bCuts;
	OutVertexPosition = true;
	OutUV = bCuts;
	OutColor = bCuts || bHideOutside;
}

void UDreamMeshModifierTextOnPath::SetSource(EDreamTextPathSource Value) { Source = Value; ListenToPath(); UpdateTicking(); MarkDirty(true); }
void UDreamMeshModifierTextOnPath::SetCircle(float InRadius, FVector2D InCentre, float InStartAngle, float InSweepAngle, bool bInClockwise)
{
	Radius = FMath::Max(InRadius, 0.0f);
	CircleCentre = InCentre;
	StartAngle = InStartAngle;
	SweepAngle = FMath::Clamp(InSweepAngle, 0.0f, 360.0f);
	bClockwise = bInClockwise;
	MarkDirty(false);
}
void UDreamMeshModifierTextOnPath::SetPathWidget(UDreamWidget* Value) { PathWidget = Value; ListenToPath(); MarkDirty(false); }
void UDreamMeshModifierTextOnPath::SetAlign(EDreamTextPathAlign Value) { Align = Value; MarkDirty(false); }
void UDreamMeshModifierTextOnPath::SetStartOffset(float Value) { if (StartOffset != Value) { StartOffset = Value; MarkDirty(false); } }
void UDreamMeshModifierTextOnPath::SetOffsetSpeed(float Value) { OffsetSpeed = Value; UpdateTicking(); }
void UDreamMeshModifierTextOnPath::SetLetterSpacing(float Value) { if (LetterSpacing != Value) { LetterSpacing = Value; MarkDirty(false); } }
void UDreamMeshModifierTextOnPath::SetBaselineOffset(float Value) { if (BaselineOffset != Value) { BaselineOffset = Value; MarkDirty(false); } }
void UDreamMeshModifierTextOnPath::SetAnchor(EDreamTextPathAnchor Value) { Anchor = Value; MarkDirty(false); }
void UDreamMeshModifierTextOnPath::SetFlip(bool Value) { bFlip = Value; MarkDirty(false); }
void UDreamMeshModifierTextOnPath::SetBendGlyphs(bool Value) { if (bBendGlyphs != Value) { bBendGlyphs = Value; MarkDirty(true); } }
void UDreamMeshModifierTextOnPath::SetHideOutside(bool Value) { if (bHideOutside != Value) { bHideOutside = Value; MarkDirty(true); } }

void UDreamMeshModifierTextOnPath::ModifyUIGeometry(FDreamUIGeometry& InGeometry, bool InTriangleChanged, bool InUVChanged, bool InColorChanged, bool InVertexPositionChanged)
{
	TArray<FDreamUIOriginVertexData>& Origins = InGeometry.OriginVertices;
	const int32 OriginalCount = Origins.Num();
	if (OriginalCount == 0)
	{
		return;
	}
	// The path widget's visual may have been replaced since: listen to the one there is.
	ListenToPath();
	FDreamPathCurve Curve;
	if (!BuildPath(Curve))
	{
		return;
	}
	const float Length = Curve.GetLength();

	// Which glyph each vertex belongs to, by the characters' vertex ranges; a mesh that is not a text is one glyph.
	TArray<int32> GlyphOf;
	GlyphOf.Init(InGeometry.bIsFont ? INDEX_NONE : 0, OriginalCount);
	int32 GlyphCount = InGeometry.bIsFont ? 0 : 1;
	const UDreamText* Text = Cast<UDreamText>(GetVisualBatchMesh());
	if (InGeometry.bIsFont && Text != nullptr)
	{
		for (const FDreamUITextCharProperty& Char : Text->GetCharPropertyArray())
		{
			if (Char.VertCount <= 0 || Char.StartVertIndex < 0 || Char.StartVertIndex + Char.VertCount > OriginalCount)
			{
				continue;
			}
			for (int32 Vertex = Char.StartVertIndex; Vertex < Char.StartVertIndex + Char.VertCount; ++Vertex)
			{
				GlyphOf[Vertex] = GlyphCount;
			}
			++GlyphCount;
		}
	}
	// What no character names (an underline, an image in rich text) is a glyph of its own, bent along the path.
	const bool bBend = bBendGlyphs || !InGeometry.bIsFont;
	if (bBend)
	{
		FDreamMeshDeform::FSubdivision Cut;
		Cut.CellLength = CellLength;
		Cut.bAcrossOnly = true;
		TArray<int32> SourceVertex;
		FDreamMeshDeform::Subdivide(InGeometry, Cut, &SourceVertex);
		const int32 Added = Origins.Num() - OriginalCount;
		for (int32 Index = 0; Index < Added; ++Index)
		{
			const int32 Glyph = GlyphOf[SourceVertex[OriginalCount + Index]];
			GlyphOf.Add(Glyph);
		}
	}

	// The run: from its leftmost ink to its rightmost, spaced out, and the line through it the path follows.
	const FBox2f Bounds = FDreamMeshDeform::MeasureBounds(InGeometry);
	TArray<FVector2f> GlyphSpan;
	GlyphSpan.Init(FVector2f(MAX_flt, -MAX_flt), FMath::Max(GlyphCount, 1));
	for (int32 Vertex = 0; Vertex < Origins.Num(); ++Vertex)
	{
		if (GlyphOf[Vertex] >= 0)
		{
			FVector2f& Span = GlyphSpan[GlyphOf[Vertex]];
			Span = FVector2f(FMath::Min(Span.X, Origins[Vertex].Position.Y), FMath::Max(Span.Y, Origins[Vertex].Position.Y));
		}
	}
	const float RunStart = Bounds.Min.X;
	const float Run = (Bounds.Max.X - Bounds.Min.X) + LetterSpacing * FMath::Max(GlyphCount - 1, 0);
	float Start = StartOffset;
	switch (Align)
	{
	case EDreamTextPathAlign::Start: break;
	case EDreamTextPathAlign::Centre: Start += (Length - Run) * 0.5f; break;
	case EDreamTextPathAlign::End: Start += Length - Run; break;
	}
	const float Line = Anchor == EDreamTextPathAnchor::Bottom ? Bounds.Min.Y : (Anchor == EDreamTextPathAnchor::Top ? Bounds.Max.Y : (Bounds.Min.Y + Bounds.Max.Y) * 0.5f);

	auto Place = [&](float InAlong, FVector2f& OutPoint, FVector2f& OutDirection)
	{
		if (bFlip)
		{
			Curve.Evaluate(Length - InAlong, OutPoint, OutDirection);
			OutDirection = -OutDirection;
		}
		else
		{
			Curve.Evaluate(InAlong, OutPoint, OutDirection);
		}
	};
	TArray<FDreamUIMeshVertex>& Vertices = InGeometry.Vertices;
	for (int32 Vertex = 0; Vertex < Origins.Num(); ++Vertex)
	{
		const int32 Glyph = GlyphOf[Vertex];
		FVector3f& Position = Origins[Vertex].Position;
		const float Spacing = LetterSpacing * FMath::Max(Glyph, 0);
		const bool bWhole = !bBend && Glyph >= 0;
		const float Pivot = bWhole ? (GlyphSpan[Glyph].X + GlyphSpan[Glyph].Y) * 0.5f : Position.Y;
		const float Along = Start + (Pivot - RunStart) + Spacing;
		FVector2f Point, Direction;
		Place(Along, Point, Direction);
		const FVector2f Up(-Direction.Y, Direction.X);
		const float Across = bWhole ? Position.Y - Pivot : 0.0f;
		const float Lift = Position.Z - Line + BaselineOffset;
		const FVector2f Placed = Point + Direction * Across + Up * Lift;
		// The tangent turns with the glyph.
		const FVector2f Tangent(Origins[Vertex].Tangent.Y, Origins[Vertex].Tangent.Z);
		const FVector2f TurnedTangent = Direction * Tangent.X + Up * Tangent.Y;
		Origins[Vertex].Tangent = FVector3f(Origins[Vertex].Tangent.X, TurnedTangent.X, TurnedTangent.Y);
		Position.Y = Placed.X;
		Position.Z = Placed.Y;
		if (bHideOutside && !Curve.bClosed && Vertices.IsValidIndex(Vertex))
		{
			// By the glyph's middle, so that a glyph goes out whole rather than column by column.
			const float Middle = Glyph >= 0 ? (GlyphSpan[Glyph].X + GlyphSpan[Glyph].Y) * 0.5f : Pivot;
			const float MiddleAlong = Start + (Middle - RunStart) + Spacing;
			if (MiddleAlong < 0.0f || MiddleAlong > Length)
			{
				Vertices[Vertex].Color.A = 0;
			}
		}
	}
}
