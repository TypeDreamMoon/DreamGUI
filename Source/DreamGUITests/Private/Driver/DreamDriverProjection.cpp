// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverProjection.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "UObject/UObjectIterator.h"

#include "Driver/DreamDriverVirtualCamera.h"

bool FDreamDriverProjection::IsProjectableCanvas(const UDreamCanvas* InRootCanvas)
{
	if (!IsValid(InRootCanvas))
	{
		return false;
	}
	const EDreamRenderMode RenderMode = InRootCanvas->GetActualRenderMode();
	return RenderMode == EDreamRenderMode::ScreenSpaceOverlay || RenderMode == EDreamRenderMode::RenderTarget;
}

TOptional<FVector2D> FDreamDriverProjection::WorldPointToPixel(const UDreamCanvas* InRootCanvas, const FVector& InWorldPoint)
{
	if (!IsProjectableCanvas(InRootCanvas))
	{
		return TOptional<FVector2D>();
	}
	const FIntPoint ViewportSize = InRootCanvas->GetViewportSize();
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)
	{
		return TOptional<FVector2D>();
	}

	const FVector4 Clip = InRootCanvas->GetViewProjectionMatrix().TransformFVector4(FVector4(InWorldPoint, 1.0));
	// At or behind the eye there is no pixel, only a mirrored one on the far side of the origin. The
	// same guard UDreamCanvas::ProjectWorldPointOntoCanvasPlane uses, for the same reason.
	if (Clip.W <= UE_KINDA_SMALL_NUMBER)
	{
		return TOptional<FVector2D>();
	}
	const FVector2D NDC(Clip.X / Clip.W, Clip.Y / Clip.W);
	const FVector2D ViewPoint01(NDC.X * 0.5 + 0.5, NDC.Y * 0.5 + 0.5);
	// The Y flip that GenerateRay applies on the way in, applied here on the way out. See the header.
	return FVector2D(ViewPoint01.X * ViewportSize.X, (1.0 - ViewPoint01.Y) * ViewportSize.Y);
}

TOptional<FVector2D> FDreamDriverProjection::WidgetLocalPointToPixel(const UDreamWidget* InWidget, const FVector2D& InLocalPoint)
{
	return WidgetLocalPointToPixel(InWidget, InLocalPoint, nullptr);
}

TOptional<FVector2D> FDreamDriverProjection::WidgetLocalPointToPixel(const UDreamWidget* InWidget, const FVector2D& InLocalPoint,
	const FDreamDriverVirtualCamera* InCamera)
{
	if (!IsValid(InWidget))
	{
		return TOptional<FVector2D>();
	}
	const UDreamCanvas* RootCanvas = InWidget->GetRootCanvas();
	if (!IsValid(RootCanvas))
	{
		return TOptional<FVector2D>();
	}
	// A widget's rect lies on its own local X = 0 plane, with the 2D X axis running along local Y and
	// the 2D Y axis along local Z. That is the same mapping UDreamVisual::LineTraceUIRect tests a ray
	// against and the same one UDreamCanvas::ProjectWorldPointOntoCanvasPlane builds its local point
	// with, so a point placed this way is a point the hit test agrees exists. For a world-space canvas
	// the transform is the real one -- the root follows the scene component it is attached to -- so
	// this is a world point in the ordinary sense; for the others it is a point in the canvas's own
	// space, which only that canvas's matrix gives a pixel to.
	const FVector WidgetPoint = InWidget->GetWorldTransform().TransformPosition(
		FVector(0.0, InLocalPoint.X, InLocalPoint.Y));

	if (InCamera != nullptr)
	{
		const EDreamRenderMode RenderMode = RootCanvas->GetActualRenderMode();
		if (RenderMode == EDreamRenderMode::WorldSpace || RenderMode == EDreamRenderMode::WorldSpace_DreamUI)
		{
			// Through the eye the world-space raycaster makes its rays from, and nothing else: the
			// canvas's own matrix describes a virtual screen a world-space canvas is never shown on.
			return InCamera->Project(WidgetPoint);
		}
		if (RenderMode == EDreamRenderMode::RenderTarget)
		{
			if (const UDreamUIRenderTargetGeometrySource* Surface = FindSurfaceShowing(RootCanvas))
			{
				// The render-target interaction's road, walked backwards. It turns a surface hit into a
				// UV, treats the UV as the canvas's view point and deprojects it with the canvas's matrix;
				// so the canvas's view point IS the UV, the surface point that carries that UV is where
				// the world ray must land, and the camera says which pixel sends it there. A surface that
				// shows this canvas but cannot say where a UV is (a static mesh) gets no pixel rather
				// than the canvas's own, which would be a pixel on a screen nobody is looking at.
				const TOptional<FVector2D> UV = WorldPointToViewPoint01(RootCanvas, WidgetPoint);
				if (!UV.IsSet())
				{
					return TOptional<FVector2D>();
				}
				const TOptional<FVector> OnSurface = RenderTargetUVToSurfacePoint(Surface, UV.GetValue());
				if (!OnSurface.IsSet())
				{
					return TOptional<FVector2D>();
				}
				return InCamera->Project(OnSurface.GetValue());
			}
		}
	}
	// The camera-less answer, unchanged: WorldPointToPixel refuses every canvas IsProjectableCanvas
	// refuses -- world-space ones among them -- before it reads a matrix.
	return WorldPointToPixel(RootCanvas, WidgetPoint);
}

