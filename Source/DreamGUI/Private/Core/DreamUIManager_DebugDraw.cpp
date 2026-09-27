// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamBaseRaycaster.h"
#include "Engine/World.h"
#include "Interaction/UISelectable.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/DreamUIMesh/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Core/DreamUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "DreamUIManager"
#define ENABLED_DreamGUI_DEBUG_DUMP				0
#define ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME		0
#if WITH_EDITOR

void UDreamUIManagerWorldSubsystem::DrawFrameOnWidget(UDreamWidget* Widget, bool ScreenOrWorld)
{
	if (GetSelection()->IsSelected(Widget))//select self
	{
		auto RectDrawColor = FColor(160, 160, 160, 255);//gray means normal object
		auto DrawWidget = [=](UDreamWidget* InWidget, const FColor& Color)
		{
			// The DRAWN matrix, not the layout one. These frames exist to say "this is the widget",
			// so inside a Perspective scope they have to follow the widget's foreshortened geometry
			// rather than sit where layout would have put it. Nothing is dragged by them, which is
			// what makes this safe here and not safe for the designer handles.
			auto WorldTransform = InWidget->GetWorldMatrix();
			FVector RelativeOffset(0, 0, 0);
			RelativeOffset.Y = (0.5f - InWidget->GetPivot().X) * InWidget->GetWidth();
			RelativeOffset.Z = (0.5f - InWidget->GetPivot().Y) * InWidget->GetHeight();
			auto Extends = FVector2D(InWidget->GetWidth(), InWidget->GetHeight()) * 0.5f;
			UDreamUIManagerWorldSubsystem::DrawDebugRect(InWidget->GetWorld()
				, RelativeOffset, WorldTransform
				, Extends, Color
				, InWidget, InWidget->GetDisplayName(), ScreenOrWorld);
		};
		//parent
		if (auto Parent = Widget->GetParent())
		{
			DrawWidget(Parent, RectDrawColor);
		}
		//child
		for (auto& Child : Widget->GetChildren())
		{
			if (IsValid(Child)
				&& Child->GetRenderVisibleInHierarchy())
			{
				DrawWidget(Child, RectDrawColor);
			}
		}
		//other object of same hierarchy is selected
		if (auto Parent = Widget->GetParent())
		{
			for (auto& SiblingWidget : Parent->GetChildren())
			{
				if (IsValid(SiblingWidget)
					&& SiblingWidget->GetRenderVisibleInHierarchy()
					&& SiblingWidget != Widget)
				{
					DrawWidget(SiblingWidget, RectDrawColor);
				}
			}
		}

		//self
		{
			RectDrawColor = FColor(0, 255, 0, 255);//green means selected object
			auto WorldTransform = Widget->GetWorldMatrix();
			FVector RelativeOffset(0, 0, 0);
			RelativeOffset.Y = (0.5f - Widget->GetPivot().X) * Widget->GetWidth();
			RelativeOffset.Z = (0.5f - Widget->GetPivot().Y) * Widget->GetHeight();
			auto Extends = FVector2D(Widget->GetWidth(), Widget->GetHeight()) * 0.5f;
			UDreamUIManagerWorldSubsystem::DrawDebugRect(Widget->GetWorld()
				, RelativeOffset, WorldTransform
				, Extends, RectDrawColor
				, Widget, Widget->GetDisplayName(), ScreenOrWorld);

			// The pivot, at the widget's origin. Sized off the widget so it stays readable on a
			// 20-pixel icon and does not swallow a full-screen panel, and clamped so it does neither.
			const float PivotSize = FMath::Clamp(FMath::Min(Widget->GetWidth(), Widget->GetHeight()) * 0.12f, 3.0f, 12.0f);
			UDreamUIManagerWorldSubsystem::DrawDebugPivot(Widget->GetWorld()
				, WorldTransform, PivotSize, RectDrawColor
				, Widget, Widget->GetDisplayName(), ScreenOrWorld);

			if (auto Visual = Cast<UDreamVisual>(Widget->GetVisual()))
			{
				FVector Min, Max;
				Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
				auto GeometryBoundsDrawColor = FColor(255, 255, 0, 255);//yellow for geometry bounds
				auto GeometryBoundsExtends = (Max - Min) * 0.5f;
				auto GeometryRelativeOffset = (Min + Max) * 0.5f;
				auto WidgetExtends3D = FVector(0, Extends.X, Extends.Y); 
				if (WidgetExtends3D != GeometryBoundsExtends || RelativeOffset != GeometryRelativeOffset)
				{
					UDreamUIManagerWorldSubsystem::DrawDebugBox(Widget->GetWorld()
						, GeometryRelativeOffset, WorldTransform
						, GeometryBoundsExtends, GeometryBoundsDrawColor
						, Widget->GetVisual() , FString::Printf(TEXT("%s.Visual"), *Widget->GetDisplayName()), ScreenOrWorld);
				}
			}
		}
	}
}

