// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIWidgetGeometryLibrary.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace DreamUIWidgetGeometryLocal
{
	/**
	 * A point in the widget's own 2D space (origin at its bottom-left corner) as a world point.
	 *
	 * The 3D mapping is (0, x, y) and not (x, y, 0): local X is DEPTH in this framework -- it points
	 * away from the viewer -- so the plane a widget's rect lies in is YZ. The vertex transform in
	 * FDreamUIGeometry::TransformVertices reads the same two components back out, which is what
	 * makes this the same space the widget is actually drawn in.
	 */
	FVector LocalToWorld(const UDreamWidget* InWidget, const FVector2D& InLocal)
	{
		const FVector2D BottomLeft = InWidget->GetLocalSpaceLeftBottomPoint();
		return InWidget->GetWorldTransform().TransformPosition(
			FVector(0.0, BottomLeft.X + InLocal.X, BottomLeft.Y + InLocal.Y));
	}

	/** The inverse, for a point already known to lie in the widget's own plane. */
	FVector2D WorldToLocal(const UDreamWidget* InWidget, const FVector& InWorld)
	{
		const FVector InWidgetSpace = InWidget->GetWorldTransform().Inverse().TransformPosition(InWorld);
		const FVector2D BottomLeft = InWidget->GetLocalSpaceLeftBottomPoint();
		return FVector2D(InWidgetSpace.Y - BottomLeft.X, InWidgetSpace.Z - BottomLeft.Y);
	}

	/**
	 * A world point in viewport pixels, top-left zero.
	 *
	 * Two different questions behind one name. Screen-space and render-target UI are drawn by the
	 * canvas's own virtual camera, so the canvas is the only thing that can say where a point of its
	 * own lands -- Project3DToScreen asks exactly that, and then the scaler converts canvas units to
	 * pixels. World-space UI is drawn by the PLAYER's camera and the canvas's virtual one says
	 * nothing about the shipped image, so the player controller has to answer instead.
	 *
	 * False rather than a guess when neither can answer: no canvas, behind the eye, a world-space
	 * widget with no local player to project through -- or UI drawn into a RENDER TARGET, which is
	 * not on the viewport at all and for which "viewport pixels" names nothing.
	 */
	bool WorldToViewportPixels(const UDreamWidget* InWidget, const FVector& InWorld, FVector2D& OutPixel)
	{
		UDreamCanvas* RootCanvas = InWidget->GetRootCanvas();
		if (!IsValid(RootCanvas))
		{
			return false;
		}
		if (RootCanvas->IsRenderToScreenSpace())
		{
			FVector2D CanvasPosition = FVector2D::ZeroVector;
			if (!RootCanvas->Project3DToScreen(InWorld, CanvasPosition))
			{
				return false;
			}
			return RootCanvas->ConvertPositionFromCanvasToViewport(CanvasPosition, OutPixel);
		}
		if (!RootCanvas->IsRenderToWorldSpace())
		{
			return false;
		}
		APlayerController* PlayerController = InWidget->GetOwningPlayer();
		if (!IsValid(PlayerController))
		{
			return false;
		}
		return UGameplayStatics::ProjectWorldToScreen(PlayerController, InWorld, OutPixel, false);
	}

	/**
	 * Viewport pixels back to a point on the widget's own plane.
	 *
	 * Only meaningful for screen-space UI, where the canvas scaler is invertible and the widget lies
	 * in the canvas plane; that covers every 2D interface. A world-space canvas would need a ray
	 * against the widget's plane, which is what the raycaster already does properly and what a
	 * coordinate helper has no business approximating.
	 */
	bool ViewportPixelsToWorldOnCanvasPlane(const UDreamWidget* InWidget, const FVector2D& InPixel, FVector& OutWorld)
	{
		UDreamCanvas* RootCanvas = InWidget->GetRootCanvas();
		if (!IsValid(RootCanvas) || !RootCanvas->IsRenderToScreenSpace())
		{
			return false;
		}
		UDreamWidget* CanvasWidget = RootCanvas->GetWidget();
		if (!IsValid(CanvasWidget))
		{
			return false;
		}
		FVector2D CanvasPosition = FVector2D::ZeroVector;
		if (!RootCanvas->ConvertPositionFromViewportToCanvas(InPixel, CanvasPosition))
		{
			return false;
		}
		// Canvas space has its zero at the canvas's bottom-left corner; the canvas widget's own local
		// origin is at its pivot, so the two differ by exactly that corner.
		const FVector2D CanvasBottomLeft = CanvasWidget->GetLocalSpaceLeftBottomPoint();
		OutWorld = CanvasWidget->GetWorldTransform().TransformPosition(
			FVector(0.0, CanvasBottomLeft.X + CanvasPosition.X, CanvasBottomLeft.Y + CanvasPosition.Y));
		return true;
	}

	/** The project's DPI scale for the current viewport. Matches UMG's GetViewportScale. */
	float GetViewportScale(const UObject* WorldContextObject)
	{
		const UWorld* World = GEngine != nullptr
			? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)
			: nullptr;
		if (World == nullptr || World->GetGameViewport() == nullptr)
		{
			return 1.0f;
		}
		FVector2D ViewportSize = FVector2D::ZeroVector;
		World->GetGameViewport()->GetViewportSize(ViewportSize);
		return GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(
			FIntPoint(FMath::TruncToInt(ViewportSize.X), FMath::TruncToInt(ViewportSize.Y)));
	}

	/** The widget behind a geometry, or null. Every entry point starts here. */
	UDreamWidget* Resolve(const FDreamUIWidgetGeometry& InGeometry)
	{
		UDreamWidget* Widget = InGeometry.Widget.Get();
		return IsValid(Widget) ? Widget : nullptr;
	}
}