TOptional<FBox2D> FDreamDriverProjection::WidgetToPixelRect(const UDreamWidget* InWidget)
{
	return WidgetToPixelRect(InWidget, nullptr);
}

TOptional<FBox2D> FDreamDriverProjection::WidgetToPixelRect(const UDreamWidget* InWidget, const FDreamDriverVirtualCamera* InCamera)
{
	if (!IsValid(InWidget))
	{
		return TOptional<FBox2D>();
	}
	const float Left = InWidget->GetLocalSpaceLeft();
	const float Right = InWidget->GetLocalSpaceRight();
	const float Bottom = InWidget->GetLocalSpaceBottom();
	const float Top = InWidget->GetLocalSpaceTop();

	const FVector2D LocalCorners[4] =
	{
		FVector2D(Left, Bottom),
		FVector2D(Right, Bottom),
		FVector2D(Left, Top),
		FVector2D(Right, Top),
	};

	FBox2D PixelBox(ForceInit);
	for (const FVector2D& LocalCorner : LocalCorners)
	{
		const TOptional<FVector2D> CornerPixel = WidgetLocalPointToPixel(InWidget, LocalCorner, InCamera);
		// All four or none: a box built from the corners that happened to be in front of the eye
		// would be a smaller box than the widget, which is worse than no answer.
		if (!CornerPixel.IsSet())
		{
			return TOptional<FBox2D>();
		}
		PixelBox += CornerPixel.GetValue();
	}
	return PixelBox;
}

TOptional<FVector2D> FDreamDriverProjection::WidgetCentrePixel(const UDreamWidget* InWidget)
{
	return WidgetCentrePixel(InWidget, nullptr);
}

TOptional<FVector2D> FDreamDriverProjection::WidgetCentrePixel(const UDreamWidget* InWidget, const FDreamDriverVirtualCamera* InCamera)
{
	if (!IsValid(InWidget))
	{
		return TOptional<FVector2D>();
	}
	return WidgetLocalPointToPixel(InWidget, InWidget->GetLocalSpaceCenter(), InCamera);
}

TOptional<FVector2D> FDreamDriverProjection::WorldPointToViewPoint01(const UDreamCanvas* InRootCanvas, const FVector& InWorldPoint)
{
	if (!IsValid(InRootCanvas))
	{
		return TOptional<FVector2D>();
	}
	const FVector4 Clip = InRootCanvas->GetViewProjectionMatrix().TransformFVector4(FVector4(InWorldPoint, 1.0));
	// The guard WorldPointToPixel has, for the reason it has it.
	if (Clip.W <= UE_KINDA_SMALL_NUMBER)
	{
		return TOptional<FVector2D>();
	}
	// No Y flip: DeprojectViewPointToWorld takes its view point with Y up, and so does the interaction
	// that feeds it a UV. See the header.
	return FVector2D(Clip.X / Clip.W * 0.5 + 0.5, Clip.Y / Clip.W * 0.5 + 0.5);
}