void UDreamUIManagerWorldSubsystem::DrawNavigationArrow(UWorld* InWorld, const TArray<FVector>& InControlPoints, const FVector& InArrowPointA, const FVector& InArrowPointB, FColor const& InColor, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	if (InControlPoints.Num() != 4)return;
	TArray<FVector3f> ResultPoints;
	TArray<FDreamUIMeshVertex> VertexArray;
	TArray<FDreamUIMeshIndex> IndexArray;
	const int Segment = FMath::Min(40, FMath::CeilToInt(FVector::Distance(InControlPoints[0], InControlPoints[3]) * 0.5f));

	auto CalculateCubicBezierPoint = [](float t, FVector p0, FVector p1, FVector p2, FVector p3)
	{
		float u = 1 - t;
		float tt = t * t;
		float uu = u * u;
		float uuu = uu * u;
		float ttt = tt * t;

		FVector p = uuu * p0;
		p += 3 * uu * t * p1;
		p += 3 * u * tt * p2;
		p += ttt * p3;

		return p;
	};

	IndexArray.Add(InControlPoints.Num());
	new(VertexArray) FDreamUIMeshVertex(FVector3f(InControlPoints[0]), InColor);
	for (int i = 1; i <= Segment; i++)
	{
		float t = i / (float)Segment;
		auto InterPoint = CalculateCubicBezierPoint(t, InControlPoints[0], InControlPoints[1], InControlPoints[2], InControlPoints[3]);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InterPoint), InColor);
	}
	
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		//arrow
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InControlPoints[3]), InColor);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InArrowPointA), InColor);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InControlPoints[3]), InColor);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InArrowPointB), InColor);

		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = FMatrix::Identity;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