FDreamUIWidgetGeometry UDreamUIWidgetGeometryLibrary::GetWidgetGeometry(UDreamWidget* InWidget)
{
	using namespace DreamUIWidgetGeometryLocal;

	FDreamUIWidgetGeometry Geometry;
	if (!IsValid(InWidget))
	{
		return Geometry;
	}
	Geometry.Widget = InWidget;
	Geometry.LocalSize = InWidget->GetSize();

	// The rect's two opposite corners in pixels, which is what gives both a position and a size --
	// and what makes the size come out right when a canvas scaler, a render scale and an ancestor's
	// scale are all in play, because every one of them is already inside the projection.
	FVector2D BottomLeftPixel = FVector2D::ZeroVector;
	FVector2D TopRightPixel = FVector2D::ZeroVector;
	const bool bHasCorners =
		WorldToViewportPixels(InWidget, LocalToWorld(InWidget, FVector2D::ZeroVector), BottomLeftPixel)
		&& WorldToViewportPixels(InWidget, LocalToWorld(InWidget, Geometry.LocalSize), TopRightPixel);
	if (bHasCorners)
	{
		// Viewport Y grows downward while local Y grows upward, so the rect's local bottom-left is
		// its pixel TOP-left. Min/Max rather than assuming, because a mirrored or rotated widget can
		// swap either axis.
		Geometry.AbsolutePosition = FVector2D(
			FMath::Min(BottomLeftPixel.X, TopRightPixel.X), FMath::Min(BottomLeftPixel.Y, TopRightPixel.Y));
		Geometry.AbsoluteSize = FVector2D(
			FMath::Abs(TopRightPixel.X - BottomLeftPixel.X), FMath::Abs(TopRightPixel.Y - BottomLeftPixel.Y));
		Geometry.bHasAbsolute = true;
	}
	return Geometry;
}

FVector2D UDreamUIWidgetGeometryLibrary::GetLocalSize(const FDreamUIWidgetGeometry& InGeometry)
{
	return InGeometry.LocalSize;
}

FVector2D UDreamUIWidgetGeometryLibrary::GetLocalTopLeft(const FDreamUIWidgetGeometry& InGeometry)
{
	return FVector2D::ZeroVector;
}

bool UDreamUIWidgetGeometryLibrary::GetAbsoluteSize(const FDreamUIWidgetGeometry& InGeometry, FVector2D& OutAbsoluteSize)
{
	OutAbsoluteSize = InGeometry.bHasAbsolute ? InGeometry.AbsoluteSize : FVector2D::ZeroVector;
	return InGeometry.bHasAbsolute;
}