const UDreamUIRenderTargetGeometrySource* FDreamDriverProjection::FindSurfaceShowing(const UDreamCanvas* InRenderTargetCanvas)
{
	if (!IsValid(InRenderTargetCanvas))
	{
		return nullptr;
	}
	const UWorld* CanvasWorld = InRenderTargetCanvas->GetWorld();
	if (CanvasWorld == nullptr)
	{
		return nullptr;
	}
	// Every source, filtered to this world before it is asked anything: GetCanvas on a source that has
	// neither a canvas nor a presenter logs a warning, and sources in other worlds (an editor level, a
	// second test's leftovers awaiting GC) are none of this canvas's business. The iterator already
	// skips class default objects.
	for (TObjectIterator<UDreamUIRenderTargetGeometrySource> SurfaceIt; SurfaceIt; ++SurfaceIt)
	{
		const UDreamUIRenderTargetGeometrySource* Candidate = *SurfaceIt;
		if (!IsValid(Candidate) || !Candidate->IsRegistered() || Candidate->GetWorld() != CanvasWorld)
		{
			continue;
		}
		if (Candidate->GetCanvas() == InRenderTargetCanvas)
		{
			return Candidate;
		}
	}
	return nullptr;
}

TOptional<FVector> FDreamDriverProjection::RenderTargetUVToSurfacePoint(const UDreamUIRenderTargetGeometrySource* InSurface, const FVector2D& InUV)
{
	if (!IsValid(InSurface))
	{
		return TOptional<FVector>();
	}
	// Plane and cylinder are both built by UpdateMeshData as a triangle list with the render target's
	// coordinates in texture channel 0. A static mesh is the host mesh's own geometry, whose UVs are in
	// its render data; there is nothing here to search.
	const EDreamUIRenderTargetGeometryMode Mode = InSurface->GetGeometryMode();
	if (Mode != EDreamUIRenderTargetGeometryMode::Plane && Mode != EDreamUIRenderTargetGeometryMode::Cylinder)
	{
		return TOptional<FVector>();
	}
	const TArray<FDynamicMeshVertex>& Vertices = InSurface->GetMeshVertices();
	const TArray<uint16> Indices = InSurface->GetMeshIndices();

	// The mesh stores V running DOWN the texture (its top edge carries V = 0), while LineTraceHitUV --
	// and so the UV the interaction deprojects -- measures V UP from the bottom edge.
	const FVector2D Wanted(InUV.X, 1.0 - InUV.Y);
	// A UV exactly on a shared edge belongs to both triangles; the slack keeps it from belonging to
	// neither through rounding.
	constexpr double EdgeSlack = 1.0e-6;
	for (int32 Corner = 0; Corner + 2 < Indices.Num(); Corner += 3)
	{
		const int32 IndexA = Indices[Corner];
		const int32 IndexB = Indices[Corner + 1];
		const int32 IndexC = Indices[Corner + 2];
		if (!Vertices.IsValidIndex(IndexA) || !Vertices.IsValidIndex(IndexB) || !Vertices.IsValidIndex(IndexC))
		{
			continue;
		}
		const FVector2D TexA(Vertices[IndexA].TextureCoordinate[0]);
		const FVector2D TexB(Vertices[IndexB].TextureCoordinate[0]);
		const FVector2D TexC(Vertices[IndexC].TextureCoordinate[0]);

		// Barycentric weights in texture space, solved directly rather than through
		// FMath::ComputeBaryCentric2D, which checks (asserts) on a degenerate triangle instead of
		// letting the caller skip it.
		const FVector2D EdgeB = TexB - TexA;
		const FVector2D EdgeC = TexC - TexA;
		const FVector2D ToWanted = Wanted - TexA;
		const double Denominator = EdgeB.X * EdgeC.Y - EdgeC.X * EdgeB.Y;
		if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
		{
			continue;
		}
		const double WeightB = (ToWanted.X * EdgeC.Y - EdgeC.X * ToWanted.Y) / Denominator;
		const double WeightC = (EdgeB.X * ToWanted.Y - ToWanted.X * EdgeB.Y) / Denominator;
		const double WeightA = 1.0 - WeightB - WeightC;
		if (WeightA < -EdgeSlack || WeightB < -EdgeSlack || WeightC < -EdgeSlack)
		{
			continue;
		}
		const FVector LocalPoint =
			FVector(Vertices[IndexA].Position) * WeightA
			+ FVector(Vertices[IndexB].Position) * WeightB
			+ FVector(Vertices[IndexC].Position) * WeightC;
		return InSurface->GetComponentTransform().TransformPosition(LocalPoint);
	}
	return TOptional<FVector>();
}