void UDreamUIManagerWorldSubsystem::DrawNavigationVisualizerOnUISelectable(UWorld* InWorld, UUISelectable* InSelectable, bool IsScreenSpace)
{
	auto SourceWidget = InSelectable->GetWidget();
	if (!IsValid(SourceWidget))return;
	const FColor Color = GetSelection()->IsSelected(SourceWidget) ? FColor(255, 255, 0, 255) : FColor(140, 140, 0, 255);
	constexpr float Offset = 2;
	constexpr float ArrowSize = 5;
	
	auto GetArrowSizeScaledByDistanceToCamera = [=, this](FVector WorldPoint)
	{
		if (this->GetWorld()->IsGameWorld())
		{
			if (auto PC = this->GetWorld()->GetFirstPlayerController())
			{
				if (auto CameraManager = PC->PlayerCameraManager)
				{
					auto ViewLocation = CameraManager->GetCameraLocation();
					float Distance = FVector::Distance(WorldPoint, ViewLocation);
					return Distance * 0.01f;
				}
			}
		}
		else
		{
			if (auto ViewportClient = GetEditorViewportClient())
			{
				if (ViewportClient->IsOrtho())
				{
					return ViewportClient->GetOrthoZoom() * 0.001f; 
				}
				else
				{
					auto ViewLocation = ViewportClient->GetViewLocation();
					float Distance = FVector::Distance(WorldPoint, ViewLocation);
					return Distance * 0.01f;
				}
			}
		}
		return ArrowSize;
	};

	if (auto ToLeftComp = InSelectable->FindSelectableOnLeft())
	{
		if (ToLeftComp != InSelectable)
		{
			auto SourceLeftPoint = FVector(0, SourceWidget->GetLocalSpaceLeft(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) + Offset);
			SourceLeftPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceLeftPoint);
			auto DestWidget = ToLeftComp->GetWidget();
			auto LocalDestRightPoint = FVector(0, DestWidget->GetLocalSpaceRight(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) + Offset);
			auto DestRightPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint);
			float Distance = FVector::Distance(SourceLeftPoint, DestRightPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestRightPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceLeftPoint,
					SourceLeftPoint - SourceWidget->GetRightVector() * Distance,
					DestRightPoint + DestWidget->GetRightVector() * Distance,
					DestRightPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationLeft"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToRightComp = InSelectable->FindSelectableOnRight())
	{
		if (ToRightComp != InSelectable)
		{
			auto SourceRightPoint = FVector(0, SourceWidget->GetLocalSpaceRight(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) - Offset);
			SourceRightPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceRightPoint);
			auto DestWidget = ToRightComp->GetWidget();
			auto LocalDestLeftPoint = FVector(0, DestWidget->GetLocalSpaceLeft(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) - Offset);
			auto DestLeftPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint);
			float Distance = FVector::Distance(SourceRightPoint, DestLeftPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestLeftPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceRightPoint,
					SourceRightPoint + SourceWidget->GetRightVector() * Distance,
					DestLeftPoint - DestWidget->GetRightVector() * Distance,
					DestLeftPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationRight"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToDownComp = InSelectable->FindSelectableOnDown())
	{
		if (ToDownComp != InSelectable)
		{
			auto SourceDownPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) - Offset, SourceWidget->GetLocalSpaceBottom());
			SourceDownPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceDownPoint);
			auto DestWidget = ToDownComp->GetWidget();
			auto LocalDestUpPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) - Offset, DestWidget->GetLocalSpaceTop());
			auto DestUpPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint);
			float Distance = FVector::Distance(SourceDownPoint, DestUpPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestUpPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceDownPoint,
					SourceDownPoint - SourceWidget->GetUpVector() * Distance,
					DestUpPoint + DestWidget->GetUpVector() * Distance,
					DestUpPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationDown"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToUpComp = InSelectable->FindSelectableOnUp())
	{
		if (ToUpComp != InSelectable)
		{
			auto SourceUpPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) + Offset, SourceWidget->GetLocalSpaceTop());
			SourceUpPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceUpPoint);
			auto DestWidget = ToUpComp->GetWidget();
			auto LocalDestDownPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) + Offset, DestWidget->GetLocalSpaceBottom());
			auto DestDownPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint);
			float Distance = FVector::Distance(SourceUpPoint, DestDownPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestDownPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceUpPoint,
					SourceUpPoint + SourceWidget->GetUpVector() * Distance,
					DestDownPoint - DestWidget->GetUpVector() * Distance,
					DestDownPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationUp"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
}

FEditorViewportClient* UDreamUIManagerWorldSubsystem::GetEditorViewportClient()
{
	if (CacheViewportClient == nullptr)
	{
		for (auto& ViewportClient : GEditor->GetAllViewportClients())
		{
			if (ViewportClient->GetWorld() == this->GetWorld())
			{
				if (ViewportClient->IsVisible())
				{
					CacheViewportClient = ViewportClient;
				}
			}
		}
	}
	return CacheViewportClient;
}


void UDreamUIManagerWorldSubsystem::OnEndOfFrame()
{
	CacheViewportClient = nullptr;
}

void UDreamUIManagerWorldSubsystem::DrawDebugPivot(UWorld* InWorld, const FMatrix& LocalToWorld, float Size, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	// The widget's origin is its pivot: the rect is drawn offset from here by the pivot fractions, so
	// this marker is what tells you which corner of that rect the numbers are measured from.
	TArray<FVector3f> LinePoints;
	const float Arm = Size;
	const float Diamond = Size * 0.45f;
	// A cross...
	LinePoints.Add(FVector3f(0, -Arm, 0));
	LinePoints.Add(FVector3f(0, Arm, 0));
	LinePoints.Add(FVector3f(0, 0, -Arm));
	LinePoints.Add(FVector3f(0, 0, Arm));
	// ...inside a diamond, so it reads as a point rather than as two more frame edges.
	LinePoints.Add(FVector3f(0, -Diamond, 0));
	LinePoints.Add(FVector3f(0, 0, Diamond));
	LinePoints.Add(FVector3f(0, 0, Diamond));
	LinePoints.Add(FVector3f(0, Diamond, 0));
	LinePoints.Add(FVector3f(0, Diamond, 0));
	LinePoints.Add(FVector3f(0, 0, -Diamond));
	LinePoints.Add(FVector3f(0, 0, -Diamond));
	LinePoints.Add(FVector3f(0, -Diamond, 0));
	UDreamUIManagerWorldSubsystem::DrawDebugLine(InWorld, LocalToWorld, LinePoints, Color, Object, DebugName, ScreenOrWorld);
}