bool UDreamUIWidgetGeometryLibrary::LocalToAbsolute(const FDreamUIWidgetGeometry& InGeometry,
	FVector2D InLocalCoordinate, FVector2D& OutAbsoluteCoordinate)
{
	using namespace DreamUIWidgetGeometryLocal;

	OutAbsoluteCoordinate = FVector2D::ZeroVector;
	UDreamWidget* Widget = Resolve(InGeometry);
	if (Widget == nullptr)
	{
		return false;
	}
	return WorldToViewportPixels(Widget, LocalToWorld(Widget, InLocalCoordinate), OutAbsoluteCoordinate);
}

bool UDreamUIWidgetGeometryLibrary::AbsoluteToLocal(const FDreamUIWidgetGeometry& InGeometry,
	FVector2D InAbsoluteCoordinate, FVector2D& OutLocalCoordinate)
{
	using namespace DreamUIWidgetGeometryLocal;

	OutLocalCoordinate = FVector2D::ZeroVector;
	UDreamWidget* Widget = Resolve(InGeometry);
	if (Widget == nullptr)
	{
		return false;
	}
	FVector WorldOnPlane = FVector::ZeroVector;
	if (!ViewportPixelsToWorldOnCanvasPlane(Widget, InAbsoluteCoordinate, WorldOnPlane))
	{
		return false;
	}
	OutLocalCoordinate = WorldToLocal(Widget, WorldOnPlane);
	return true;
}

bool UDreamUIWidgetGeometryLibrary::LocalToViewport(UObject* WorldContextObject,
	const FDreamUIWidgetGeometry& InGeometry, FVector2D InLocalCoordinate,
	FVector2D& OutPixelPosition, FVector2D& OutViewportPosition)
{
	OutPixelPosition = FVector2D::ZeroVector;
	OutViewportPosition = FVector2D::ZeroVector;
	if (!LocalToAbsolute(InGeometry, InLocalCoordinate, OutPixelPosition))
	{
		return false;
	}
	const float Scale = DreamUIWidgetGeometryLocal::GetViewportScale(WorldContextObject);
	OutViewportPosition = Scale > UE_KINDA_SMALL_NUMBER ? OutPixelPosition / Scale : OutPixelPosition;
	return true;
}

bool UDreamUIWidgetGeometryLibrary::AbsoluteToViewport(UObject* WorldContextObject,
	FVector2D InAbsoluteCoordinate, FVector2D& OutPixelPosition, FVector2D& OutViewportPosition)
{
	OutPixelPosition = InAbsoluteCoordinate;
	const float Scale = DreamUIWidgetGeometryLocal::GetViewportScale(WorldContextObject);
	OutViewportPosition = Scale > UE_KINDA_SMALL_NUMBER ? InAbsoluteCoordinate / Scale : InAbsoluteCoordinate;
	return true;
}

bool UDreamUIWidgetGeometryLibrary::ScreenToWidgetLocal(const FDreamUIWidgetGeometry& InGeometry,
	FVector2D InScreenPosition, FVector2D& OutLocalCoordinate)
{
	// A screen position and a viewport pixel position are the same space here: the framework has no
	// window offset of its own, and every screen position it produces or consumes -- a mouse
	// position, a world-to-screen projection -- is already in viewport pixels.
	return AbsoluteToLocal(InGeometry, InScreenPosition, OutLocalCoordinate);
}

bool UDreamUIWidgetGeometryLibrary::ScreenToWidgetAbsolute(FVector2D InScreenPosition, FVector2D& OutAbsoluteCoordinate)
{
	OutAbsoluteCoordinate = InScreenPosition;
	return true;
}

bool UDreamUIWidgetGeometryLibrary::ScreenToViewport(UObject* WorldContextObject, FVector2D InScreenPosition,
	FVector2D& OutViewportPosition)
{
	const float Scale = DreamUIWidgetGeometryLocal::GetViewportScale(WorldContextObject);
	OutViewportPosition = Scale > UE_KINDA_SMALL_NUMBER ? InScreenPosition / Scale : InScreenPosition;
	return true;
}