void UDreamUIManagerWorldSubsystem::DrawDebugRect(UWorld* InWorld, const FVector& Center, const FMatrix& LocalToWorld, FVector2D const& Rect, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		TArray<FDreamUIMeshVertex> VertexArray;
		TArray<FDreamUIMeshIndex> IndexArray;
		auto PushNewLine = [&](FVector Start, FVector End)
		{
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + Start), Color);
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + End), Color);
		};

		auto Start = FVector(0, Rect.X, Rect.Y);
		auto End = FVector(0, -Rect.X, Rect.Y);
		PushNewLine(Start, End);

		Start = FVector(0, Rect.X, -Rect.Y);
		End = FVector(0, -Rect.X, -Rect.Y);
		PushNewLine(Start, End);

		Start = FVector(0, Rect.X, Rect.Y);
		End = FVector(0, Rect.X, -Rect.Y);
		PushNewLine(Start, End);

		Start = FVector(0, -Rect.X, Rect.Y);
		End = FVector(0, -Rect.X, -Rect.Y);
		PushNewLine(Start, End);

		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = LocalToWorld;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

void UDreamUIManagerWorldSubsystem::DrawDebugBox(UWorld* InWorld, const FVector& Center, const FMatrix& LocalToWorld,
	FVector const& Box, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		TArray<FDreamUIMeshVertex> VertexArray;
		TArray<FDreamUIMeshIndex> IndexArray;
		auto PushNewLine = [&](const FVector& Start, const FVector& End)
		{
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + Start), Color);
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + End), Color);
		};

		FVector Start, End;
		Start = FVector(Box.X, Box.Y, Box.Z);
		End = FVector(Box.X, -Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, -Box.Z);
		End = FVector(Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, Box.Z);
		End = FVector(Box.X, Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, -Box.Y, Box.Z);
		End = FVector(Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, Box.Y, Box.Z);
		End = FVector(-Box.X, -Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, Box.Y, -Box.Z);
		End = FVector(-Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, Box.Y, Box.Z);
		End = FVector(-Box.X, Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, -Box.Y, Box.Z);
		End = FVector(-Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, Box.Z);
		End = FVector(-Box.X, Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, -Box.Y, Box.Z);
		End = FVector(-Box.X, -Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, -Box.Z);
		End = FVector(-Box.X, Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, -Box.Y, -Box.Z);
		End = FVector(-Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = LocalToWorld;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

void UDreamUIManagerWorldSubsystem::DrawDebugLine(UWorld* InWorld, const FMatrix& LocalToWorld,
	const TArray<FVector3f>& LinePoints, FColor const& Color, void* Object, const FString& DebugName,
	bool ScreenOrWorld)
{
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		TArray<FDreamUIMeshVertex> VertexArray;
		TArray<FDreamUIMeshIndex> IndexArray;
		//lines
		for (int i = 0; i < LinePoints.Num(); i+=2)
		{
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(LinePoints[i], Color);
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(LinePoints[i + 1], Color);
		}
		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = LocalToWorld;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

bool UDreamUIManagerWorldSubsystem::RaycastHitUI(UWorld* InWorld, const TArray<UDreamWidget*>& InWidgets, const FVector& LineStart, const FVector& LineEnd
                                               , UDreamWidget*& ResultSelectTarget, int& InOutTargetIndexInHitArray
)
{
	TArray<FDreamUIHitResult> HitResultArray;
	for (auto Widget : InWidgets)
	{
		if (!IsValid(Widget))continue;
		if (Widget->GetWorld() == InWorld)
		{
			if (auto Visual = Widget->GetVisual())
			{
				if (Widget->GetRenderVisibleInHierarchy() && Widget->GetRenderCanvas() != nullptr)
				{
					FDreamUIHitResult HitInfo;
					auto OriginRaycastType = Visual->GetRaycastType();
					Visual->SetRaycastType(EDreamVisualRaycastType::Mesh);//in editor selection, make the ray hit actural triangle
					if (Visual->LineTraceUI(HitInfo, LineStart, LineEnd))
					{
						if (Widget->IsPointVisibleOnClip(HitInfo.Location))
						{
							HitResultArray.Add(HitInfo);
						}
					}
					Visual->SetRaycastType(OriginRaycastType);
				}
			}
		}
	}
	if (HitResultArray.Num() > 0)//hit something
	{
		HitResultArray.Sort([](const FDreamUIHitResult& A, const FDreamUIHitResult& B)
			{
				auto AWidget = (UDreamWidget*)(A.Widget.Get());
				auto BWidget = (UDreamWidget*)(B.Widget.Get());
				if (AWidget->GetRenderCanvas()->GetActualSortOrder() == BWidget->GetRenderCanvas()->GetActualSortOrder())//if Canvas's sort order is equal then sort on item's depth
				{
					return AWidget->GetFlattenHierarchyIndex() > BWidget->GetFlattenHierarchyIndex();
				}
				else//if Canvas's depth not equal then sort on Canvas's SortOrder
				{
					return AWidget->GetRenderCanvas()->GetActualSortOrder() > BWidget->GetRenderCanvas()->GetActualSortOrder();
				}
			});
		InOutTargetIndexInHitArray++;
		if (InOutTargetIndexInHitArray >= HitResultArray.Num() || InOutTargetIndexInHitArray < 0)
		{
			InOutTargetIndexInHitArray = 0;
		}
		auto HitWidget = (UDreamWidget*)(HitResultArray[InOutTargetIndexInHitArray].Widget.Get());//target need to select
		ResultSelectTarget = HitWidget;
		return true;
	}
	return false;
}
#endif
#if WITH_EDITOR
void UDreamUIManagerWorldSubsystem::DrawHelperGizmo()
{
	//editor draw helper frame
	auto Settings = GetDefault<UDreamUIEditorSettings>();
	if (Settings->bDrawHelperFrame)
	{
		if (this->GetWorld()->WorldType == EWorldType::Game
			|| this->GetWorld()->WorldType == EWorldType::PIE
			|| this->GetWorld()->WorldType == EWorldType::Editor
			// || this->GetWorld()->WorldType == EWorldType::EditorPreview
			)
		{
			struct LOCAL
			{
				static void ForEachWidget(UDreamUIManagerWorldSubsystem* DreamUIManager, UDreamWidget* Widget, bool bIsGameWorld)
				{
					if (!IsValid(Widget))return;

					bool bIsScreenSpace = false;
					if (bIsGameWorld)
					{
						if (auto RenderCanvas = Widget->GetRenderCanvas())
						{
							bIsScreenSpace = RenderCanvas->IsRenderToScreenSpace() || RenderCanvas->IsRenderToRenderTarget();
						}
					}
					DreamUIManager->DrawFrameOnWidget(Widget, bIsScreenSpace);

					for (auto& Child : Widget->GetChildren())
					{
						ForEachWidget(DreamUIManager, Child, bIsGameWorld);
					}
				}
			};
			auto bIsGameWorld = this->GetWorld()->IsGameWorld();
			for (const TWeakObjectPtr<UDreamCanvas>& Canvas : SnapshotCanvases())
			{
				if (!IsCanvasStillRegistered(Canvas))continue;
				if (!Canvas->IsRootCanvas())continue;
				LOCAL::ForEachWidget(this, Canvas->GetWidget(), bIsGameWorld);
			}
		}
	}

	if (Settings->bDrawSelectableNavigationVisualizer)
	{
		for (auto& Selectable : AllSelectableArray)
		{
			if (!Selectable.IsValid())continue;
			if (!IsValid(Selectable->GetWorld()))continue;
			if (!IsValid(Selectable->GetWidget()))continue;
			if (!IsValid(Selectable->GetWidget()->GetRenderCanvas()))continue;
			if (!Selectable->GetWidget()->GetInteractableInHierarchy())continue;

			bool bIsScreenSpace = false;
			if (DreamUI::IsGameWorld(Selectable.Get()))
			{
				auto RenderCanvas = Selectable->GetWidget()->GetRenderCanvas();
				bIsScreenSpace = RenderCanvas->IsRenderToScreenSpace() || RenderCanvas->IsRenderToRenderTarget();
			}
			DrawNavigationVisualizerOnUISelectable(Selectable->GetWorld(), Selectable.Get()
				, bIsScreenSpace);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