bool UDreamUIWidgetGeometryLibrary::IsUnderLocation(const FDreamUIWidgetGeometry& InGeometry,
	FVector2D InAbsoluteCoordinate)
{
	if (!InGeometry.bHasAbsolute)
	{
		return false;
	}
	const FVector2D Max = InGeometry.AbsolutePosition + InGeometry.AbsoluteSize;
	return InAbsoluteCoordinate.X >= InGeometry.AbsolutePosition.X && InAbsoluteCoordinate.X <= Max.X
		&& InAbsoluteCoordinate.Y >= InGeometry.AbsolutePosition.Y && InAbsoluteCoordinate.Y <= Max.Y;
}

namespace DreamUIWidgetGeometryLocal
{
	/**
	 * Pixels per local unit, taken from the rect itself.
	 *
	 * The wider axis decides when the two disagree, which they do for a non-uniformly scaled widget.
	 * UMG has the same limitation for the same reason -- a scalar has one value and a 2D scale has
	 * two -- and picking the larger errs toward "this length is at least this long", which is the
	 * safe direction for the hit-test-ish things scalars get used for.
	 */
	bool GetLocalToAbsoluteScale(const FDreamUIWidgetGeometry& InGeometry, float& OutScale)
	{
		if (!InGeometry.bHasAbsolute
			|| InGeometry.LocalSize.X <= UE_KINDA_SMALL_NUMBER
			|| InGeometry.LocalSize.Y <= UE_KINDA_SMALL_NUMBER)
		{
			return false;
		}
		OutScale = static_cast<float>(FMath::Max(
			InGeometry.AbsoluteSize.X / InGeometry.LocalSize.X,
			InGeometry.AbsoluteSize.Y / InGeometry.LocalSize.Y));
		return OutScale > UE_KINDA_SMALL_NUMBER;
	}
}

bool UDreamUIWidgetGeometryLibrary::TransformScalarLocalToAbsolute(const FDreamUIWidgetGeometry& InGeometry,
	float InLocalScalar, float& OutAbsoluteScalar)
{
	OutAbsoluteScalar = 0.0f;
	float Scale = 0.0f;
	if (!DreamUIWidgetGeometryLocal::GetLocalToAbsoluteScale(InGeometry, Scale))
	{
		return false;
	}
	OutAbsoluteScalar = InLocalScalar * Scale;
	return true;
}

bool UDreamUIWidgetGeometryLibrary::TransformScalarAbsoluteToLocal(const FDreamUIWidgetGeometry& InGeometry,
	float InAbsoluteScalar, float& OutLocalScalar)
{
	OutLocalScalar = 0.0f;
	float Scale = 0.0f;
	if (!DreamUIWidgetGeometryLocal::GetLocalToAbsoluteScale(InGeometry, Scale))
	{
		return false;
	}
	OutLocalScalar = InAbsoluteScalar / Scale;
	return true;
}

bool UDreamUIWidgetGeometryLibrary::TransformVectorLocalToAbsolute(const FDreamUIWidgetGeometry& InGeometry,
	FVector2D InLocalVector, FVector2D& OutAbsoluteVector)
{
	OutAbsoluteVector = FVector2D::ZeroVector;
	if (!InGeometry.bHasAbsolute
		|| InGeometry.LocalSize.X <= UE_KINDA_SMALL_NUMBER
		|| InGeometry.LocalSize.Y <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutAbsoluteVector = InLocalVector * (InGeometry.AbsoluteSize / InGeometry.LocalSize);
	return true;
}

bool UDreamUIWidgetGeometryLibrary::TransformVectorAbsoluteToLocal(const FDreamUIWidgetGeometry& InGeometry,
	FVector2D InAbsoluteVector, FVector2D& OutLocalVector)
{
	OutLocalVector = FVector2D::ZeroVector;
	if (!InGeometry.bHasAbsolute
		|| InGeometry.AbsoluteSize.X <= UE_KINDA_SMALL_NUMBER
		|| InGeometry.AbsoluteSize.Y <= UE_KINDA_SMALL_NUMBER)
	{
		return false;
	}
	OutLocalVector = InAbsoluteVector * (InGeometry.LocalSize / InGeometry.AbsoluteSize);
	return true;
}
