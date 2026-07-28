// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditorViewportClient.h"
#include "LexUIDesignScreenSizes.h"
#include "LexUIPrefabEditorViewport.h"
#include "Components/DirectionalLightComponent.h"
#include "Animation/AnimationAsset.h"
#include "GameFramework/Actor.h"
#include "Math/Vector.h"
#include "AssetEditorModeManager.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "SceneView.h"
#include "Editor/UnrealEdEngine.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Editor.h"
#include "LexUIPrefabEditor.h"
#include "MouseDeltaTracker.h"
#include "Misc/ITransaction.h"
#include "UnrealEdGlobals.h"
#include "UnrealWidget.h"
#include "Elements/Framework/TypedElementRegistry.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementViewportInteraction.h"
#include "InputState.h"
#include "ViewportSelectionUtilities.h"
#include "HModel.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "LexUIPrefabViewportClickHandlers.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexWidget.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "PrefabAnimation/LexUIPrefabSequenceEditor.h"
#include "ISequencer.h"
#include "KeyPropertyParams.h"
#include "PropertyPath.h"
#include "Core/LexUIMesh/LexUIGizmoMesh.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "PrefabSystem/LexUIPrefabInstanceScene.h"
#include "Utils/LexUIUtils.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorViewportClient"

// UE5.8: HLevelSocketProxy is now declared AND implemented/exported by the engine
// (ViewportSelectionUtilities.h), so re-implementing it here is a duplicate (C4273).

class FLexUITransformWidget
{
private:		
	int PressMouseX = 0, PressMouseY = 0; FVector PressAxisHitPoint = FVector::Zero();
	FVector PressAxisVector = FVector::ZeroVector;
	FTransform ThisTransformWhenPress = FTransform::Identity;
	FTransform ThisTransform = FTransform::Identity;
	FTransform RenderTransform = FTransform::Identity;
	float PressRenderScale = 1.0f;
	TWeakObjectPtr<ULexUIManagerWorldSubsystem> LexUIManager;
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<FLexUIGizmoMesh> MoveAxisX;
	TSharedPtr<FLexUIGizmoMesh> MoveAxisY;
	TSharedPtr<FLexUIGizmoMesh> MoveAxisZ;
	TSharedPtr<FLexUIGizmoMesh> MovePlaneYZ;
	TSharedPtr<FLexUIGizmoMesh> MovePlaneZX;
	TSharedPtr<FLexUIGizmoMesh> MovePlaneXY;
	TSharedPtr<FLexUIGizmoMesh> RotateAxisX;
	TSharedPtr<FLexUIGizmoMesh> RotateAxisY;
	TSharedPtr<FLexUIGizmoMesh> RotateAxisZ;
	TWeakObjectPtr<UMaterialInterface> GizmoMaterial;
	FVector MovePlaneYZCenter;
	FVector MovePlaneZXCenter;
	FVector MovePlaneXYCenter;
	const float AxisLength = 100.0f;
	const float AxisPlaneSize = 30.0f;
	const float RotateAxisRadius = 100.0f;
	FColor ColorAxisX = FColor::Red, ColorAxisY = FColor::Green, ColorAxisZ = FColor::Blue;
	FColor HighlightColor = FColor::Yellow;
	FString DebugName;
	bool bCanTick = false;
	enum class EMoveAxisType
	{
		None, X, Y, Z, YZ, ZX, XY, 
	};
	EMoveAxisType MoveAxisType = EMoveAxisType::None;
	enum class ERotateAxisType
	{
		None, X, Y, Z,
	};
	ERotateAxisType RotateAxisType = ERotateAxisType::None;
	enum class ETransformType
	{
		None, Move, Rotate,
	};
	ETransformType TransformType = ETransformType::Move;
	bool bIsMousePressedAtThisFrame = false;
	bool bIsMouseReleasedAtThisFrame = false;
	bool bIsDragging = false;
	TWeakObjectPtr<ULexWidget> SelectedWidget;
	FLexUIPrefabEditorViewportClient* ViewportClient = nullptr;
	TUniquePtr<FSceneViewFamilyContext> ViewFamily = nullptr;
	void UpdateAxis()
	{
		auto SceneView = ViewportClient->CalcSceneView( ViewFamily.Get() );
		auto MouseX = ViewportClient->Viewport->GetMouseX();
		auto MouseY = ViewportClient->Viewport->GetMouseY();

		RenderTransform = ThisTransform;
		float RenderScale = 1;
		if (bIsDragging)
		{
			RenderScale = PressRenderScale;
			RenderTransform.SetScale3D(FVector(RenderScale, RenderScale, RenderScale));
		}
		else
		{
			if (ViewportClient->GetViewportType() != LVT_Perspective)
			{
				RenderScale = ViewportClient->GetViewTransform().GetOrthoZoom() * 0.0001f;
			}
			else
			{
				RenderScale = FVector::Dist(ViewportClient->GetViewLocation(), ThisTransform.GetTranslation()) * 1.5f / ViewportClient->Viewport->GetSizeXY().X;
			}
			if (bIsMousePressedAtThisFrame)
			{
				PressRenderScale = RenderScale; 
			}
		}
		RenderTransform.SetScale3D(FVector(RenderScale, RenderScale, RenderScale));
		
		if (bIsDragging)
		{
			if (SelectedWidget.IsValid())
			{
				FVector RayOrigin, RayDirection;
				FSceneView::DeprojectScreenToWorld(FVector2D(MouseX, MouseY), SceneView->UnscaledViewRect, SceneView->ViewMatrices.GetInvViewProjectionMatrix(), RayOrigin, RayDirection);

				auto Center = ThisTransform.GetTranslation();
				constexpr float Far = 1e6f;
				auto LineStartOfMouse = RayOrigin - RayDirection * Far;
				auto LineEndOfMouse = RayOrigin + RayDirection * Far;
				if (TransformType == ETransformType::Move)
				{
					FVector A = FVector::Zero(), B = FVector(BIG_NUMBER);
					FVector Diff = FVector::ZeroVector;
					switch (MoveAxisType)
					{
					case EMoveAxisType::YZ:
						{
							auto HitPoint = FMath::LinePlaneIntersection(LineStartOfMouse, LineEndOfMouse, Center, ThisTransform.GetUnitAxis(EAxis::X));
							Diff = HitPoint - PressAxisHitPoint;
						}
						break;
					case EMoveAxisType::ZX:
						{
							auto HitPoint = FMath::LinePlaneIntersection(LineStartOfMouse, LineEndOfMouse, Center, ThisTransform.GetUnitAxis(EAxis::Y));
							Diff = HitPoint - PressAxisHitPoint;
						}
						break;
					case EMoveAxisType::XY:
						{
							auto HitPoint = FMath::LinePlaneIntersection(LineStartOfMouse, LineEndOfMouse, Center, ThisTransform.GetUnitAxis(EAxis::Z));
							Diff = HitPoint - PressAxisHitPoint;
						}
						break;
					case EMoveAxisType::X:
						{
							FMath::SegmentDistToSegment(LineStartOfMouse, LineEndOfMouse, ThisTransform.TransformPosition(FVector(-Far, 0, 0)), ThisTransform.TransformPosition(FVector(Far, 0, 0)), A, B);
							Diff = B - PressAxisHitPoint;
						}
						break;
					case EMoveAxisType::Y:
						{
							FMath::SegmentDistToSegment(LineStartOfMouse, LineEndOfMouse, ThisTransform.TransformPosition(FVector(0, -Far, 0)), ThisTransform.TransformPosition(FVector(0, Far, 0)), A, B);
							Diff = B - PressAxisHitPoint;
						}
						break;
					case EMoveAxisType::Z:
						{
							FMath::SegmentDistToSegment(LineStartOfMouse, LineEndOfMouse, ThisTransform.TransformPosition(FVector(0, 0, -Far)), ThisTransform.TransformPosition(FVector(0, 0, Far)), A, B);
							Diff = B - PressAxisHitPoint;
						}
						break;
					}
					ThisTransform.SetTranslation(ThisTransformWhenPress.GetTranslation() + Diff);
					FLexUIUtils::ChangePropertyWithNotify(SelectedWidget.Get(), USceneComponent::GetRelativeLocationPropertyName(), [=, this]
					{
						SelectedWidget->SetWorldLocation(ThisTransform.GetLocation());
					});
				}
				else if (TransformType == ETransformType::Rotate)
				{
					FRotator Diff = FRotator();
					switch (RotateAxisType)
					{
					case ERotateAxisType::X:
						{
							auto LinePlaneIntersectPointX = FMath::LinePlaneIntersection(LineStartOfMouse, LineEndOfMouse, Center, RenderTransform.GetUnitAxis(EAxis::X));
							auto AxisVector = (LinePlaneIntersectPointX - Center).GetSafeNormal();
							auto DotValue = FVector::DotProduct(PressAxisVector, AxisVector);
							auto AngleInDegree = FMath::RadiansToDegrees(FMath::Acos(DotValue));
							auto CrossVector = FVector::CrossProduct(PressAxisVector, AxisVector).GetSafeNormal();
							auto AngleSign = -FMath::Sign(FVector::DotProduct(CrossVector, FVector(1, 0, 0)));
							AngleInDegree *= AngleSign;
							Diff.Roll = AngleInDegree;
						}
						break;
					case ERotateAxisType::Y:
						{
							auto LinePlaneIntersectPointX = FMath::LinePlaneIntersection(LineStartOfMouse, LineEndOfMouse, Center, RenderTransform.GetUnitAxis(EAxis::Y));
							auto AxisVector = (LinePlaneIntersectPointX - Center).GetSafeNormal();
							auto DotValue = FVector::DotProduct(PressAxisVector, AxisVector);
							auto AngleInDegree = FMath::RadiansToDegrees(FMath::Acos(DotValue));
							auto CrossVector = FVector::CrossProduct(PressAxisVector, AxisVector).GetSafeNormal();
							auto AngleSign = -FMath::Sign(FVector::DotProduct(CrossVector, FVector(0, 1, 0)));
							AngleInDegree *= AngleSign;
							Diff.Pitch = AngleInDegree;
						}
						break;
					case ERotateAxisType::Z:
						{
							auto LinePlaneIntersectPointX = FMath::LinePlaneIntersection(LineStartOfMouse, LineEndOfMouse, Center, RenderTransform.GetUnitAxis(EAxis::Z));
							auto AxisVector = (LinePlaneIntersectPointX - Center).GetSafeNormal();
							auto DotValue = FVector::DotProduct(PressAxisVector, AxisVector);
							auto AngleInDegree = FMath::RadiansToDegrees(FMath::Acos(DotValue));
							auto CrossVector = FVector::CrossProduct(PressAxisVector, AxisVector).GetSafeNormal();
							auto AngleSign = FMath::Sign(FVector::DotProduct(CrossVector, FVector(0, 0, 1)));
							AngleInDegree *= AngleSign;
							Diff.Yaw = AngleInDegree;
						}
						break;
					}
					ThisTransform.SetRotation(ThisTransformWhenPress.GetRotation() * Diff.Quaternion());
					FLexUIUtils::ChangePropertyWithNotify(SelectedWidget.Get(), USceneComponent::GetRelativeRotationPropertyName(), [=, this]
					{
						SelectedWidget->SetWorldRotation(ThisTransform.GetRotation());
					});
				}
			}
		}
		else
		{
			if (SelectedWidget.IsValid())
			{
				ThisTransform = SelectedWidget->GetWorldTransform();
			}
			constexpr uint8 AxisAlpha = 255;
			constexpr uint8 PlaneAlpha = 50;
			//reset color
			{
				MoveAxisX->SetColor(ColorAxisX.WithAlpha(AxisAlpha));
				MoveAxisY->SetColor(ColorAxisY.WithAlpha(AxisAlpha));
				MoveAxisZ->SetColor(ColorAxisZ.WithAlpha(AxisAlpha));
				MovePlaneYZ->SetColor(ColorAxisX.WithAlpha(PlaneAlpha));
				MovePlaneZX->SetColor(ColorAxisY.WithAlpha(PlaneAlpha));
				MovePlaneXY->SetColor(ColorAxisZ.WithAlpha(PlaneAlpha));
				RotateAxisX->SetColor(ColorAxisX.WithAlpha(AxisAlpha));
				RotateAxisY->SetColor(ColorAxisY.WithAlpha(AxisAlpha));
				RotateAxisZ->SetColor(ColorAxisZ.WithAlpha(AxisAlpha));
			}
			MoveAxisType = EMoveAxisType::None;
			RotateAxisType = ERotateAxisType::None;
			
			constexpr float Far = 100000000;
			FVector RayOrigin, RayDirection;
			FSceneView::DeprojectScreenToWorld(FVector2D(MouseX, MouseY), SceneView->UnscaledViewRect, SceneView->ViewMatrices.GetInvViewProjectionMatrix(), RayOrigin, RayDirection);
			FVector LineEnd = RayOrigin + RayDirection * Far;

			auto Center = ThisTransform.GetTranslation();
			if (TransformType == ETransformType::Move)
			{
				//yz plane
				{
					auto IntersectPoint = FMath::LinePlaneIntersection(RayOrigin, LineEnd, Center, RenderTransform.GetUnitAxis(EAxis::X));
					auto IntersectPointLocalSpace = RenderTransform.InverseTransformPosition(IntersectPoint);
					bool bIsHit = IntersectPointLocalSpace.Y > 0 && IntersectPointLocalSpace.Y < AxisPlaneSize && IntersectPointLocalSpace.Z > 0 && IntersectPointLocalSpace.Z < AxisPlaneSize;
					MovePlaneYZ->SetColor((bIsHit ? HighlightColor : ColorAxisX).WithAlpha(PlaneAlpha));
					if (bIsHit)
					{
						MoveAxisType = EMoveAxisType::YZ;
						if (bIsMousePressedAtThisFrame)
						{
							PressAxisHitPoint = IntersectPoint;
						}
						return;
					}
				}
				//zx plane
				{
					auto IntersectPoint = FMath::LinePlaneIntersection(RayOrigin, LineEnd, Center, RenderTransform.GetUnitAxis(EAxis::Y));
					auto IntersectPointLocalSpace = RenderTransform.InverseTransformPosition(IntersectPoint);
					bool bIsHit = IntersectPointLocalSpace.Z > 0 && IntersectPointLocalSpace.Z < AxisPlaneSize && IntersectPointLocalSpace.X > 0 && IntersectPointLocalSpace.X < AxisPlaneSize;
					MovePlaneZX->SetColor((bIsHit ? HighlightColor : ColorAxisY).WithAlpha(PlaneAlpha));
					if (bIsHit)
					{
						MoveAxisType = EMoveAxisType::ZX;
						if (bIsMousePressedAtThisFrame)
						{
							PressAxisHitPoint = IntersectPoint;
						}
						return;
					}
				}
				//xy plane
				{
					auto IntersectPoint = FMath::LinePlaneIntersection(RayOrigin, LineEnd, Center, RenderTransform.GetUnitAxis(EAxis::Z));
					auto IntersectPointLocalSpace = RenderTransform.InverseTransformPosition(IntersectPoint);
					bool bIsHit = IntersectPointLocalSpace.X > 0 && IntersectPointLocalSpace.X < AxisPlaneSize && IntersectPointLocalSpace.Y > 0 && IntersectPointLocalSpace.Y < AxisPlaneSize;
					MovePlaneXY->SetColor((bIsHit ? HighlightColor : ColorAxisZ).WithAlpha(PlaneAlpha));
					if (bIsHit)
					{
						MoveAxisType = EMoveAxisType::XY;
						if (bIsMousePressedAtThisFrame)
						{
							PressAxisHitPoint = IntersectPoint;
						}
						return;
					}
				}
			
				FVector A = FVector::Zero(), DistanceXHitPoint = FVector(BIG_NUMBER), DistanceYHitPoint = FVector(BIG_NUMBER), DistanceZHitPoint = FVector(BIG_NUMBER);

				const float HitThreshold = 10.0f * RenderScale;
				FMath::SegmentDistToSegment(RayOrigin, LineEnd, Center, RenderTransform.TransformPosition(FVector(AxisLength, 0, 0)), A, DistanceXHitPoint);
				auto DistanceToX = FVector::Dist(A, DistanceXHitPoint);

				FMath::SegmentDistToSegment(RayOrigin, LineEnd, Center, RenderTransform.TransformPosition(FVector(0, AxisLength, 0)), A, DistanceYHitPoint);
				auto DistanceToY = FVector::Dist(A, DistanceYHitPoint);

				FMath::SegmentDistToSegment(RayOrigin, LineEnd, Center, RenderTransform.TransformPosition(FVector(0, 0, AxisLength)), A, DistanceZHitPoint);
				auto DistanceToZ = FVector::Dist(A, DistanceZHitPoint);

				if (DistanceToX < DistanceToY && DistanceToX < DistanceToZ && DistanceToX < HitThreshold)
				{
					MoveAxisType = EMoveAxisType::X;
					MoveAxisX->SetColor(HighlightColor.WithAlpha(AxisAlpha));
					if (bIsMousePressedAtThisFrame)
					{
						PressAxisHitPoint = DistanceXHitPoint;
					}
				}

				if (DistanceToY < DistanceToX && DistanceToY < DistanceToZ && DistanceToY < HitThreshold)
				{
					MoveAxisType = EMoveAxisType::Y;
					MoveAxisY->SetColor(HighlightColor.WithAlpha(AxisAlpha));
					if (bIsMousePressedAtThisFrame)
					{
						PressAxisHitPoint = DistanceYHitPoint;
					}
				}

				if (DistanceToZ < DistanceToX && DistanceToZ < DistanceToY && DistanceToZ < HitThreshold)
				{
					MoveAxisType = EMoveAxisType::Z;
					MoveAxisZ->SetColor(HighlightColor.WithAlpha(AxisAlpha));
					if (bIsMousePressedAtThisFrame)
					{
						PressAxisHitPoint = DistanceZHitPoint;
					}
				}
			}
			else if (TransformType == ETransformType::Rotate)
			{
				const float HitThreshold = 10.0f * RenderScale;
				FVector A = FVector::Zero(), B = FVector(BIG_NUMBER);

				auto LinePlaneIntersectPointX = FMath::LinePlaneIntersection(RayOrigin, LineEnd, Center, RenderTransform.GetUnitAxis(EAxis::X));
				auto DistToCenter = FVector::Dist(LinePlaneIntersectPointX, Center);
				auto DistanceToX = FMath::Abs(DistToCenter - RotateAxisRadius * RenderScale);

				auto LinePlaneIntersectPointY = FMath::LinePlaneIntersection(RayOrigin, LineEnd, Center, RenderTransform.GetUnitAxis(EAxis::Y));
				DistToCenter = FVector::Dist(LinePlaneIntersectPointY, Center);
				auto DistanceToY = FMath::Abs(DistToCenter - RotateAxisRadius * RenderScale);

				auto LinePlaneIntersectPointZ = FMath::LinePlaneIntersection(RayOrigin, LineEnd, Center, RenderTransform.GetUnitAxis(EAxis::Z));
				DistToCenter = FVector::Dist(LinePlaneIntersectPointZ, Center);
				auto DistanceToZ = FMath::Abs(DistToCenter - RotateAxisRadius * RenderScale);

				if (DistanceToX < DistanceToY && DistanceToX < DistanceToZ && DistanceToX < HitThreshold)
				{
					RotateAxisType = ERotateAxisType::X;
					RotateAxisX->SetColor(HighlightColor.WithAlpha(AxisAlpha));
					if (bIsMousePressedAtThisFrame)
					{
						PressAxisVector = (LinePlaneIntersectPointX - Center).GetSafeNormal();
					}
				}

				if (DistanceToY < DistanceToX && DistanceToY < DistanceToZ && DistanceToY < HitThreshold)
				{
					RotateAxisType = ERotateAxisType::Y;
					RotateAxisY->SetColor(HighlightColor.WithAlpha(AxisAlpha));
					if (bIsMousePressedAtThisFrame)
					{
						PressAxisVector = (LinePlaneIntersectPointY - Center).GetSafeNormal();
					}
				}

				if (DistanceToZ < DistanceToX && DistanceToZ < DistanceToY && DistanceToZ < HitThreshold)
				{
					RotateAxisType = ERotateAxisType::Z;
					RotateAxisZ->SetColor(HighlightColor.WithAlpha(AxisAlpha));
					if (bIsMousePressedAtThisFrame)
					{
						PressAxisVector = (LinePlaneIntersectPointZ - Center).GetSafeNormal();
					}
				}
			}
		}
	}
	TUniquePtr<FScopedTransaction> Transaction = nullptr;
public:
	FLexUITransformWidget(UWorld* InWorld, ULexWidget* InWidget, FLexUIPrefabEditorViewportClient* InViewportClient)
	{
		World = InWorld;
		SelectedWidget = InWidget;
		ThisTransform = SelectedWidget->GetWorldTransform();
		LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(InWorld);
		DebugName = TEXT("LexUITransformWidget");
		
		auto MoveAxisMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/LGUI/EditorGizmo/MoveAxis"));
		if (!MoveAxisMesh)return;
		TArray<FLexUIMeshVertex> SrcMeshVertexArray; TArray<FLexUIMeshIndex> SrcMeshIndexArray;
		FLexUIUtils::StaticMeshToLexUIMeshRenderData(MoveAxisMesh, SrcMeshVertexArray, SrcMeshIndexArray);
		{
			auto VertexArray = SrcMeshVertexArray;
			FRotator3f MoveAxisXRot = FRotator3f(-90, 0, 0);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Position = MoveAxisXRot.RotateVector(Vertex.Position);
				Vertex.Color = ColorAxisX;
			}
			MoveAxisX = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			MoveAxisX->UpdateLocalBounds();
		}
		{
			auto VertexArray = SrcMeshVertexArray;
			FRotator3f MoveAxisYRot = FRotator3f(0, 0, 90);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Position = MoveAxisYRot.RotateVector(Vertex.Position);
				Vertex.Color = ColorAxisY;
			}
			MoveAxisY = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			MoveAxisY->UpdateLocalBounds();
		}
		{
			auto VertexArray = SrcMeshVertexArray;
			for (auto& Vertex : VertexArray)
			{
				Vertex.Color = ColorAxisZ;
			}
			MoveAxisZ = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			MoveAxisZ->UpdateLocalBounds();
		}
		
		auto MovePlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/LGUI/EditorGizmo/MovePlane"));
		FLexUIUtils::StaticMeshToLexUIMeshRenderData(MovePlaneMesh, SrcMeshVertexArray, SrcMeshIndexArray);
		{
			auto VertexArray = SrcMeshVertexArray;
			MovePlaneYZCenter = FVector(0, 0.15f, 0.15f);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Color = ColorAxisX;
			}
			MovePlaneYZ = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			MovePlaneYZ->UpdateLocalBounds();
		}
		{
			auto VertexArray = SrcMeshVertexArray;
			MovePlaneZXCenter = FVector(0.15f, 0, 0.15f);
			FRotator3f MovePlaneZXRot = FRotator3f(0, -90, 0);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Position = MovePlaneZXRot.RotateVector(Vertex.Position);
				Vertex.Color = ColorAxisY;
			}
			MovePlaneZX = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			MovePlaneZX->UpdateLocalBounds();
		}
		{
			auto VertexArray = SrcMeshVertexArray;
			MovePlaneXYCenter = FVector(0.15f, 0.15f, 0);
			FRotator3f MovePlaneXYRot = FRotator3f(-90, 0, 0);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Position = MovePlaneXYRot.RotateVector(Vertex.Position);
				Vertex.Color = ColorAxisZ;
			}
			MovePlaneXY = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			MovePlaneXY->UpdateLocalBounds();
		}

		auto RotateAxisMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/LGUI/EditorGizmo/RotateAxis"));
		FLexUIUtils::StaticMeshToLexUIMeshRenderData(RotateAxisMesh, SrcMeshVertexArray, SrcMeshIndexArray);
		{
			auto VertexArray = SrcMeshVertexArray;
			FRotator3f AxisRot = FRotator3f(90, 0, 0);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Position = AxisRot.RotateVector(Vertex.Position);
				Vertex.Color = ColorAxisX;
			}
			RotateAxisX = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			RotateAxisX->UpdateLocalBounds();
		}
		{
			auto VertexArray = SrcMeshVertexArray;
			FRotator3f AxisRot = FRotator3f(0, 0, 90);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Position = AxisRot.RotateVector(Vertex.Position);
				Vertex.Color = ColorAxisY;
			}
			RotateAxisY = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			RotateAxisY->UpdateLocalBounds();
		}
		{
			auto VertexArray = SrcMeshVertexArray;
			FRotator3f AxisRot = FRotator3f(0, 0, 0);
			for (auto& Vertex : VertexArray)
			{
				Vertex.Position = AxisRot.RotateVector(Vertex.Position);
				Vertex.Color = ColorAxisY;
			}
			RotateAxisZ = MakeShared<FLexUIGizmoMesh>(VertexArray, SrcMeshIndexArray, ELexUIGizmoMeshPrimitiveType::Triangle);
			RotateAxisZ->UpdateLocalBounds();
		}
		
		ViewportClient = InViewportClient;
		ViewFamily = MakeUnique<FSceneViewFamilyContext>(FSceneViewFamily::ConstructionValues(
			InViewportClient->Viewport,
			InViewportClient->GetScene(),
			InViewportClient->EngineShowFlags)
			.SetRealtimeUpdate( true ) );

		if (!GizmoMaterial.IsValid())
		{
			GizmoMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/LGUI/EditorGizmo/GizmoMaterial"));
		}
		if (GizmoMaterial.IsValid())
		{
			MoveAxisX->Material
			= MoveAxisY->Material
			= MoveAxisZ->Material
			= MovePlaneYZ->Material
			= MovePlaneZX->Material
			= MovePlaneXY->Material
			= RotateAxisX->Material
			= RotateAxisY->Material
			= RotateAxisZ->Material
			= TStrongObjectPtr(GizmoMaterial.Get());
		}

		bCanTick = true;
	}
	~FLexUITransformWidget()
	{
	}
	void Tick()
	{
		if (!bCanTick)return;
		UpdateAxis();

		auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(World.Get(), true);
		if (!ViewExtension)return;
		
		auto LocalToWorld = RenderTransform.ToMatrixWithScale();
		if (TransformType == ETransformType::Move)
		{
			auto ViewLocation = ViewportClient->GetViewLocation();
			struct FMovePlaneInfo
			{
				double DistanceToCamera;
				TSharedPtr<FLexUIGizmoMesh> RenderData;
			};
			TArray<FMovePlaneInfo> MovePlanes;
			MovePlanes.Add({ FVector::DistSquared(ViewLocation, RenderTransform.TransformPosition(MovePlaneYZCenter)), MovePlaneYZ});
			MovePlanes.Add({ FVector::DistSquared(ViewLocation, RenderTransform.TransformPosition(MovePlaneZXCenter)), MovePlaneZX});
			MovePlanes.Add({ FVector::DistSquared(ViewLocation, RenderTransform.TransformPosition(MovePlaneXYCenter)), MovePlaneXY});
			//simple sort on distance
			MovePlanes.Sort([](const FMovePlaneInfo& A, const FMovePlaneInfo& B)
			{			
				return A.DistanceToCamera > B.DistanceToCamera;
			});
			for (auto& MovePlane : MovePlanes)
			{
				MovePlane.RenderData->LocalToWorldMatrix = LocalToWorld;
				MovePlane.RenderData->Render(ViewExtension, false);
			}

			MoveAxisX->LocalToWorldMatrix = LocalToWorld;
			MoveAxisX->Render(ViewExtension, false);
			MoveAxisY->LocalToWorldMatrix = LocalToWorld;
			MoveAxisY->Render(ViewExtension, false);
			MoveAxisZ->LocalToWorldMatrix = LocalToWorld;
			MoveAxisZ->Render(ViewExtension, false);
		}
		else if (TransformType == ETransformType::Rotate)
		{
			RotateAxisX->LocalToWorldMatrix = LocalToWorld;
			RotateAxisX->Render(ViewExtension, false);
			RotateAxisY->LocalToWorldMatrix = LocalToWorld;
			RotateAxisY->Render(ViewExtension, false);
			RotateAxisZ->LocalToWorldMatrix = LocalToWorld;
			RotateAxisZ->Render(ViewExtension, false);
		}
	}
	bool IsDragging()const{return bIsDragging;}
	bool HandleInputKey(const FInputKeyEventArgs& EventArgs)
	{
		if (EventArgs.Key == EKeys::LeftMouseButton)
		{
			if (EventArgs.Event == IE_Pressed)
			{
				bIsMousePressedAtThisFrame = true;
				UpdateAxis();
				bIsMousePressedAtThisFrame = false;
				if (MoveAxisType != EMoveAxisType::None || RotateAxisType != ERotateAxisType::None)
				{
					bIsDragging = true;
					PressMouseX = EventArgs.Viewport->GetMouseX();
					PressMouseY = EventArgs.Viewport->GetMouseY();
					ThisTransformWhenPress = ThisTransform;
					Transaction = MakeUnique<FScopedTransaction>(LOCTEXT("MoveWidget", "Move Widget"));
					SelectedWidget->Modify();
					return true;
				}
			}
			else if (EventArgs.Event == IE_Released)
			{
				bIsMouseReleasedAtThisFrame = true;
				MoveAxisType = EMoveAxisType::None;
				RotateAxisType = ERotateAxisType::None;
				if (bIsDragging)
				{
					bIsDragging = false;
					Transaction.Reset();
					return true;
				}
			}
		}
		else if (EventArgs.Key == EKeys::W)
		{
			if (EventArgs.Event == IE_Pressed)
			{
				TransformType = ETransformType::Move;
				return true;
			}
		}
		else if (EventArgs.Key == EKeys::E)
		{
			if (EventArgs.Event == IE_Pressed)
			{
				TransformType = ETransformType::Rotate;
				return true;
			}
		}
		return false;
	}
};

FLexUIPrefabEditorViewportClient::FLexUIPrefabEditorViewportClient(TWeakPtr<FLexUIPrefabEditor> InPrefabEditorPtr
	, const TSharedRef<SLexUIPrefabEditorViewport>& InEditorViewportPtr)
	// UE5.8: pass nullptr (NOT &GLevelEditorModeTools()) so the base creates a PRIVATE
	// FAssetEditorModeManager for this viewport. Sharing the global level-editor mode tools
	// routes InputKey/ProcessClick through the 5.8 Interactive Tools Framework's global
	// InputRouter, which is not valid in a custom asset-editor viewport -> null-deref crash
	// in FEditorViewportClient::InputKey (EditorInteractiveToolsFramework). Same root cause
	// and fix as the LGUI3 Anchor Tool issue.
	: FEditorViewportClient(nullptr, nullptr, StaticCastSharedRef<SEditorViewport>(InEditorViewportPtr))
	, TrackingTransaction()
	, CachedElementsToManipulate(UTypedElementRegistry::GetInstance()->CreateElementList())
{
	PrefabEditorPtr = InPrefabEditorPtr;
	EditorViewportPtr = InEditorViewportPtr;
	ModeTools->SetWidgetMode(UE::Widget::WM_Translate);
	Widget->SetUsesEditorModeTools(ModeTools.Get());
	bShowWidget = false;

	// GEditorModeTools serves as our draw helper
	bUsesDrawHelper = true;

	// DrawHelper set up

	DrawHelper.PerspectiveGridSize = HALF_WORLD_MAX1;
	DrawHelper.AxesLineThickness = 1.0f;
	DrawHelper.bDrawGrid = true;

	EngineShowFlags.Game = 0;
	EngineShowFlags.ScreenSpaceReflections = 1;
	EngineShowFlags.AmbientOcclusion = 1;
	EngineShowFlags.SetSnap(false);

	SetRealtime(true);

	EngineShowFlags.DisableAdvancedFeatures();
	EngineShowFlags.SetSeparateTranslucency(true);
	EngineShowFlags.SetCompositeEditorPrimitives(true);
	EngineShowFlags.SetParticles(true);
	EngineShowFlags.SetSelection(true);
	EngineShowFlags.SetSelectionOutline(true);

	FVector InitialViewLocation;
	FRotator InitialViewRotation;
	FVector InitialViewOrbitLocation;
	ELevelViewportType InitialViewportType;
	InPrefabEditorPtr.Pin()->GetInitialViewSetting(InitialViewLocation, InitialViewRotation, InitialViewOrbitLocation, InitialViewportType);
	SetViewLocation(InitialViewLocation);
	this->ViewportType = InitialViewportType;
	SetViewRotation(InitialViewRotation);
	SetLookAtLocation(InitialViewOrbitLocation);
	GetPrefabBeingEdited()->GetPrefabInstanceScene()->SetSkyCubeVisibility(IsPerspective());

	OnSelectionChangedDelegateHandle = PrefabEditorPtr.Pin()->OnSelectionChanged.AddLambda([=, this]()
	{
		auto SelectedWidgets = PrefabEditorPtr.Pin()->GetSelectedWidgets();
		if (SelectedWidgets.Num() == 1 && SelectedWidgets[0].IsValid()
			&& !PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(SelectedWidgets[0].Get())
			&& !PrefabEditorPtr.Pin()->IsWidgetHiddenInDesigner(SelectedWidgets[0].Get()))
		{
			TransformWidget = MakeUnique<FLexUITransformWidget>(GetWorld(), SelectedWidgets[0].Get(), this);
		}
		else
		{
			TransformWidget.Reset();
		}
	});
}

FLexUIPrefabEditorViewportClient::~FLexUIPrefabEditorViewportClient()
{
	if (PrefabEditorPtr.IsValid())
	{
		PrefabEditorPtr.Pin()->OnSelectionChanged.Remove(OnSelectionChangedDelegateHandle);
	}
}


/**
 * Renders a view frustum specified by the provided frustum parameters
 *
 * @param	PDI					PrimitiveDrawInterface to use to draw the view frustum
 * @param	FrustumColor		Color to draw the view frustum in
 * @param	FrustumAngle		Angle of the frustum
 * @param	FrustumAspectRatio	Aspect ratio of the frustum
 * @param	FrustumStartDist	Start distance of the frustum
 * @param	FrustumEndDist		End distance of the frustum
 * @param	InViewMatrix		View matrix to use to draw the frustum
 */
static void RenderViewFrustum(FPrimitiveDrawInterface* PDI,
	const FLinearColor& FrustumColor,
	float FrustumAngle,
	float FrustumAspectRatio,
	float FrustumStartDist,
	float FrustumEndDist,
	const FMatrix& InViewMatrix)
{
	FVector Direction(0, 0, 1);
	FVector LeftVector(1, 0, 0);
	FVector UpVector(0, 1, 0);

	FVector Verts[8];

	// FOVAngle controls the horizontal angle.
	float HozHalfAngle = (FrustumAngle) * ((float)PI / 360.f);
	float HozLength = FrustumStartDist * FMath::Tan(HozHalfAngle);
	float VertLength = HozLength / FrustumAspectRatio;

	// near plane verts
	Verts[0] = (Direction * FrustumStartDist) + (UpVector * VertLength) + (LeftVector * HozLength);
	Verts[1] = (Direction * FrustumStartDist) + (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[2] = (Direction * FrustumStartDist) - (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[3] = (Direction * FrustumStartDist) - (UpVector * VertLength) + (LeftVector * HozLength);

	HozLength = FrustumEndDist * FMath::Tan(HozHalfAngle);
	VertLength = HozLength / FrustumAspectRatio;

	// far plane verts
	Verts[4] = (Direction * FrustumEndDist) + (UpVector * VertLength) + (LeftVector * HozLength);
	Verts[5] = (Direction * FrustumEndDist) + (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[6] = (Direction * FrustumEndDist) - (UpVector * VertLength) - (LeftVector * HozLength);
	Verts[7] = (Direction * FrustumEndDist) - (UpVector * VertLength) + (LeftVector * HozLength);

	for (int32 x = 0; x < 8; ++x)
	{
		Verts[x] = InViewMatrix.InverseFast().TransformPosition(Verts[x]);
	}

	const uint8 PrimitiveDPG = SDPG_Foreground;
	PDI->DrawLine(Verts[0], Verts[1], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[1], Verts[2], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[2], Verts[3], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[3], Verts[0], FrustumColor, PrimitiveDPG);

	PDI->DrawLine(Verts[4], Verts[5], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[5], Verts[6], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[6], Verts[7], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[7], Verts[4], FrustumColor, PrimitiveDPG);

	PDI->DrawLine(Verts[0], Verts[4], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[1], Verts[5], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[2], Verts[6], FrustumColor, PrimitiveDPG);
	PDI->DrawLine(Verts[3], Verts[7], FrustumColor, PrimitiveDPG);
}
// Frustum parameters for the perspective view.
static float GPerspFrustumAngle=90.f;
static float GPerspFrustumAspectRatio=1.77777f;
static float GPerspFrustumStartDist=GNearClippingPlane;
static float GPerspFrustumEndDist=UE_FLOAT_HUGE_DISTANCE;
static FMatrix GPerspViewMatrix;
void FLexUIPrefabEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FMemMark Mark(FMemStack::Get());

	//Draw grid
	{
#if 0
		auto ScreenColorRenderTargetTexture = View->Family->RenderTarget->GetRenderTargetTexture();
		if (ScreenColorRenderTargetTexture != nullptr)
		{
			static UTexture2D* GridTexture = Cast<UTexture2D>(FAppStyle::GetBrush("Checkerboard")->GetResourceObject());
			if (GridTexture == nullptr)
			{
				GridTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineMaterials/DefaultWhiteGrid.DefaultWhiteGrid"), nullptr, LOAD_None, nullptr);
			}
			const bool bAlphaBlend = false;
			Canvas.DrawTile(
				0,
				0,
				InViewport.GetSizeXY().X,
				InViewport.GetSizeXY().Y,
				0.0f,
				0.0f,
				4.0f,
				4.0f,
				FLinearColor(0.15f, 0.15f, 0.15f),
				GridTexture->GetResource(),
				bAlphaBlend);
		}
#endif
	}

	FEditorViewportClient::Draw(View, PDI);

	//AGroupActor::DrawBracketsForGroups(PDI, Viewport);

	// A frustum should be drawn if the viewport is ortho and level streaming volume previs is enabled in some viewport
	if (IsOrtho())
	{
		for (FLevelEditorViewportClient* CurViewportClient : GEditor->GetLevelViewportClients())
		{
			if (CurViewportClient && IsPerspective() && GetDefault<ULevelEditorViewportSettings>()->bLevelStreamingVolumePrevis)
			{
				// Draw the view frustum of the level streaming volume previs viewport.
				RenderViewFrustum(PDI, FLinearColor(1.0, 0.0, 1.0, 1.0),
					GPerspFrustumAngle,
					GPerspFrustumAspectRatio,
					GPerspFrustumStartDist,
					GPerspFrustumEndDist,
					GPerspViewMatrix);

				break;
			}
		}
	}

	if (GEditor->bEnableSocketSnapping)
	{
		const bool bGameViewMode = View->Family->EngineShowFlags.Game && !GEditor->bDrawSocketsInGMode;

		for (FActorIterator It(GetWorld()); It; ++It)
		{
			AActor* Actor = *It;

			if (bGameViewMode || Actor->IsHiddenEd())
			{
				// Don't display sockets on hidden actors...
				continue;
			}

			for (UActorComponent* Component : Actor->GetComponents())
			{
				USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
				if (SceneComponent && SceneComponent->HasAnySockets())
				{
					TArray<FComponentSocketDescription> Sockets;
					SceneComponent->QuerySupportedSockets(Sockets);

					for (int32 SocketIndex = 0; SocketIndex < Sockets.Num(); ++SocketIndex)
					{
						FComponentSocketDescription& Socket = Sockets[SocketIndex];

						if (Socket.Type == EComponentSocketType::Socket)
						{
							const FTransform SocketTransform = SceneComponent->GetSocketTransform(Socket.Name);

							const float DiamondSize = 2.0f;
							const FColor DiamondColor(255, 128, 128);

							PDI->SetHitProxy(new HLevelSocketProxy(*It, SceneComponent, Socket.Name));
							DrawWireDiamond(PDI, SocketTransform.ToMatrixWithScale(), DiamondSize, DiamondColor, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}
					}
				}
			}
		}
	}

	//if (this == GCurrentLevelEditingViewportClient)
	//{
	//	FSnappingUtils::DrawSnappingHelpers(View, PDI);
	//}

	if (GUnrealEd != NULL && !IsInGameView())
	{
		GUnrealEd->DrawComponentVisualizers(View, PDI);
	}

	if (GEditor->bDrawParticleHelpers == true)
	{
		if (View->Family->EngineShowFlags.Game)
		{
			extern ENGINE_API void DrawParticleSystemHelpers(const FSceneView * View, FPrimitiveDrawInterface * PDI);
			DrawParticleSystemHelpers(View, PDI);
		}
	}

	Mark.Pop();
}
void FLexUIPrefabEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{	
	if (GUnrealEd != nullptr && !IsInGameView())
	{
		GUnrealEd->DrawComponentVisualizersHUD(&InViewport, &View, &Canvas);
	}

	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
	DrawDesignerOverlay(InViewport, View, Canvas);
	DrawAnimationModeIndicator(InViewport, Canvas);
}

void FLexUIPrefabEditorViewportClient::DrawAnimationModeIndicator(FViewport& InViewport, FCanvas& Canvas) const
{
	const TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	ULexUIPrefabSequence* Animation = Editor.IsValid() ? Editor->GetAnimationBeingEdited() : nullptr;
	if (Animation == nullptr)
	{
		return;
	}

	// While an animation is selected Sequencer drives the widgets, so the viewport is showing the
	// animated pose rather than the prefab's design values. Frame it so the two can't be confused.
	const float DpiScale = Canvas.GetDPIScale();
	const FVector2D ViewSize = FVector2D(InViewport.GetSizeXY()) / DpiScale;
	const FLinearColor AccentColor(1.0f, 0.62f, 0.1f);

	const float Inset = 1.0f;
	const FVector2D Corners[4] = {
		FVector2D(Inset, Inset),
		FVector2D(ViewSize.X - Inset, Inset),
		FVector2D(ViewSize.X - Inset, ViewSize.Y - Inset),
		FVector2D(Inset, ViewSize.Y - Inset),
	};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FCanvasLineItem Line(Corners[Index], Corners[(Index + 1) % 4]);
		Line.SetColor(AccentColor);
		Line.LineThickness = 2.0f;
		Canvas.DrawItem(Line);
	}

	UFont* Font = GEngine->GetSmallFont();
	if (Font == nullptr)
	{
		return;
	}
	const FString Label = FString::Printf(TEXT("ANIMATION MODE   %s"), *Animation->GetDisplayNameString());
	const FVector2D Padding(8.0f, 4.0f);
	const FVector2D ChipSize = FVector2D(Font->GetStringSize(*Label), Font->GetMaxCharHeight()) + Padding * 2.0f;
	const FVector2D ChipPos((ViewSize.X - ChipSize.X) * 0.5f, 6.0f);

	FCanvasTileItem Chip(ChipPos, ChipSize, FLinearColor(0.0f, 0.0f, 0.0f, 0.65f));
	Chip.BlendMode = SE_BLEND_Translucent;
	Canvas.DrawItem(Chip);

	FCanvasTextItem Text(ChipPos + Padding, FText::FromString(Label), Font, AccentColor);
	Canvas.DrawItem(Text);
}

void FLexUIPrefabEditorViewportClient::AutoKeyAnimatedTransform(const TArray<ULexWidget*>& InWidgets, bool bLocation, bool bRotation, bool bScale) const
{
	const TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	if (!Editor.IsValid() || !Editor->IsInAnimationEditMode())
	{
		return;
	}
	const TSharedPtr<SLexUIPrefabSequenceEditor> SequencerEditor = Editor->GetSequencerEditor();
	const TSharedPtr<ISequencer> Sequencer = SequencerEditor.IsValid() ? SequencerEditor->GetSequencer() : nullptr;
	if (!Sequencer.IsValid())
	{
		return;
	}

	TArray<UObject*> ObjectsToKey;
	for (ULexWidget* KeyedWidget : InWidgets)
	{
		if (IsValid(KeyedWidget))
		{
			ObjectsToKey.Add(KeyedWidget);
		}
	}
	if (ObjectsToKey.IsEmpty())
	{
		return;
	}

	auto KeyPropertyNamed = [&Sequencer, &ObjectsToKey](const TCHAR* InPropertyName)
	{
		if (FProperty* Property = ULexWidget::StaticClass()->FindPropertyByName(InPropertyName))
		{
			FPropertyPath PropertyPath;
			PropertyPath.AddProperty(FPropertyInfo(Property));
			Sequencer->KeyProperty(FKeyPropertyParams(ObjectsToKey, PropertyPath, ESequencerKeyMode::ManualKeyForced));
		}
	};

	if (bLocation)KeyPropertyNamed(TEXT("RelativeLocation"));
	// Rotation is keyed through the euler mirror; Sequencer has no track for the FQuat itself.
	if (bRotation)KeyPropertyNamed(TEXT("RelativeRotationEuler"));
	if (bScale)KeyPropertyNamed(TEXT("RelativeScale"));
}

bool FLexUIPrefabEditorViewportClient::UpdateDesignerScreenGeometry(FSceneView& View)
{
	DesignerScreenCorners.Reset();
	DesignerHandlePositions.Reset();
	DesignerScreenBounds = FBox2D(EForceInit::ForceInit);
	if (!IsOrtho() || !PrefabEditorPtr.IsValid())return false;
	TArray<ULexWidget*> Selected;
	for (const TWeakObjectPtr<ULexWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
	{
		if (ULexWidget* SelectedWidget = WeakWidget.Get())
		{
			if (!PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(SelectedWidget) && !PrefabEditorPtr.Pin()->IsWidgetHiddenInDesigner(SelectedWidget))Selected.Add(SelectedWidget);
		}
	}
	if (Selected.IsEmpty())return false;

	auto ProjectWidgetCorners = [&View](ULexWidget* InWidget, TArray<FVector2D>& OutCorners) -> bool
	{
		const float Left = -InWidget->GetPivot().X * InWidget->GetWidth();
		const float Right = (1.0f - InWidget->GetPivot().X) * InWidget->GetWidth();
		const float Bottom = -InWidget->GetPivot().Y * InWidget->GetHeight();
		const float Top = (1.0f - InWidget->GetPivot().Y) * InWidget->GetHeight();
		const FTransform& Transform = InWidget->GetWorldTransform();
		for (const FVector& Local : { FVector(0, Left, Bottom), FVector(0, Right, Bottom), FVector(0, Right, Top), FVector(0, Left, Top) })
		{
			FVector2D Pixel;
			if (!View.WorldToPixel(Transform.TransformPosition(Local), Pixel))return false;
			OutCorners.Add(Pixel);
		}
		return true;
	};

	TArray<FVector2D> SingleCorners;
	for (ULexWidget* SelectedWidget : Selected)
	{
		TArray<FVector2D> Corners;
		if (!ProjectWidgetCorners(SelectedWidget, Corners))continue;
		for (const FVector2D& Corner : Corners)DesignerScreenBounds += Corner;
		if (Selected.Num() == 1)SingleCorners = MoveTemp(Corners);
	}
	if (!DesignerScreenBounds.bIsValid)return false;

	if (Selected.Num() == 1 && SingleCorners.Num() == 4)
	{
		DesignerScreenCorners = SingleCorners;
		ULexWidget* SelectedWidget = Selected[0];
		const bool bLayoutControlled = SelectedWidget->GetParent() && SelectedWidget->GetParent()->GetLayoutContainer();
		if (!bLayoutControlled)
		{
			DesignerHandlePositions.Add(EDesignerHandle::BottomLeft, SingleCorners[0]);
			DesignerHandlePositions.Add(EDesignerHandle::BottomRight, SingleCorners[1]);
			DesignerHandlePositions.Add(EDesignerHandle::TopRight, SingleCorners[2]);
			DesignerHandlePositions.Add(EDesignerHandle::TopLeft, SingleCorners[3]);
			DesignerHandlePositions.Add(EDesignerHandle::Bottom, (SingleCorners[0] + SingleCorners[1]) * 0.5f);
			DesignerHandlePositions.Add(EDesignerHandle::Right, (SingleCorners[1] + SingleCorners[2]) * 0.5f);
			DesignerHandlePositions.Add(EDesignerHandle::Top, (SingleCorners[2] + SingleCorners[3]) * 0.5f);
			DesignerHandlePositions.Add(EDesignerHandle::Left, (SingleCorners[3] + SingleCorners[0]) * 0.5f);
		}
		FVector2D PivotPixel;
		if (View.WorldToPixel(SelectedWidget->GetWorldTransform().GetLocation(), PivotPixel))DesignerHandlePositions.Add(EDesignerHandle::Pivot, PivotPixel);
	}
	else
	{
		DesignerScreenCorners = {
			FVector2D(DesignerScreenBounds.Min.X, DesignerScreenBounds.Max.Y),
			DesignerScreenBounds.Max,
			FVector2D(DesignerScreenBounds.Max.X, DesignerScreenBounds.Min.Y),
			DesignerScreenBounds.Min
		};
	}
	return DesignerScreenCorners.Num() == 4;
}

FLexUIPrefabEditorViewportClient::EDesignerHandle FLexUIPrefabEditorViewportClient::HitTestDesignerHandle(const FVector2D& PixelPosition) const
{
	constexpr float HandleRadius = 9.0f;
	if (const FVector2D* Pivot = DesignerHandlePositions.Find(EDesignerHandle::Pivot))
	{
		if (FVector2D::Distance(*Pivot, PixelPosition) <= HandleRadius)return EDesignerHandle::Pivot;
	}
	for (const auto& Pair : DesignerHandlePositions)
	{
		if (Pair.Key != EDesignerHandle::Pivot && FVector2D::Distance(Pair.Value, PixelPosition) <= HandleRadius)return Pair.Key;
	}
	if (DesignerScreenBounds.bIsValid
		&& PixelPosition.X >= DesignerScreenBounds.Min.X && PixelPosition.X <= DesignerScreenBounds.Max.X
		&& PixelPosition.Y >= DesignerScreenBounds.Min.Y && PixelPosition.Y <= DesignerScreenBounds.Max.Y)
	{
		return EDesignerHandle::Move;
	}
	return EDesignerHandle::None;
}

bool FLexUIPrefabEditorViewportClient::IntersectDesignerPlane(const FVector2D& PixelPosition, const FTransform& PlaneTransform, FVector& OutPoint) const
{
	if (!Viewport)return false;
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = const_cast<FLexUIPrefabEditorViewportClient*>(this)->CalcSceneView(&ViewFamily);
	if (!View)return false;
	FVector RayOrigin, RayDirection;
	FSceneView::DeprojectScreenToWorld(PixelPosition, View->UnscaledViewRect,
		View->ViewMatrices.GetClipToWorld(), RayOrigin, RayDirection);
	OutPoint = FMath::LinePlaneIntersection(RayOrigin, RayOrigin + RayDirection * 100000000.0f,
		PlaneTransform.GetLocation(), PlaneTransform.GetUnitAxis(EAxis::X));
	return true;
}

void FLexUIPrefabEditorViewportClient::DrawWidgetScreenOutline(ULexWidget* InWidget, FSceneView& View, FCanvas& Canvas,
	const FLinearColor& Color, float Thickness) const
{
	if (!InWidget)return;
	const float Left = -InWidget->GetPivot().X * InWidget->GetWidth();
	const float Right = (1.0f - InWidget->GetPivot().X) * InWidget->GetWidth();
	const float Bottom = -InWidget->GetPivot().Y * InWidget->GetHeight();
	const float Top = (1.0f - InWidget->GetPivot().Y) * InWidget->GetHeight();
	const FTransform& Transform = InWidget->GetWorldTransform();
	TArray<FVector2D> Corners;
	for (const FVector& Local : { FVector(0, Left, Bottom), FVector(0, Right, Bottom), FVector(0, Right, Top), FVector(0, Left, Top) })
	{
		FVector2D Pixel;
		if (!View.WorldToPixel(Transform.TransformPosition(Local), Pixel))return;
		Corners.Add(Pixel / Canvas.GetDPIScale());
	}
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FCanvasLineItem Line(Corners[Index], Corners[(Index + 1) % 4]);
		Line.SetColor(Color);
		Line.LineThickness = Thickness;
		Canvas.DrawItem(Line);
	}
}

void FLexUIPrefabEditorViewportClient::DrawDesignerCanvasBoundary(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const
{
	const TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	ULexWidget* RootAgent = Editor.IsValid() ? Editor->GetRootAgentWidget() : nullptr;
	if (!IsValid(RootAgent) || RootAgent->GetWidth() <= 0.0f || RootAgent->GetHeight() <= 0.0f)
	{
		return;
	}

	const float Left = -RootAgent->GetPivot().X * RootAgent->GetWidth();
	const float Right = (1.0f - RootAgent->GetPivot().X) * RootAgent->GetWidth();
	const float Bottom = -RootAgent->GetPivot().Y * RootAgent->GetHeight();
	const float Top = (1.0f - RootAgent->GetPivot().Y) * RootAgent->GetHeight();
	const FTransform& Transform = RootAgent->GetWorldTransform();
	const float DpiScale = Canvas.GetDPIScale();

	TArray<FVector2D> Corners;
	FBox2D Bounds(EForceInit::ForceInit);
	for (const FVector& Local : { FVector(0, Left, Bottom), FVector(0, Right, Bottom), FVector(0, Right, Top), FVector(0, Left, Top) })
	{
		FVector2D Pixel;
		if (!View.WorldToPixel(Transform.TransformPosition(Local), Pixel))
		{
			return;
		}
		Pixel /= DpiScale;
		Corners.Add(Pixel);
		Bounds += Pixel;
	}

	const FVector2D ViewSize = FVector2D(InViewport.GetSizeXY()) / DpiScale;
	const bool bIntersectsViewport = Bounds.Max.X > 0.0f && Bounds.Max.Y > 0.0f
		&& Bounds.Min.X < ViewSize.X && Bounds.Min.Y < ViewSize.Y;
	if (bIntersectsViewport)
	{
		const float MinX = FMath::Clamp(Bounds.Min.X, 0.0f, ViewSize.X);
		const float MaxX = FMath::Clamp(Bounds.Max.X, 0.0f, ViewSize.X);
		const float MinY = FMath::Clamp(Bounds.Min.Y, 0.0f, ViewSize.Y);
		const float MaxY = FMath::Clamp(Bounds.Max.Y, 0.0f, ViewSize.Y);
		const FLinearColor OutsideColor(0.0f, 0.0f, 0.0f, 0.32f);

		auto DrawOutsideTile = [&Canvas, &OutsideColor](const FVector2D& Position, const FVector2D& Size)
		{
			if (Size.X <= 0.0f || Size.Y <= 0.0f)
			{
				return;
			}
			FCanvasTileItem Tile(Position, Size, OutsideColor);
			Tile.BlendMode = SE_BLEND_Translucent;
			Canvas.DrawItem(Tile);
		};

		DrawOutsideTile(FVector2D::ZeroVector, FVector2D(ViewSize.X, MinY));
		DrawOutsideTile(FVector2D(0.0f, MaxY), FVector2D(ViewSize.X, ViewSize.Y - MaxY));
		DrawOutsideTile(FVector2D(0.0f, MinY), FVector2D(MinX, MaxY - MinY));
		DrawOutsideTile(FVector2D(MaxX, MinY), FVector2D(ViewSize.X - MaxX, MaxY - MinY));
	}

	for (int32 Index = 0; Index < Corners.Num(); ++Index)
	{
		const FVector2D& Start = Corners[Index];
		const FVector2D& End = Corners[(Index + 1) % Corners.Num()];

		FCanvasLineItem Shadow(Start, End);
		Shadow.SetColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
		Shadow.LineThickness = 4.0f;
		Canvas.DrawItem(Shadow);

		FCanvasLineItem Border(Start, End);
		Border.SetColor(FLinearColor(0.72f, 0.82f, 0.9f, 1.0f));
		Border.LineThickness = 2.0f;
		Canvas.DrawItem(Border);
	}
}

void FLexUIPrefabEditorViewportClient::DrawResolutionGuides(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const
{
	const TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	ULexWidget* RootAgent = Editor.IsValid() ? Editor->GetRootAgentWidget() : nullptr;
	if (!IsValid(RootAgent))
	{
		return;
	}
	// Anchor every resolution rect at the design canvas's top-left corner, mirroring how UMG's
	// designer stacks its device-size overlay.
	const float Left = -RootAgent->GetPivot().X * RootAgent->GetWidth();
	const float Top = (1.0f - RootAgent->GetPivot().Y) * RootAgent->GetHeight();
	const FTransform& Transform = RootAgent->GetWorldTransform();
	const float DpiScale = Canvas.GetDPIScale();
	UFont* Font = GEngine->GetSmallFont();
	// Draw the canvas rect each device resolution actually produces, not the raw resolution: with a
	// scale rule in play those are different rectangles, and the raw one is a shape this prefab will
	// never be laid out at. Resolutions sharing an aspect collapse onto one canvas rect, so dedupe
	// instead of stacking identical outlines and labels on top of each other.
	TArray<FIntPoint> GuideSizes;
	for (const FLexUIDesignScreenSize& ScreenSize : GetLexUIDesignScreenSizes())
	{
		FIntPoint GuideSize = ScreenSize.Size;
		float GuideScale = 1.0f;
		Editor->CalculateDesignerCanvasFor(ScreenSize.Size, GuideSize, GuideScale);
		GuideSizes.AddUnique(GuideSize);
	}
	for (int32 Index = 0; Index < GuideSizes.Num(); Index++)
	{
		const FIntPoint Size = GuideSizes[Index];
		const FVector LocalCorners[4] = {
			FVector(0, Left, Top),
			FVector(0, Left + Size.X, Top),
			FVector(0, Left + Size.X, Top - Size.Y),
			FVector(0, Left, Top - Size.Y),
		};
		FVector2D Pixels[4];
		FBox2D Bounds(EForceInit::ForceInit);
		bool bProjected = true;
		for (int32 Corner = 0; Corner < 4; Corner++)
		{
			if (!View.WorldToPixel(Transform.TransformPosition(LocalCorners[Corner]), Pixels[Corner]))
			{
				bProjected = false;
				break;
			}
			Pixels[Corner] /= DpiScale;
			Bounds += Pixels[Corner];
		}
		if (!bProjected)
		{
			continue;
		}
		const float Fade = GuideSizes.Num() > 1 ? (float)Index / (GuideSizes.Num() - 1) : 0.0f;
		const FLinearColor GuideColor = FMath::Lerp(
			FLinearColor(0.05f, 0.45f, 0.95f, 0.85f), FLinearColor(0.65f, 0.85f, 1.0f, 0.85f), Fade);
		for (int32 Corner = 0; Corner < 4; Corner++)
		{
			FCanvasLineItem Line(Pixels[Corner], Pixels[(Corner + 1) % 4]);
			Line.SetColor(GuideColor);
			Line.LineThickness = 1.0f;
			Canvas.DrawItem(Line);
		}
		const FString Label = FString::Printf(TEXT("%d x %d"), Size.X, Size.Y);
		const float LabelWidth = Font ? (float)Font->GetStringSize(*Label) : 60.0f;
		FCanvasTextItem Text(FVector2D(Bounds.Max.X - LabelWidth - 4.0f, Bounds.Max.Y - 16.0f),
			FText::FromString(Label), Font, GuideColor);
		Text.EnableShadow(FLinearColor::Black);
		Canvas.DrawItem(Text);
	}
}

void FLexUIPrefabEditorViewportClient::DrawShippedImageOutline(ULexWidget* InWidget, FSceneView& View, FCanvas& Canvas) const
{
	// The 2D view is orthographic, and an orthographic projection has no perspective divide: a
	// widget pushed away in depth keeps exactly its laid-out size on screen. Perspective is
	// therefore invisible here however the camera is calibrated -- not because the remap did not
	// run, but because the last step discards what it did. Trading the design surface for a
	// perspective projection would cost the handles, the ruler, the grid and arrow-key nudging,
	// all of which key off IsOrtho or off the projection matrix itself. So instead the shipped
	// position is computed with the canvas's own matrices and drawn alongside: the author sees
	// layout and shipped image at once, rather than having one replace the other.
	if (!IsValid(InWidget))return;
	ULexCanvas* RootCanvas = GetPreviewRootCanvas();
	if (RootCanvas == nullptr
		|| RootCanvas->IsRenderToWorldSpace()
		|| RootCanvas->GetProjectionType() != ECameraProjectionMode::Perspective)
	{
		return;//no virtual camera to ship through, so the shipped image IS the layout
	}

	const float Left = -InWidget->GetPivot().X * InWidget->GetWidth();
	const float Right = (1.0f - InWidget->GetPivot().X) * InWidget->GetWidth();
	const float Bottom = -InWidget->GetPivot().Y * InWidget->GetHeight();
	const float Top = (1.0f - InWidget->GetPivot().Y) * InWidget->GetHeight();
	const FTransform& LayoutTransform = InWidget->GetWorldTransform();
	const float DpiScale = Canvas.GetDPIScale();

	TArray<FVector2D> Shipped;
	double MaxDeviation = 0.0;
	for (const FVector& Local : { FVector(0, Left, Bottom), FVector(0, Right, Bottom), FVector(0, Right, Top), FVector(0, Left, Top) })
	{
		FVector OnPlane;
		if (!RootCanvas->ProjectWorldPointOntoCanvasPlane(FVector(InWidget->GetWorldMatrix().TransformPosition(Local)), OnPlane))return;
		FVector2D Pixel, LayoutPixel;
		if (!View.WorldToPixel(OnPlane, Pixel))return;
		if (!View.WorldToPixel(LayoutTransform.TransformPosition(Local), LayoutPixel))return;
		MaxDeviation = FMath::Max(MaxDeviation, FVector2D::Distance(Pixel, LayoutPixel));
		Shipped.Add(Pixel / DpiScale);
	}
	// Silent when the two coincide. Flat content ships where it is laid out, so drawing this
	// unconditionally would put a second outline on every selection forever and train authors to
	// ignore it -- costing exactly the cases it exists to show.
	if (MaxDeviation <= 1.0)return;

	const FLinearColor ShippedColor(1.0f, 0.35f, 0.75f);
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FCanvasLineItem Line(Shipped[Index], Shipped[(Index + 1) % 4]);
		Line.SetColor(ShippedColor);
		Line.LineThickness = 1.5f;
		Canvas.DrawItem(Line);
	}
	FBox2D ShippedBounds(EForceInit::ForceInit);
	for (const FVector2D& Corner : Shipped)ShippedBounds += Corner;
	FCanvasTextItem Label(FVector2D(ShippedBounds.Min.X, ShippedBounds.Max.Y + 2.0f),
		NSLOCTEXT("LexUIPrefabEditor", "ShippedImageOutline", "shipped"),
		GEngine->GetSmallFont(), ShippedColor);
	Label.EnableShadow(FLinearColor::Black);
	Canvas.DrawItem(Label);
}

void FLexUIPrefabEditorViewportClient::DrawDesignerOverlay(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	if (!IsOrtho())return;
	DrawDesignerCanvasBoundary(InViewport, View, Canvas);
	if (PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->GetShowResolutionGuides())
	{
		DrawResolutionGuides(InViewport, View, Canvas);
	}
	DrawLayoutDebugOverlay(InViewport, Canvas);
	if (PaletteDropPreviewWidget.IsValid())
	{
		DrawWidgetScreenOutline(PaletteDropPreviewWidget.Get(), View, Canvas, FLinearColor(1.0f, 0.55f, 0.05f), 2.0f);
	}
	if (PrefabEditorPtr.IsValid())
	{
		for (const TWeakObjectPtr<ULexWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
		{
			DrawShippedImageOutline(WeakWidget.Get(), View, Canvas);
		}
	}
	if (!UpdateDesignerScreenGeometry(View))return;
	const float DpiScale = Canvas.GetDPIScale();
	const FLinearColor OutlineColor(0.1f, 0.65f, 1.0f);
	for (int32 Index = 0; Index < DesignerScreenCorners.Num(); ++Index)
	{
		FCanvasLineItem Line(DesignerScreenCorners[Index] / DpiScale, DesignerScreenCorners[(Index + 1) % DesignerScreenCorners.Num()] / DpiScale);
		Line.SetColor(OutlineColor);
		Line.LineThickness = 1.5f;
		Canvas.DrawItem(Line);
	}
	for (const auto& Pair : DesignerHandlePositions)
	{
		const bool bPivot = Pair.Key == EDesignerHandle::Pivot;
		const float Size = bPivot ? 7.0f : 8.0f;
		FCanvasTileItem Tile(Pair.Value / DpiScale - FVector2D(Size * 0.5f), FVector2D(Size),
			bPivot ? FLinearColor(1.0f, 0.7f, 0.05f) : FLinearColor(0.05f, 0.45f, 0.9f));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(Tile);
	}
	if (PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->GetSelectedWidgets().Num() == 1)
	{
		if (ULexWidget* SelectedWidget = PrefabEditorPtr.Pin()->GetSelectedWidgets()[0].Get())
		{
			FCanvasTextItem SizeText(DesignerScreenBounds.Min / DpiScale + FVector2D(0, -16),
				FText::FromString(FString::Printf(TEXT("%.0f x %.0f"), SelectedWidget->GetWidth(), SelectedWidget->GetHeight())),
				GEngine->GetSmallFont(), FLinearColor::White);
			SizeText.EnableShadow(FLinearColor::Black);
			Canvas.DrawItem(SizeText);
		}
	}
	if (bDesignerDragging && PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->GetShowDesignerGuides())
	{
		const FVector2D ViewSize = FVector2D(InViewport.GetSizeXY()) / DpiScale;
		if (DesignerGuideX.IsSet())
		{
			const float X = DesignerGuideX.GetValue() / DpiScale;
			FCanvasLineItem Guide(FVector2D(X, 0), FVector2D(X, ViewSize.Y));
			Guide.SetColor(FLinearColor(1.0f, 0.25f, 0.65f, 0.9f));
			Canvas.DrawItem(Guide);
		}
		if (DesignerGuideY.IsSet())
		{
			const float Y = DesignerGuideY.GetValue() / DpiScale;
			FCanvasLineItem Guide(FVector2D(0, Y), FVector2D(ViewSize.X, Y));
			Guide.SetColor(FLinearColor(1.0f, 0.25f, 0.65f, 0.9f));
			Canvas.DrawItem(Guide);
		}
	}
}

void FLexUIPrefabEditorViewportClient::DrawLayoutDebugOverlay(FViewport& InViewport, FCanvas& Canvas) const
{
	const TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	if (!Editor.IsValid() || !Editor->GetShowLayoutDebug() || Editor->GetSelectedWidgets().Num() != 1)
	{
		return;
	}

	ULexWidget* SelectedWidget = Editor->GetSelectedWidgets()[0].Get();
	if (!IsValid(SelectedWidget))
	{
		return;
	}
	ULexLayoutContainer* Layout = nullptr;
	if (ULexWidget* Parent = SelectedWidget->GetParent(); IsValid(Parent))
	{
		Layout = Parent->GetLayoutContainer();
	}
	if (!IsValid(Layout))
	{
		Layout = SelectedWidget->GetLayoutContainer();
	}
	FLexLayoutDebugInfo Info;
	if (!IsValid(Layout) || !Layout->GetLayoutDebugInfo(SelectedWidget, Info))
	{
		return;
	}

	auto SizeLine = [](const TCHAR* Label, const FVector2D& Value)
	{
		return FString::Printf(TEXT("%s %.1f x %.1f"), Label, Value.X, Value.Y);
	};
	auto Compact = [](const FString& Value)
	{
		return Value.Len() > 76 ? Value.Left(73) + TEXT("...") : Value;
	};
	const TArray<FString> Lines = {
		Compact(Info.Algorithm),
		SizeLine(TEXT("Desired"), Info.DesiredSize),
		FString::Printf(TEXT("Arranged %.1f x %.1f at %.1f, %.1f"), Info.ArrangedSize.X, Info.ArrangedSize.Y,
			Info.ArrangedPosition.X, Info.ArrangedPosition.Y),
		SizeLine(TEXT("Authored"), Info.AuthoredSize) + TEXT("   ") + SizeLine(TEXT("Bounds"), Info.ContentBounds),
		Compact(FString(TEXT("Slot: ")) + Info.SlotRule),
		Compact(FString(TEXT("Position: ")) + Info.PositionOwner),
		Compact(FString(TEXT("Size: ")) + Info.SizeOwner),
		Compact(FString(TEXT("Clipping: ")) + Info.Clipping),
	};

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!Font)
	{
		return;
	}
	const float DpiScale = Canvas.GetDPIScale();
	const FVector2D ViewSize = FVector2D(InViewport.GetSizeXY()) / DpiScale;
	const float LineHeight = Font->GetMaxCharHeight() + 3.0f;
	const FVector2D Padding(10.0f, 8.0f);
	float TextWidth = 0.0f;
	for (const FString& Line : Lines)
	{
		TextWidth = FMath::Max(TextWidth, static_cast<float>(Font->GetStringSize(*Line)));
	}
	const float MaxPanelWidth = FMath::Max(240.0f, ViewSize.X - 24.0f);
	const FVector2D PanelSize(FMath::Min(TextWidth + Padding.X * 2.0f, MaxPanelWidth),
		Lines.Num() * LineHeight + Padding.Y * 2.0f);
	const FVector2D PanelPosition(FMath::Max(12.0f, ViewSize.X - PanelSize.X - 12.0f), 42.0f);
	FCanvasTileItem Background(PanelPosition, PanelSize, FLinearColor(0.015f, 0.02f, 0.025f, 0.9f));
	Background.BlendMode = SE_BLEND_Translucent;
	Canvas.DrawItem(Background);

	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		FCanvasTextItem Text(PanelPosition + Padding + FVector2D(0.0f, Index * LineHeight),
			FText::FromString(Lines[Index]), Font,
			Index == 0 ? FLinearColor(0.2f, 0.75f, 1.0f) : FLinearColor::White);
		Text.EnableShadow(FLinearColor::Black);
		Canvas.DrawItem(Text);
	}
}

void FLexUIPrefabEditorViewportClient::ReceivedFocus(FViewport* InViewport)
{
	if (!bReceivedFocusRecently)
	{ 
		bReceivedFocusRecently = true;

		// A few frames can pass between receiving focus and processing a click, so we use a timer to track whether we have recently received focus.
		FTimerDelegate ResetFocusReceivedTimer;
		ResetFocusReceivedTimer.BindLambda([&]()
			{
				bReceivedFocusRecently = false;
				FocusTimerHandle.Invalidate(); // The timer will only execute once, so we can invalidate now.
			});
		GEditor->GetTimerManager()->SetTimer(FocusTimerHandle, ResetFocusReceivedTimer, 0.1f, false);
	}

	FEditorViewportClient::ReceivedFocus(InViewport);
}

void FLexUIPrefabEditorViewportClient::LostFocus(FViewport* InViewport)
{
	if (bDesignerDragging)FinishDesignerDrag(true);
	bRightMouseButtonDown = false;
	bRightMouseMoved = false;
	RightMouseDownPosition = FIntPoint::ZeroValue;
	FEditorViewportClient::LostFocus(InViewport);

	GEditor->SetPreviewMeshMode(false);
}

void FLexUIPrefabEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	TickWorld(DeltaSeconds);

	SyncViewFOVToCanvas();

	if (bDesignerDragging)UpdateDesignerDrag();
	if (TransformWidget.IsValid() && IsPerspective())
	{
		TransformWidget->Tick();
	}
}


bool FLexUIPrefabEditorViewportClient::HandleDesignerInputKey(const FInputKeyEventArgs& EventArgs)
{
	if (!IsOrtho() || !PrefabEditorPtr.IsValid())return false;
	if (EventArgs.Key == EKeys::Escape && EventArgs.Event == IE_Pressed && bDesignerDragging)
	{
		FinishDesignerDrag(true);
		return true;
	}
	if (EventArgs.Key != EKeys::LeftMouseButton)return false;
	if (EventArgs.Event == IE_Released && bDesignerDragging)
	{
		FinishDesignerDrag(false);
		return true;
	}
	if (EventArgs.Event != IE_Pressed || bDesignerDragging || !Viewport)return false;

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = CalcSceneView(&ViewFamily);
	if (!View || !UpdateDesignerScreenGeometry(*View))return false;
	const FVector2D MousePixel(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
	const EDesignerHandle HitHandle = HitTestDesignerHandle(MousePixel);
	if (HitHandle == EDesignerHandle::None)return false;

	TArray<ULexWidget*> Widgets;
	for (const TWeakObjectPtr<ULexWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
	{
		if (ULexWidget* SelectedWidget = WeakWidget.Get())
		{
			if (!PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(SelectedWidget))Widgets.Add(SelectedWidget);
		}
	}
	if (Widgets.IsEmpty())return false;
	if (HitHandle != EDesignerHandle::Move && Widgets.Num() != 1)return false;

	DesignerSnapshots.Reset();
	DesignerTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("DesignerTransformWidgets", "Transform Widgets"));
	if (ULexUIPrefabHelperObject* Helper = PrefabEditorPtr.Pin()->GetPrefabHelperObject())
	{
		Helper->Modify();
	}
	for (ULexWidget* SelectedWidget : Widgets)
	{
		SelectedWidget->Modify();
		FDesignerWidgetSnapshot& Snapshot = DesignerSnapshots.AddDefaulted_GetRef();
		Snapshot.Widget = SelectedWidget;
		Snapshot.AnchoredPosition = SelectedWidget->GetAnchoredPosition();
		Snapshot.Pivot = SelectedWidget->GetPivot();
		Snapshot.Width = SelectedWidget->GetWidth();
		Snapshot.Height = SelectedWidget->GetHeight();
		Snapshot.WorldTransform = SelectedWidget->GetWorldTransform();
		Snapshot.PlaneTransform = HitHandle == EDesignerHandle::Move && SelectedWidget->GetParent()
			? SelectedWidget->GetParent()->GetWorldTransform() : SelectedWidget->GetWorldTransform();
		IntersectDesignerPlane(MousePixel, Snapshot.PlaneTransform, Snapshot.StartPlanePoint);
	}
	ActiveDesignerHandle = HitHandle;
	DesignerDragStartPixel = MousePixel;
	bDesignerDragging = true;
	bDesignerChanged = false;
	DesignerGuideX.Reset();
	DesignerGuideY.Reset();
	return true;
}

void FLexUIPrefabEditorViewportClient::UpdateDesignerDrag()
{
	if (!bDesignerDragging || !Viewport || DesignerSnapshots.IsEmpty() || !PrefabEditorPtr.IsValid())return;
	const FVector2D MousePixel(Viewport->GetMouseX(), Viewport->GetMouseY());
	DesignerGuideX.Reset();
	DesignerGuideY.Reset();

	if (ActiveDesignerHandle == EDesignerHandle::Move)
	{
		FVector2D SnappedDelta = FVector2D::ZeroVector;
		bool bHasSnappedDelta = false;
		for (FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
		{
			ULexWidget* SelectedWidget = Snapshot.Widget.Get();
			if (!SelectedWidget)continue;
			FVector CurrentPoint;
			if (!IntersectDesignerPlane(MousePixel, Snapshot.PlaneTransform, CurrentPoint))continue;
			const FVector StartLocal = Snapshot.PlaneTransform.InverseTransformPosition(Snapshot.StartPlanePoint);
			const FVector CurrentLocal = Snapshot.PlaneTransform.InverseTransformPosition(CurrentPoint);
			const FVector2D Delta(CurrentLocal.Y - StartLocal.Y, CurrentLocal.Z - StartLocal.Z);
			if (!bHasSnappedDelta)
			{
				const FVector2D FirstPosition = Snapshot.AnchoredPosition + Delta;
				SnappedDelta.X = PrefabEditorPtr.Pin()->SnapDesignerValue(FirstPosition.X) - Snapshot.AnchoredPosition.X;
				SnappedDelta.Y = PrefabEditorPtr.Pin()->SnapDesignerValue(FirstPosition.Y) - Snapshot.AnchoredPosition.Y;
				bHasSnappedDelta = true;
			}
			SelectedWidget->SetAnchoredPosition(Snapshot.AnchoredPosition + SnappedDelta);
		}
	}
	else
	{
		FDesignerWidgetSnapshot& Snapshot = DesignerSnapshots[0];
		ULexWidget* SelectedWidget = Snapshot.Widget.Get();
		if (!SelectedWidget)return;
		FVector CurrentPoint;
		if (!IntersectDesignerPlane(MousePixel, Snapshot.WorldTransform, CurrentPoint))return;
		const FVector LocalPoint = Snapshot.WorldTransform.InverseTransformPosition(CurrentPoint);
		const float OriginalLeft = -Snapshot.Pivot.X * Snapshot.Width;
		const float OriginalRight = (1.0f - Snapshot.Pivot.X) * Snapshot.Width;
		const float OriginalBottom = -Snapshot.Pivot.Y * Snapshot.Height;
		const float OriginalTop = (1.0f - Snapshot.Pivot.Y) * Snapshot.Height;

		if (ActiveDesignerHandle == EDesignerHandle::Pivot)
		{
			const FVector2D NewPivot(
				FMath::Clamp((LocalPoint.Y - OriginalLeft) / FMath::Max(1.0f, Snapshot.Width), 0.0f, 1.0f),
				FMath::Clamp((LocalPoint.Z - OriginalBottom) / FMath::Max(1.0f, Snapshot.Height), 0.0f, 1.0f));
			const FVector NewOriginLocal(0, OriginalLeft + NewPivot.X * Snapshot.Width, OriginalBottom + NewPivot.Y * Snapshot.Height);
			SelectedWidget->SetPivot(NewPivot);
			SelectedWidget->SetWorldLocation(Snapshot.WorldTransform.TransformPosition(NewOriginLocal));
		}
		else
		{
			const bool bChangeLeft = ActiveDesignerHandle == EDesignerHandle::Left || ActiveDesignerHandle == EDesignerHandle::TopLeft || ActiveDesignerHandle == EDesignerHandle::BottomLeft;
			const bool bChangeRight = ActiveDesignerHandle == EDesignerHandle::Right || ActiveDesignerHandle == EDesignerHandle::TopRight || ActiveDesignerHandle == EDesignerHandle::BottomRight;
			const bool bChangeTop = ActiveDesignerHandle == EDesignerHandle::Top || ActiveDesignerHandle == EDesignerHandle::TopLeft || ActiveDesignerHandle == EDesignerHandle::TopRight;
			const bool bChangeBottom = ActiveDesignerHandle == EDesignerHandle::Bottom || ActiveDesignerHandle == EDesignerHandle::BottomLeft || ActiveDesignerHandle == EDesignerHandle::BottomRight;
			float Left = OriginalLeft;
			float Right = OriginalRight;
			float Bottom = OriginalBottom;
			float Top = OriginalTop;
			if (bChangeLeft)Left = FMath::Min(PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPoint.Y), Right - 1.0f);
			if (bChangeRight)Right = FMath::Max(PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPoint.Y), Left + 1.0f);
			if (bChangeBottom)Bottom = FMath::Min(PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPoint.Z), Top - 1.0f);
			if (bChangeTop)Top = FMath::Max(PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPoint.Z), Bottom + 1.0f);
			const float NewWidth = Right - Left;
			const float NewHeight = Top - Bottom;
			const FVector NewOriginLocal(0, Left + Snapshot.Pivot.X * NewWidth, Bottom + Snapshot.Pivot.Y * NewHeight);
			SelectedWidget->SetWidth(NewWidth);
			SelectedWidget->SetHeight(NewHeight);
			SelectedWidget->SetWorldLocation(Snapshot.WorldTransform.TransformPosition(NewOriginLocal));
		}
	}
	bDesignerChanged = false;
	for (const FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
	{
		if (const ULexWidget* SelectedWidget = Snapshot.Widget.Get())
		{
			bDesignerChanged = !SelectedWidget->GetAnchoredPosition().Equals(Snapshot.AnchoredPosition)
				|| !SelectedWidget->GetPivot().Equals(Snapshot.Pivot)
				|| !FMath::IsNearlyEqual(SelectedWidget->GetWidth(), Snapshot.Width)
				|| !FMath::IsNearlyEqual(SelectedWidget->GetHeight(), Snapshot.Height)
				|| !SelectedWidget->GetWorldTransform().Equals(Snapshot.WorldTransform);
			if (bDesignerChanged)break;
		}
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	if (FSceneView* View = CalcSceneView(&ViewFamily))
	{
		ULexWidget* GuideWidget = DesignerSnapshots[0].Widget.Get();
		FVector2D GuidePixel;
		if (GuideWidget && View->WorldToPixel(GuideWidget->GetWorldTransform().GetLocation(), GuidePixel))
		{
			DesignerGuideX = GuidePixel.X;
			DesignerGuideY = GuidePixel.Y;
		}
	}
	Invalidate();
}

void FLexUIPrefabEditorViewportClient::FinishDesignerDrag(bool bCancel)
{
	if (!bDesignerDragging)return;
	if (bCancel)
	{
		for (const FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
		{
			if (ULexWidget* SelectedWidget = Snapshot.Widget.Get())
			{
				SelectedWidget->SetPivot(Snapshot.Pivot);
				SelectedWidget->SetWidth(Snapshot.Width);
				SelectedWidget->SetHeight(Snapshot.Height);
				SelectedWidget->SetAnchoredPosition(Snapshot.AnchoredPosition);
				SelectedWidget->SetWorldTransform(Snapshot.WorldTransform);
			}
		}
	}
	else if (bDesignerChanged && PrefabEditorPtr.IsValid())
	{
		if (ULexUIPrefabHelperObject* Helper = PrefabEditorPtr.Pin()->GetPrefabHelperObject())
		{
			Helper->SetAnythingDirty();
		}
		// Dragging in the designer moves the widget by its anchored position, which the widget
		// resolves into RelativeLocation; that is the property the animation keys.
		TArray<ULexWidget*> DraggedWidgets;
		for (const FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
		{
			if (ULexWidget* DraggedWidget = Snapshot.Widget.Get())
			{
				DraggedWidgets.Add(DraggedWidget);
			}
		}
		AutoKeyAnimatedTransform(DraggedWidgets, true, false, false);
	}
	if (DesignerTransaction.IsValid() && (bCancel || !bDesignerChanged))DesignerTransaction->Cancel();
	DesignerTransaction.Reset();
	DesignerSnapshots.Reset();
	ActiveDesignerHandle = EDesignerHandle::None;
	bDesignerDragging = false;
	bDesignerChanged = false;
	DesignerGuideX.Reset();
	DesignerGuideY.Reset();
	Invalidate();
}

bool FLexUIPrefabEditorViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	bool bSummonContextMenu = false;
	if (EventArgs.Key == EKeys::RightMouseButton)
	{
		if (EventArgs.Event == IE_Pressed)
		{
			RightMouseDownPosition = FIntPoint(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
			bRightMouseButtonDown = true;
			bRightMouseMoved = false;
		}
		else if (EventArgs.Event == IE_Released)
		{
			TrackRightMouseMovement(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
			bSummonContextMenu = bRightMouseButtonDown && !bRightMouseMoved;
		}
	}

	bool bHandled = false;
	if (IsOrtho())
	{
		bHandled = HandleDesignerInputKey(EventArgs);
	}
	if (!bHandled && TransformWidget.IsValid() && IsPerspective())
	{
		const bool bWasDraggingGizmo = TransformWidget->IsDragging();
		bHandled = TransformWidget->HandleInputKey(EventArgs);
		if (bWasDraggingGizmo && !TransformWidget->IsDragging() && PrefabEditorPtr.IsValid())
		{
			// The gizmo drag just ended. It writes location and rotation, so key both.
			TArray<ULexWidget*> MovedWidgets;
			for (const TWeakObjectPtr<ULexWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
			{
				if (ULexWidget* MovedWidget = WeakWidget.Get())
				{
					MovedWidgets.Add(MovedWidget);
				}
			}
			AutoKeyAnimatedTransform(MovedWidgets, true, true, false);
		}
	}
	if (!bHandled)
	{
		bHandled = GUnrealEd->ComponentVisManager.HandleInputKey(this, EventArgs.Viewport, EventArgs.Key, EventArgs.Event);
	}
	if (!bHandled)
	{
		bHandled = FEditorViewportClient::InputKey(EventArgs);

		if (!bHandled && EventArgs.Key == EKeys::F)
		{
			if (EventArgs.Event == IE_Pressed)
			{
				bHandled = FocusViewportToTargets();
			}
		}
	}

	if (EventArgs.Key == EKeys::RightMouseButton && EventArgs.Event == IE_Released)
	{
		bRightMouseButtonDown = false;
		bRightMouseMoved = false;
		RightMouseDownPosition = FIntPoint::ZeroValue;
		if (bSummonContextMenu)
		{
			if (TSharedPtr<SLexUIPrefabEditorViewport> EditorViewport = EditorViewportPtr.Pin())
			{
				bHandled |= EditorViewport->SummonContextMenu();
			}
		}
	}

	return bHandled;
}

void FLexUIPrefabEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	const FViewportClick Click(&View, this, Key, Event, HitX, HitY);

	FVector RayOrigin, RayDirection;
	View.DeprojectScreenToWorld(FVector2D(HitX, HitY), View.UnscaledViewRect, View.ViewMatrices.GetInvViewProjectionMatrix(), RayOrigin, RayDirection);
	ULexWidget* ClickHitWidget = nullptr;
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		float LineTraceLength = 100000000;
		//find hit LexVisualBatchMesh
		auto LineStart = RayOrigin;
		auto LineEnd = RayOrigin + RayDirection * LineTraceLength;
		ULexWidget* ClickHitUI = nullptr;
		TArray<ULexWidget*> AllWidgetArray;
		{
			for (auto& Canvas : LexUIManager->GetAllCanvasArray())
			{
				if (!Canvas->IsRootCanvas())continue;;
				auto RootWidget = Canvas->GetWidget();
				ULexWidget::CollectChildrenWidgets(RootWidget, AllWidgetArray);
			}
		}
		if (ULexUIManagerWorldSubsystem::RaycastHitUI(this->GetWorld(), AllWidgetArray, LineStart, LineEnd, ClickHitUI, IndexOfClickSelectUI))
		{
			ClickHitWidget = ClickHitUI;
		}
	}
	if (ClickHitWidget != nullptr && PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(ClickHitWidget))
	{
		ClickHitWidget = nullptr;
	}
	if (ClickHitWidget != nullptr && (Click.GetKey() == EKeys::LeftMouseButton || Click.GetKey() == EKeys::RightMouseButton))
	{
		PrefabEditorPtr.Pin()->SelectWidgets({ClickHitWidget}, Click.IsControlDown());
		return;
	}

	// We may have started gizmo manipulation if hot-keys were pressed when we started this click
	// If so, we need to end that now before we potentially update the selection below, 
	// otherwise the usual call in TrackingStopped would include the newly selected element
	if (bHasBegunGizmoManipulation)
	{
		FTypedElementListConstRef ElementsToManipulate = GetElementsToManipulate();
		ViewportInteraction->EndGizmoManipulation(ElementsToManipulate, GetWidgetMode(), ETypedElementViewportInteractionGizmoManipulationType::Click);
		bHasBegunGizmoManipulation = false;
	}

	if (Click.GetKey() == EKeys::MiddleMouseButton && !Click.IsAltDown() && !Click.IsShiftDown())
	{
		LexUIPrefabViewportClickHandlers::ClickViewport(this, Click);
		return;
	}
	if (!ModeTools->HandleClick(this, HitProxy, Click))
	{
		const FTypedElementHandle HitElement = HitProxy ? HitProxy->GetElementHandle() : FTypedElementHandle();

		if (HitProxy == NULL)
		{
			LexUIPrefabViewportClickHandlers::ClickBackdrop(this, Click);
		}
		else if (HitProxy->IsA(HWidgetAxis::StaticGetType()))
		{
			// The user clicked on an axis translation/rotation hit proxy.  However, we want
			// to find out what's underneath the axis widget.  To do this, we'll need to render
			// the viewport's hit proxies again, this time *without* the axis widgets!

			// OK, we need to be a bit evil right here.  Basically we want to hijack the ShowFlags
			// for the scene so we can re-render the hit proxies without any axis widgets.  We'll
			// store the original ShowFlags and modify them appropriately
			const bool bOldModeWidgets1 = EngineShowFlags.ModeWidgets;
			const bool bOldModeWidgets2 = View.Family->EngineShowFlags.ModeWidgets;

			EngineShowFlags.SetModeWidgets(false);
			FSceneViewFamily* SceneViewFamily = const_cast<FSceneViewFamily*>(View.Family);
			SceneViewFamily->EngineShowFlags.SetModeWidgets(false);
			bool bWasWidgetDragging = Widget->IsDragging();
			Widget->SetDragging(false);

			// Invalidate the hit proxy map so it will be rendered out again when GetHitProxy
			// is called
			Viewport->InvalidateHitProxy();

			// This will actually re-render the viewport's hit proxies!
			HHitProxy* HitProxyWithoutAxisWidgets = Viewport->GetHitProxy(HitX, HitY);
			if (HitProxyWithoutAxisWidgets != NULL && !HitProxyWithoutAxisWidgets->IsA(HWidgetAxis::StaticGetType()))
			{
				// Try this again, but without the widget this time!
				ProcessClick(View, HitProxyWithoutAxisWidgets, Key, Event, HitX, HitY);
			}

			// Undo the evil
			EngineShowFlags.SetModeWidgets(bOldModeWidgets1);
			SceneViewFamily->EngineShowFlags.SetModeWidgets(bOldModeWidgets2);

			Widget->SetDragging(bWasWidgetDragging);

			// Invalidate the hit proxy map again so that it'll be refreshed with the original
			// scene contents if we need it again later.
			Viewport->InvalidateHitProxy();
		}
		else if (GUnrealEd->ComponentVisManager.HandleClick(this, HitProxy, Click))
		{
			// Component vis manager handled the click
		}
		else if (HitElement && LexUIPrefabViewportClickHandlers::ClickElement(this, HitElement, Click))
		{
			// Element handled the click
		}
		else if (HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorHitProxy = (HActor*)HitProxy;
			AActor* ConsideredActor = ActorHitProxy->Actor;
			if (ConsideredActor) // It is possible to be clicking something during level transition if you spam click, and it might not be valid by this point
			{
				while (ConsideredActor->IsChildActor())
				{
					ConsideredActor = ConsideredActor->GetParentActor();
				}

				// We want to process the click on the component only if:
				// 1. The actor clicked is already selected
				// 2. The actor selected is the only actor selected
				// 3. The actor selected is blueprintable
				// 4. No components are already selected and the click was a double click
				// 5. OR, a component is already selected and the click was NOT a double click
				const bool bActorAlreadySelectedExclusively = GEditor->GetSelectedActors()->IsSelected(ConsideredActor) && (GEditor->GetSelectedActorCount() == 1);
				const bool bActorIsBlueprintable = FKismetEditorUtilities::CanCreateBlueprintOfClass(ConsideredActor->GetClass());
				const bool bComponentAlreadySelected = GEditor->GetSelectedComponentCount() > 0;
				const bool bWasDoubleClick = (Click.GetEvent() == IE_DoubleClick);

				const bool bSelectComponent = bActorAlreadySelectedExclusively && bActorIsBlueprintable && (bComponentAlreadySelected != bWasDoubleClick);
				bool bComponentSelected = false;

				if (bSelectComponent)
				{
					bComponentSelected = LexUIPrefabViewportClickHandlers::ClickComponent(this, ActorHitProxy, Click);
				}

				if (!bComponentSelected)
				{
					LexUIPrefabViewportClickHandlers::ClickActor(this, ConsideredActor, Click, true);
				}

				// We clicked an actor, allow the pivot to reposition itself.
				// GUnrealEd->SetPivotMovedIndependently(false);
			}
		}
		else if (HitProxy->IsA(HInstancedStaticMeshInstance::StaticGetType()))
		{
			LexUIPrefabViewportClickHandlers::ClickActor(this, ((HInstancedStaticMeshInstance*)HitProxy)->Component->GetOwner(), Click, true);
		}
		//else if (HitProxy->IsA(HBSPBrushVert::StaticGetType()) && ((HBSPBrushVert*)HitProxy)->Brush.IsValid())
		//{
		//	FVector Vertex = FVector(*((HBSPBrushVert*)HitProxy)->Vertex);
		//	LGUIPrefabViewportClickHandlers::ClickBrushVertex(this, ((HBSPBrushVert*)HitProxy)->Brush.Get(), &Vertex, Click);
		//}
		else if (HitProxy->IsA(HStaticMeshVert::StaticGetType()))
		{
			LexUIPrefabViewportClickHandlers::ClickStaticMeshVertex(this, ((HStaticMeshVert*)HitProxy)->Actor, ((HStaticMeshVert*)HitProxy)->Vertex, Click);
		}
		//else if (BrushSubsystem && BrushSubsystem->ProcessClickOnBrushGeometry(this, HitProxy, Click))
		//{
		//	// Handled by the brush subsystem
		//}
		else if (HitProxy->IsA(HModel::StaticGetType()))
		{
			HModel* ModelHit = (HModel*)HitProxy;

			// Compute the viewport's current view family.
			FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
			FSceneView* SceneView = CalcSceneView(&ViewFamily);

			uint32 SurfaceIndex = INDEX_NONE;
			if (ModelHit->ResolveSurface(SceneView, HitX, HitY, SurfaceIndex))
			{
				LexUIPrefabViewportClickHandlers::ClickSurface(this, ModelHit->GetModel(), SurfaceIndex, Click);
			}
		}
		else if (HitProxy->IsA(HLevelSocketProxy::StaticGetType()))
		{
			LexUIPrefabViewportClickHandlers::ClickLevelSocket(this, HitProxy, Click);
		}
	}
}

bool FLexUIPrefabEditorViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type InCurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (TransformWidget.IsValid() && TransformWidget->IsDragging())
	{
		return true;
	}
	
	if (GUnrealEd->ComponentVisManager.IsActive() && GUnrealEd->ComponentVisManager.HandleInputDelta(this, InViewport, Drag, Rot, Scale))
	{
		return true;
	}

	bool bHandled = false;

	// Give the current editor mode a chance to use the input first.  If it does, don't apply it to anything else.
	if (FEditorViewportClient::InputWidgetDelta(InViewport, InCurrentAxis, Drag, Rot, Scale))
	{
		bHandled = true;
	}
	else
	{
		if (InCurrentAxis != EAxisList::None)
		{
			// Skip actors transformation routine in case if any of the selected actors locked
			// but still pretend that we have handled the input
			if (!GEditor->HasLockedActors())
			{
				const bool LeftMouseButtonDown = InViewport->KeyState(EKeys::LeftMouseButton);
				const bool RightMouseButtonDown = InViewport->KeyState(EKeys::RightMouseButton);
				const bool MiddleMouseButtonDown = InViewport->KeyState(EKeys::MiddleMouseButton);

				// We do not want actors updated if we are holding down the middle mouse button.
				if (!MiddleMouseButtonDown)
				{
					ApplyDeltaToActors(Drag, Rot, Scale);
					ApplyDeltaToRotateWidget(Rot);
				}

				ModeTools->PivotLocation += Drag;
				ModeTools->SnappedLocation += Drag;

				if (IsShiftPressed())
				{
					FVector CameraDelta(Drag);
					MoveViewportCamera(CameraDelta, FRotator::ZeroRotator);
				}

				// zachma todo
				//TArray<FEdMode*> ActiveModes;
				//ModeTools->GetActiveModes(ActiveModes);

				//for (int32 ModeIndex = 0; ModeIndex < ActiveModes.Num(); ++ModeIndex)
				//{
				//	ActiveModes[ModeIndex]->UpdateInternalData();
				//}
			}

			bHandled = true;
		}

	}

	return bHandled;
}
UE::Widget::EWidgetMode FLexUIPrefabEditorViewportClient::GetWidgetMode() const
{
	if (GUnrealEd->ComponentVisManager.IsActive() && GUnrealEd->ComponentVisManager.IsVisualizingArchetype())
	{
		return UE::Widget::WM_None;
	}

	return FEditorViewportClient::GetWidgetMode();
}
FVector FLexUIPrefabEditorViewportClient::GetWidgetLocation() const
{
	FVector ComponentVisWidgetLocation;
	if (GUnrealEd->ComponentVisManager.GetWidgetLocation(this, ComponentVisWidgetLocation))
	{
		return ComponentVisWidgetLocation;
	}

	return FEditorViewportClient::GetWidgetLocation();
}
FMatrix FLexUIPrefabEditorViewportClient::GetWidgetCoordSystem() const
{
	FMatrix ComponentVisWidgetCoordSystem;
	if (GUnrealEd->ComponentVisManager.GetCustomInputCoordinateSystem(this, ComponentVisWidgetCoordSystem))
	{
		return ComponentVisWidgetCoordSystem;
	}

	return FEditorViewportClient::GetWidgetCoordSystem();
}

ULexCanvas* FLexUIPrefabEditorViewportClient::GetPreviewRootCanvas()const
{
	if (!PrefabEditorPtr.IsValid())return nullptr;
	ULexWidget* RootAgent = PrefabEditorPtr.Pin()->GetRootAgentWidget();
	if (!IsValid(RootAgent))return nullptr;
	ULexCanvas* Canvas = RootAgent->GetComponent<ULexCanvas>();
	// GetViewLocation and GetProjectionMatrix dereference GetWidget() with no null check of their
	// own. The runtime only ever called them mid-frame with everything alive; this runs on editor
	// ticks and across prefab close and world teardown, so the guard belongs here.
	if (!IsValid(Canvas) || !IsValid(Canvas->GetWidget()))return nullptr;
	return Canvas;
}

void FLexUIPrefabEditorViewportClient::SyncViewFOVToCanvas()
{
	// The editor camera ships with a 90 degree lens; the canvas's is FieldOfView, 60 by default.
	// A Perspective scope bakes its geometry for the canvas's eye, so looking at that geometry
	// through a different lens shows a foreshortening that is nobody's -- not the authored intent
	// and not what ships. Matching the lens is half of making the 3D view honest. The other half is
	// standing in the right place, which is FrameFromCanvasEye; this half is unconditional because
	// there is no reading of this viewport for which a mismatched lens is the right answer.
	if (!IsPerspective())return;//an ortho view has no field of view to match
	ULexCanvas* RootCanvas = GetPreviewRootCanvas();
	if (RootCanvas == nullptr || RootCanvas->GetProjectionType() != ECameraProjectionMode::Perspective)return;
	const float CanvasFOV = RootCanvas->GetFieldOfView();
	if (CanvasFOV > 0.0f && !FMath::IsNearlyEqual(ViewFOV, CanvasFOV))
	{
		ViewFOV = CanvasFOV;
		Invalidate();
	}
}

bool FLexUIPrefabEditorViewportClient::CanFrameFromCanvasEye()const
{
	ULexCanvas* RootCanvas = GetPreviewRootCanvas();
	// A world-space canvas does not project through its own camera, and an orthographic one has its
	// eye at infinity. In both cases there is no eye to stand at, and Perspective is inert anyway.
	return RootCanvas != nullptr
		&& !RootCanvas->IsRenderToWorldSpace()
		&& RootCanvas->GetProjectionType() == ECameraProjectionMode::Perspective;
}

void FLexUIPrefabEditorViewportClient::FrameFromCanvasEye()
{
	ULexCanvas* RootCanvas = GetPreviewRootCanvas();
	if (RootCanvas == nullptr)return;
	if (!IsPerspective())
	{
		SetViewportType(LVT_Perspective);
	}
	SetViewLocation(RootCanvas->GetViewLocation());
	SetViewRotation(RootCanvas->GetViewRotator());
	SyncViewFOVToCanvas();
	Invalidate();
}

void FLexUIPrefabEditorViewportClient::SetViewportType(ELevelViewportType InViewportType)
{
	FEditorViewportClient::SetViewportType(InViewportType);
	GetPrefabBeingEdited()->GetPrefabInstanceScene()->SetSkyCubeVisibility(IsPerspective());
}

/**
 * Returns the horizontal axis for this viewport.
 */

EAxisList::Type FLexUIPrefabEditorViewportClient::GetHorizAxis() const
{
	switch (GetViewportType())
	{
	case LVT_OrthoXY:
	case LVT_OrthoNegativeXY:
		return EAxisList::X;
	case LVT_OrthoXZ:
	case LVT_OrthoNegativeXZ:
		return EAxisList::X;
	case LVT_OrthoYZ:
	case LVT_OrthoNegativeYZ:
		return EAxisList::Y;
	case LVT_OrthoFreelook:
	case LVT_Perspective:
		break;
	}

	return EAxisList::X;
}

/**
 * Returns the vertical axis for this viewport.
 */

EAxisList::Type FLexUIPrefabEditorViewportClient::GetVertAxis() const
{
	switch (GetViewportType())
	{
	case LVT_OrthoXY:
	case LVT_OrthoNegativeXY:
		return EAxisList::Y;
	case LVT_OrthoXZ:
	case LVT_OrthoNegativeXZ:
		return EAxisList::Z;
	case LVT_OrthoYZ:
	case LVT_OrthoNegativeYZ:
		return EAxisList::Z;
	case LVT_OrthoFreelook:
	case LVT_Perspective:
		break;
	}

	return EAxisList::Y;
}
void FLexUIPrefabEditorViewportClient::NudgeSelectedObjects(const struct FInputEventState& InputState)
{
	if (!PrefabEditorPtr.IsValid())return;
	const bool bHasMovableSelection = PrefabEditorPtr.Pin()->GetSelectedWidgets().ContainsByPredicate([this](const TWeakObjectPtr<ULexWidget>& SelectedWidget)
	{
		return SelectedWidget.IsValid() && !PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(SelectedWidget.Get());
	});
	if (!bHasMovableSelection)return;

	FViewport* InViewport = InputState.GetViewport();
	EInputEvent Event = InputState.GetInputEvent();
	FKey Key = InputState.GetKey();

	const int32 MouseX = InViewport->GetMouseX();
	const int32 MouseY = InViewport->GetMouseY();

	if (Event == IE_Pressed)
	{
		GEditor->BeginTransaction(LOCTEXT("MoveWidget", "Move Widget"));
		if (PrefabEditorPtr.IsValid())
		{
			if (ULexUIPrefabHelperObject* Helper = PrefabEditorPtr.Pin()->GetPrefabHelperObject())
			{
				Helper->Modify();
				Helper->SetAnythingDirty();
			}
		}
		for (auto LexWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
		{
			if (LexWidget.IsValid() && !PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(LexWidget.Get()))LexWidget->Modify();
		}
	}
	else if (Event == IE_Released)
	{
		GEditor->EndTransaction();
	}
	
	if (Event == IE_Pressed || Event == IE_Repeat)
	{
		FVector2D MouseDelta(0,0);
		if (Key == EKeys::Left) MouseDelta.X = -1;
		else if (Key == EKeys::Right) MouseDelta.X = 1;
		else if (Key == EKeys::Up) MouseDelta.Y = 1;
		else if (Key == EKeys::Down) MouseDelta.Y = -1;
		if (PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->IsDesignerGridSnapEnabled())
		{
			MouseDelta *= PrefabEditorPtr.Pin()->GetDesignerGridSize();
		}
		
		for (auto LexWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
		{
			if (!LexWidget.IsValid() || PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(LexWidget.Get()))continue;
			auto AnchoredPos = LexWidget->GetAnchoredPosition();
			AnchoredPos += MouseDelta;
			LexWidget->SetAnchoredPosition(AnchoredPos);
		}
	}

	RedrawAllViewportsIntoThisScene();
}

void FLexUIPrefabEditorViewportClient::ApplyDeltaToActors(const FVector& InDrag, const FRotator& InRot, const FVector& InScale)
{
	ApplyDeltaToSelectedElements(FTransform(InRot, InDrag, InScale));
}

void FLexUIPrefabEditorViewportClient::ApplyDeltaToActor(AActor* InActor, const FVector& InDeltaDrag, const FRotator& InDeltaRot, const FVector& InDeltaScale)
{
	if (FTypedElementHandle ActorElementHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor))
	{
		ApplyDeltaToElement(ActorElementHandle, FTransform(InDeltaRot, InDeltaDrag, InDeltaScale));
	}
}

void FLexUIPrefabEditorViewportClient::ApplyDeltaToComponent(USceneComponent* InComponent, const FVector& InDeltaDrag, const FRotator& InDeltaRot, const FVector& InDeltaScale)
{
	if (FTypedElementHandle ComponentElementHandle = UEngineElementsLibrary::AcquireEditorComponentElementHandle(InComponent))
	{
		ApplyDeltaToElement(ComponentElementHandle, FTransform(InDeltaRot, InDeltaDrag, InDeltaScale));
	}
}

void FLexUIPrefabEditorViewportClient::ApplyDeltaToSelectedElements(const FTransform& InDeltaTransform)
{
	if (InDeltaTransform.GetTranslation().IsZero() && InDeltaTransform.Rotator().IsZero() && InDeltaTransform.GetScale3D().IsZero())
	{
		return;
	}

	FTransform ModifiedDeltaTransform = InDeltaTransform;

	{
		FVector AdjustedScale = ModifiedDeltaTransform.GetScale3D();

		// If we are scaling, we need to change the scaling factor a bit to properly align to grid
		if (GEditor->UsePercentageBasedScaling() && !AdjustedScale.IsNearlyZero())
		{
			AdjustedScale *= ((GEditor->GetScaleGridSize() / 100.0f) / GEditor->GetGridSize());
		}

		ModifiedDeltaTransform.SetScale3D(AdjustedScale);
	}

	FInputDeviceState InputState;
	InputState.SetModifierKeyStates(IsShiftPressed(), IsAltPressed(), IsCtrlPressed(), IsCmdPressed());

	FTypedElementListConstRef ElementsToManipulate = GetElementsToManipulate(true);
	ViewportInteraction->UpdateGizmoManipulation(ElementsToManipulate, GetWidgetMode(), Widget ? Widget->GetCurrentAxis() : EAxisList::None, InputState, ModifiedDeltaTransform);
}

void FLexUIPrefabEditorViewportClient::ApplyDeltaToElement(const FTypedElementHandle& InElementHandle, const FTransform& InDeltaTransform)
{
	FInputDeviceState InputState;
	InputState.SetModifierKeyStates(IsShiftPressed(), IsAltPressed(), IsCtrlPressed(), IsCmdPressed());

	ViewportInteraction->ApplyDeltaToElement(InElementHandle, GetWidgetMode(), Widget ? Widget->GetCurrentAxis() : EAxisList::None, InputState, InDeltaTransform);
}

FTypedElementListConstRef FLexUIPrefabEditorViewportClient::GetElementsToManipulate(const bool bForceRefresh)
{
	CacheElementsToManipulate(bForceRefresh);
	return CachedElementsToManipulate;
}

void FLexUIPrefabEditorViewportClient::CacheElementsToManipulate(const bool bForceRefresh)
{
	if (bForceRefresh)
	{
		ResetElementsToManipulate();
	}

	if (!bHasCachedElementsToManipulate)
	{
		const FTypedElementSelectionNormalizationOptions NormalizationOptions = FTypedElementSelectionNormalizationOptions()
			.SetExpandGroups(true)
			.SetFollowAttachment(true);

		const UTypedElementSelectionSet* SelectionSet = GetSelectionSet();
		SelectionSet->GetNormalizedSelection(NormalizationOptions, CachedElementsToManipulate);

		// Remove any elements that cannot be moved
		CachedElementsToManipulate->RemoveAll<ITypedElementWorldInterface>([this](const TTypedElement<ITypedElementWorldInterface>& InWorldElement)
			{
				if (!InWorldElement.CanMoveElement(bIsSimulateInEditorViewport ? ETypedElementWorldType::Game : ETypedElementWorldType::Editor))
				{
					return true;
				}

				// This element must belong to the current viewport world
				if (GEditor->PlayWorld)
				{
					const UWorld* CurrentWorld = InWorldElement.GetOwnerWorld();
					const UWorld* RequiredWorld = bIsSimulateInEditorViewport ? GEditor->PlayWorld : GEditor->EditorWorld;
					if (CurrentWorld != RequiredWorld)
					{
						return true;
					}
				}

				return false;
			});

		bHasCachedElementsToManipulate = true;
	}
}
void FLexUIPrefabEditorViewportClient::ResetElementsToManipulate(const bool bClearList)
{
	if (bClearList)
	{
		CachedElementsToManipulate->Reset();
	}
	bHasCachedElementsToManipulate = false;
}

void FLexUIPrefabEditorViewportClient::ResetElementsToManipulateFromSelectionChange(const UTypedElementSelectionSet* InSelectionSet)
{
	check(InSelectionSet == GetSelectionSet());

	// Don't clear the list immediately, as the selection may change from a construction script running (while we're still iterating the list!)
	// We'll process the clear on the next cache request, or when the typed element registry actually processes its pending deletion
	ResetElementsToManipulate(/*bClearList*/false);
}

void FLexUIPrefabEditorViewportClient::ResetElementsToManipulateFromProcessingDeferredElementsToDestroy()
{
	if (!bHasCachedElementsToManipulate)
	{
		// If we have no cache, make sure the cached list is definitely empty now to ensure it doesn't contain any lingering references to things that are about to be deleted
		CachedElementsToManipulate->Reset();
	}
}

const UTypedElementSelectionSet* FLexUIPrefabEditorViewportClient::GetSelectionSet() const
{
	return GEditor->GetSelectedActors()->GetElementSelectionSet();
}

UTypedElementSelectionSet* FLexUIPrefabEditorViewportClient::GetMutableSelectionSet() const
{
	return GEditor->GetSelectedActors()->GetElementSelectionSet();
}


void FLexUIPrefabEditorViewportClient::TickWorld(float DeltaSeconds)
{
	GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
}

bool FLexUIPrefabEditorViewportClient::FocusViewportToTargets()
{
	if (!PrefabEditorPtr.IsValid())
	{
		return false;
	}

	FBoxSphereBounds Bounds = FBoxSphereBounds(EForceInit::ForceInitToZero);
	if (!PrefabEditorPtr.Pin()->GetSelectedObjectsBounds(Bounds))
	{
		Bounds = PrefabEditorPtr.Pin()->GetAllObjectsBounds();
	}
	FocusViewportOnBox(Bounds.GetBox());

	return false;
}

ULexWidget* FLexUIPrefabEditorViewportClient::GetWidgetUnderCursor(int32 PixelX, int32 PixelY, bool bRespectDesignerLock)
{
	if (!Viewport || !GetWorld())return nullptr;
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = CalcSceneView(&ViewFamily);
	if (!View)return nullptr;
	FVector RayOrigin, RayDirection;
	FSceneView::DeprojectScreenToWorld(FVector2D(PixelX, PixelY), View->UnscaledViewRect,
		View->ViewMatrices.GetClipToWorld(), RayOrigin, RayDirection);
	TArray<ULexWidget*> Widgets;
	if (ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		for (const TWeakObjectPtr<ULexCanvas>& WeakCanvas : Manager->GetAllCanvasArray())
		{
			ULexCanvas* Canvas = WeakCanvas.Get();
			if (!Canvas || !Canvas->IsRootCanvas())continue;
			ULexWidget* Root = Canvas->GetWidget();
			if (!Root)continue;
			Widgets.Add(Root);
			ULexWidget::CollectChildrenWidgets(Root, Widgets);
		}
		ULexWidget* Result = nullptr;
		int32 HitIndex = INDEX_NONE;
		if (ULexUIManagerWorldSubsystem::RaycastHitUI(GetWorld(), Widgets, RayOrigin,
			RayOrigin + RayDirection * 100000000.0f, Result, HitIndex))
		{
			if (bRespectDesignerLock && PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->IsWidgetLockedInDesigner(Result))return nullptr;
			return Result;
		}
	}
	return nullptr;
}

bool FLexUIPrefabEditorViewportClient::GetDropWorldPosition(int32 PixelX, int32 PixelY, ULexWidget* ParentWidget, FVector& OutWorldPosition)
{
	if (!Viewport || !ParentWidget)return false;
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = CalcSceneView(&ViewFamily);
	if (!View)return false;
	FVector RayOrigin, RayDirection;
	FSceneView::DeprojectScreenToWorld(FVector2D(PixelX, PixelY), View->UnscaledViewRect,
		View->ViewMatrices.GetClipToWorld(), RayOrigin, RayDirection);
	const FTransform& ParentTransform = ParentWidget->GetWorldTransform();
	const FVector Intersection = FMath::LinePlaneIntersection(RayOrigin, RayOrigin + RayDirection * 100000000.0f,
		ParentTransform.GetLocation(), ParentTransform.GetUnitAxis(EAxis::X));
	FVector LocalPosition = ParentTransform.InverseTransformPosition(Intersection);
	if (PrefabEditorPtr.IsValid())
	{
		LocalPosition.Y = PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPosition.Y);
		LocalPosition.Z = PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPosition.Z);
	}
	OutWorldPosition = ParentTransform.TransformPosition(LocalPosition);
	return true;
}

void FLexUIPrefabEditorViewportClient::SetPaletteDropPreview(ULexWidget* InWidget)
{
	PaletteDropPreviewWidget = InWidget;
	Invalidate();
}

void FLexUIPrefabEditorViewportClient::ClearPaletteDropPreview()
{
	PaletteDropPreviewWidget.Reset();
	Invalidate();
}


// Begin override because PreviewScene is nullptr
// These implementation are copied from FEditorViewportClient
UWorld* FLexUIPrefabEditorViewportClient::GetWorld()const
{
	return PrefabEditorPtr.Pin()->GetWorld();
}
void FLexUIPrefabEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);
	PrefabEditorPtr.Pin()->GetPreviewScene()->AddReferencedObjects(Collector);
}
namespace PreviewLightConstants
{
	const float MovingPreviewLightTimerDuration = 1.0f;

	const float MinMouseRadius = 100.0f;
	const float MinArrowLength = 10.0f;
	const float ArrowLengthToSizeRatio = 0.1f;
	const float MouseLengthToArrowLenghtRatio = 0.2f;

	const float ArrowLengthToThicknessRatio = 0.05f;
	const float MinArrowThickness = 2.0f;

	// Note: MinMouseRadius must be greater than MinArrowLength
}
void FLexUIPrefabEditorViewportClient::DrawPreviewLightVisualization(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	// Draw the indicator of the current light direction if it was recently moved
	auto PrefabScene = PrefabEditorPtr.Pin()->GetPreviewScene();
	if ((PrefabScene != nullptr) && (PrefabScene->DirectionalLight != nullptr) && (MovingPreviewLightTimer > 0.0f))
	{
		const float A = MovingPreviewLightTimer / PreviewLightConstants::MovingPreviewLightTimerDuration;

		ULightComponent* Light = PrefabScene->DirectionalLight;

		const FLinearColor ArrowColor = Light->LightColor;

		// Figure out where the light is (ignoring position for directional lights)
		const FTransform LightLocalToWorldRaw = Light->GetComponentToWorld();
		FTransform LightLocalToWorld = LightLocalToWorldRaw;
		if (Light->IsA(UDirectionalLightComponent::StaticClass()))
		{
			LightLocalToWorld.SetTranslation(FVector::ZeroVector);
		}
		LightLocalToWorld.SetScale3D(FVector(1.0f));

		// Project the last mouse position during the click into world space
		FVector LastMouseWorldPos;
		FVector LastMouseWorldDir;
		View->DeprojectFVector2D(MovingPreviewLightSavedScreenPos, /*out*/ LastMouseWorldPos, /*out*/ LastMouseWorldDir);

		// The world pos may be nuts due to a super distant near plane for orthographic cameras, so find the closest
		// point to the origin along the ray
		LastMouseWorldPos = FMath::ClosestPointOnLine(LastMouseWorldPos, LastMouseWorldPos + LastMouseWorldDir * WORLD_MAX, FVector::ZeroVector);

		// Figure out the radius to draw the light preview ray at
		const FVector LightToMousePos = LastMouseWorldPos - LightLocalToWorld.GetTranslation();
		const float LightToMouseRadius = FMath::Max<FVector::FReal>(LightToMousePos.Size(), PreviewLightConstants::MinMouseRadius);

		const float ArrowLength = FMath::Max(PreviewLightConstants::MinArrowLength, LightToMouseRadius * PreviewLightConstants::MouseLengthToArrowLenghtRatio);
		const float ArrowSize = PreviewLightConstants::ArrowLengthToSizeRatio * ArrowLength;
		const float ArrowThickness = FMath::Max(PreviewLightConstants::ArrowLengthToThicknessRatio * ArrowLength, PreviewLightConstants::MinArrowThickness);

		const FVector ArrowOrigin = LightLocalToWorld.TransformPosition(FVector(-LightToMouseRadius - 0.5f * ArrowLength, 0.0f, 0.0f));
		const FVector ArrowDirection = LightLocalToWorld.TransformVector(FVector(-1.0f, 0.0f, 0.0f));

		const FQuatRotationTranslationMatrix ArrowToWorld(LightLocalToWorld.GetRotation(), ArrowOrigin);

		DrawDirectionalArrow(PDI, ArrowToWorld, ArrowColor, ArrowLength, ArrowSize, SDPG_World, ArrowThickness);
	}
}
FLinearColor FLexUIPrefabEditorViewportClient::GetBackgroundColor() const
{
	auto PrefabScene = PrefabEditorPtr.Pin()->GetPreviewScene();
	return PrefabScene ? PrefabScene->GetBackgroundColor() : FColor(55, 55, 55);
}
namespace EditorViewportClient
{
	static const float GridSize = 2048.0f;
	static const int8 CellSize = 16;
	static const float LightRotSpeed = 0.22f;
}
class FCachedJoystickState
{
public:
	uint32 JoystickType;
	TMap <FKey, float> AxisDeltaValues;
	TMap <FKey, EInputEvent> KeyEventValues;
};
bool FLexUIPrefabEditorViewportClient::Internal_InputAxis(FViewport* InViewport, FInputDeviceId DeviceID, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
{
	if (bDisableInput)
	{
		return true;
	}

	const FPlatformUserId UserId = IPlatformInputDeviceMapper::Get().GetUserForInputDevice(DeviceID);

	// Let the current mode have a look at the input before reacting to it.
	if (ModeTools->InputAxis(this, Viewport, FGenericPlatformMisc::GetUserIndexForPlatformUser(UserId), Key, Delta, DeltaTime))
	{
		return true;
	}

	const bool bMouseButtonDown = InViewport->KeyState(EKeys::LeftMouseButton) || InViewport->KeyState(EKeys::MiddleMouseButton) || InViewport->KeyState(EKeys::RightMouseButton);
	const bool bLightMoveDown = InViewport->KeyState(EKeys::L);

	// Look at which axis is being dragged and by how much
	const float DragX = (Key == EKeys::MouseX) ? Delta : 0.f;
	const float DragY = (Key == EKeys::MouseY) ? Delta : 0.f;

	auto PrefabScene = PrefabEditorPtr.Pin()->GetPreviewScene();
	if (bLightMoveDown && bMouseButtonDown && PrefabScene)
	{
		// Adjust the preview light direction
		FRotator LightDir = PrefabScene->GetLightDirection();

		LightDir.Yaw += -DragX * EditorViewportClient::LightRotSpeed;
		LightDir.Pitch += -DragY * EditorViewportClient::LightRotSpeed;

		PrefabScene->SetLightDirection(LightDir);

		// Remember that we adjusted it for the visualization
		MovingPreviewLightTimer = PreviewLightConstants::MovingPreviewLightTimerDuration;
		MovingPreviewLightSavedScreenPos = FVector2D(LastMouseX, LastMouseY);

		Invalidate();
	}
	else
	{
		/**Save off axis commands for future camera work*/
		FCachedJoystickState* JoystickState = GetJoystickState(DeviceID.GetId());
		if (JoystickState)
		{
			JoystickState->AxisDeltaValues.Add(Key, Delta);
		}

		if (bIsTracking)
		{
			// Accumulate and snap the mouse movement since the last mouse button click.
			MouseDeltaTracker->AddDelta(this, Key, Delta, 0);
		}
	}

	// If we are using a drag tool, paint the viewport so we can see it update.
	if (MouseDeltaTracker->UsingDragTool())
	{
		Invalidate(false, false);
	}

	return true;
}
// End override because PreviewScene is nullptr


ULexUIPrefab* FLexUIPrefabEditorViewportClient::GetPrefabBeingEdited()const
{
	return PrefabEditorPtr.Pin()->GetPrefabBeingEdited();
}

namespace LevelEditorViewportClientHelper
{
	FProperty* GetEditTransformProperty(UE::Widget::EWidgetMode WidgetMode)
	{
		FProperty* ValueProperty = nullptr;
		switch (WidgetMode)
		{
		case UE::Widget::WM_Translate:
			ValueProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeLocationPropertyName());
			break;
		case UE::Widget::WM_Rotate:
			ValueProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeRotationPropertyName());
			break;
		case UE::Widget::WM_Scale:
			ValueProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeScale3DPropertyName());
			break;
		case UE::Widget::WM_TranslateRotateZ:
			ValueProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeLocationPropertyName());
			break;
		case UE::Widget::WM_2D:
			ValueProperty = FindFProperty<FProperty>(USceneComponent::StaticClass(), USceneComponent::GetRelativeLocationPropertyName());
			break;
		default:
			break;
		}
		return ValueProperty;
	}
}

void FLexUIPrefabEditorViewportClient::GetSelectedActorsAndComponentsForMove(TArray<AActor*>& OutActorsToMove, TArray<USceneComponent*>& OutComponentsToMove) const
{
	OutActorsToMove.Reset();
	OutComponentsToMove.Reset();

	// Get the list of parent-most component(s) that are selected
	if (GEditor->GetSelectedComponentCount() > 0)
	{
		// Otherwise, if both a parent and child are selected and the delta is applied to both, the child will actually move 2x delta
		for (FSelectedEditableComponentIterator EditableComponentIt(GEditor->GetSelectedEditableComponentIterator()); EditableComponentIt; ++EditableComponentIt)
		{
			USceneComponent* SceneComponent = Cast<USceneComponent>(*EditableComponentIt);
			if (!SceneComponent)
			{
				continue;
			}

			// Check to see if any parent is selected
			bool bParentAlsoSelected = false;
			USceneComponent* Parent = SceneComponent->GetAttachParent();
			while (Parent != nullptr)
			{
				if (Parent->IsSelected())
				{
					bParentAlsoSelected = true;
					break;
				}

				Parent = Parent->GetAttachParent();
			}

			AActor* ComponentOwner = SceneComponent->GetOwner();
			if (!CanMoveActorInViewport(ComponentOwner))
			{
				continue;
			}

			const bool bIsRootComponent = (ComponentOwner && (ComponentOwner->GetRootComponent() == SceneComponent));
			if (bIsRootComponent)
			{
				// If it is a root component, use the parent actor instead
				OutActorsToMove.Add(ComponentOwner);
			}
			else if (!bParentAlsoSelected)
			{
				// If no parent of this component is also in the selection set, move it
				OutComponentsToMove.Add(SceneComponent);
			}
		}
	}

	// Skip gathering selected actors if we had a valid component selection
	if (OutComponentsToMove.Num() || OutActorsToMove.Num())
	{
		return;
	}

	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		AActor* Actor = CastChecked<AActor>(*It);

		// If the root component was selected, this actor is already accounted for
		USceneComponent* RootComponent = Actor->GetRootComponent();
		if (RootComponent && RootComponent->IsSelected())
		{
			continue;
		}

		if (!CanMoveActorInViewport(Actor))
		{
			continue;
		}

		OutActorsToMove.Add(Actor);
	}
}

bool FLexUIPrefabEditorViewportClient::CanMoveActorInViewport(const AActor* InActor) const
{
	if (!GEditor || !InActor)
	{
		return false;
	}

	// The actor cannot be location locked
	if (InActor->IsLockLocation())
	{
		return false;
	}

	// The actor needs to be in the current viewport world
	if (GEditor->PlayWorld)
	{
		if (bIsSimulateInEditorViewport)
		{
			// If the Actor's outer (level) outer (world) is not the PlayWorld then it cannot be moved in this viewport.
			if (!(GEditor->PlayWorld == InActor->GetOuter()->GetOuter()))
			{
				return false;
			}
		}
		else if (!(GEditor->EditorWorld == InActor->GetOuter()->GetOuter()))
		{
			return false;
		}
	}

	return true;
}

#include "UnrealWidget.h"

void FLexUIPrefabEditorViewportClient::TrackRightMouseMovement(int32 MouseX, int32 MouseY)
{
	if (!bRightMouseButtonDown || bRightMouseMoved)
	{
		return;
	}

	const int32 DeltaX = MouseX - RightMouseDownPosition.X;
	const int32 DeltaY = MouseY - RightMouseDownPosition.Y;
	bRightMouseMoved = FMath::Square(DeltaX) + FMath::Square(DeltaY) >= MOUSE_CLICK_DRAG_DELTA;
}

void FLexUIPrefabEditorViewportClient::CapturedMouseMove(FViewport* InViewport, int32 InMouseX, int32 InMouseY)
{
	TrackRightMouseMovement(InMouseX, InMouseY);

	// Commit to any pending transactions now
	TrackingTransaction.PromotePendingToActive();

	FEditorViewportClient::CapturedMouseMove(InViewport, InMouseX, InMouseY);
	
	if (InMouseX != PrevMouseX || InMouseY != PrevMouseY)
	{
		IndexOfClickSelectUI = INDEX_NONE;
	}
	PrevMouseX = InMouseX;
	PrevMouseY = InMouseY;
}

void FLexUIPrefabEditorViewportClient::MouseEnter(FViewport* InViewport, int32 x, int32 y)
{
	FEditorViewportClient::MouseEnter(InViewport, x, y);
}
void FLexUIPrefabEditorViewportClient::MouseMove(FViewport* InViewport, int32 x, int32 y)
{
	TrackRightMouseMovement(x, y);
	FEditorViewportClient::MouseMove(InViewport, x, y);
}
void FLexUIPrefabEditorViewportClient::MouseLeave(FViewport* InViewport)
{
	FEditorViewportClient::MouseLeave(InViewport);
}

void FLexUIPrefabEditorViewportClient::TrackingStarted(const struct FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge)
{
	// Begin transacting.  Give the current editor mode an opportunity to do the transacting.
	const bool bTrackingHandledExternally = ModeTools->StartTracking(this, Viewport);

	TrackingTransaction.End();

	const bool bIsDraggingComponents = GEditor->GetSelectedComponentCount() > 0;

	// Create edit property event
	FEditPropertyChain PropertyChain;
	FProperty* TransformProperty = LevelEditorViewportClientHelper::GetEditTransformProperty(GetWidgetMode());
	if (TransformProperty)
	{
		PropertyChain.AddHead(TransformProperty);
	}

	if (bIsDraggingComponents)
	{
		if (bIsDraggingWidget)
		{
			Widget->SetSnapEnabled(true);

			for (FSelectedEditableComponentIterator It(GEditor->GetSelectedEditableComponentIterator()); It; ++It)
			{
				USceneComponent* SceneComponent = Cast<USceneComponent>(*It);
				if (SceneComponent)
				{
					// Notify that this component is beginning to move
					GEditor->BroadcastBeginObjectMovement(*SceneComponent);

					// Broadcast Pre Edit change notification, we can't call PreEditChange directly on Actor or ActorComponent from here since it will unregister the components until PostEditChange
					if (TransformProperty)
					{
						FCoreUObjectDelegates::OnPreObjectPropertyChanged.Broadcast(SceneComponent, PropertyChain);
					}
				}
			}
		}
	}
	else
	{
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It && !bIsTrackingBrushModification; ++It)
		{
			AActor* Actor = CastChecked<AActor>(*It);

			if (bIsDraggingWidget)
			{
				// Notify that this actor is beginning to move
				GEditor->BroadcastBeginObjectMovement(*Actor);

				// Broadcast Pre Edit change notification, we can't call PreEditChange directly on Actor or ActorComponent from here since it will unregister the components until PostEditChange
				if (TransformProperty)
				{
					FCoreUObjectDelegates::OnPreObjectPropertyChanged.Broadcast(Actor, PropertyChain);
				}
			}

			Widget->SetSnapEnabled(true);
		}
	}

	// Start a transformation transaction if required
	if (!bTrackingHandledExternally)
	{
		if (bIsDraggingWidget)
		{
			TrackingTransaction.TransCount++;

			FText TrackingDescription;
			switch (GetWidgetMode())
			{
			case UE::Widget::WM_Translate:
				TrackingDescription = LOCTEXT("MoveTransaction", "Move Elements");
				break;
			case UE::Widget::WM_Rotate:
				TrackingDescription = LOCTEXT("RotateTransaction", "Rotate Elements");
				break;
			case UE::Widget::WM_Scale:
				TrackingDescription = LOCTEXT("ScaleTransaction", "Scale Elements");
				break;
			case UE::Widget::WM_TranslateRotateZ:
				TrackingDescription = LOCTEXT("TranslateRotateZTransaction", "Translate/RotateZ Elements");
				break;
			case UE::Widget::WM_2D:
				TrackingDescription = LOCTEXT("TranslateRotate2D", "Translate/Rotate2D Elements");
				break;
			default:
				if (bNudge)
				{
					TrackingDescription = LOCTEXT("NudgeTransaction", "Nudge Elements");
				}
			}

			if (!TrackingDescription.IsEmpty())
			{
				if (bNudge)
				{
					TrackingTransaction.Begin(TrackingDescription);
				}
				else
				{
					// If this hasn't begun due to a nudge, start it as a pending transaction so that it only really begins when the mouse is moved
					TrackingTransaction.BeginPending(TrackingDescription);
				}
			}
		}

		if (TrackingTransaction.IsActive() || TrackingTransaction.IsPending())
		{
			// Suspend actor/component modification during each delta step to avoid recording unnecessary overhead into the transaction buffer
			GEditor->DisableDeltaModification(true);
		}
	}
}
void FLexUIPrefabEditorViewportClient::TrackingStopped()
{
	const bool AltDown = IsAltPressed();
	const bool ShiftDown = IsShiftPressed();
	const bool ControlDown = IsCtrlPressed();
	const bool LeftMouseButtonDown = Viewport->KeyState(EKeys::LeftMouseButton);
	const bool RightMouseButtonDown = Viewport->KeyState(EKeys::RightMouseButton);
	const bool MiddleMouseButtonDown = Viewport->KeyState(EKeys::MiddleMouseButton);

	// here we check to see if anything of worth actually changed when ending our MouseMovement
	// If the TransCount > 0 (we changed something of value) so we need to call PostEditMove() on stuff
	// if we didn't change anything then don't call PostEditMove()
	bool bDidAnythingActuallyChange = false;

	// Stop transacting.  Give the current editor mode an opportunity to do the transacting.
	const bool bTransactingHandledByEditorMode = ModeTools->EndTracking(this, Viewport);
	if (!bTransactingHandledByEditorMode)
	{
		if (TrackingTransaction.TransCount > 0)
		{
			bDidAnythingActuallyChange = true;
			TrackingTransaction.TransCount--;
		}
	}

	// Notify the selected actors that they have been moved.
	// Don't do this if AddDelta was never called.
	if (bDidAnythingActuallyChange && MouseDeltaTracker->HasReceivedDelta())
	{
		// Create post edit property change event
		FProperty* TransformProperty = LevelEditorViewportClientHelper::GetEditTransformProperty(GetWidgetMode());
		FPropertyChangedEvent PropertyChangedEvent(TransformProperty, EPropertyChangeType::ValueSet);

		// Move components and actors
		{
			TArray<USceneComponent*> ComponentsToMove;
			TArray<AActor*> ActorsToMove;
			GetSelectedActorsAndComponentsForMove(ActorsToMove, ComponentsToMove);

			for (USceneComponent* Component : ComponentsToMove)
			{
				// Broadcast Post Edit change notification, we can't call PostEditChangeProperty directly on Actor or ActorComponent from here since it wasn't pair with a proper PreEditChange
				FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Component, PropertyChangedEvent);
				
				Component->PostEditComponentMove(true);
				GEditor->BroadcastEndObjectMovement(*Component);
			}

			for (AActor* Actor : ActorsToMove)
			{
				// Broadcast Post Edit change notification, we can't call PostEditChangeProperty directly on Actor or ActorComponent from here since it wasn't pair with a proper PreEditChange
				FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(Actor, PropertyChangedEvent);
				Actor->PostEditMove(true);
				GEditor->BroadcastEndObjectMovement(*Actor);
			}

			GEditor->BroadcastActorsMoved(ActorsToMove);
		}
	}

	// End the transaction here if one was started in StartTransaction()
	if (TrackingTransaction.IsActive() || TrackingTransaction.IsPending())
	{
		if (!HaveSelectedObjectsBeenChanged())
		{
			TrackingTransaction.Cancel();
		}
		else
		{
			TrackingTransaction.End();
		}

		// Restore actor/component delta modification
		GEditor->DisableDeltaModification(false);
	}

	ModeTools->ActorMoveNotify();

	if (bDidAnythingActuallyChange)
	{
		FScopedLevelDirtied LevelDirtyCallback;
		LevelDirtyCallback.Request();

		RedrawAllViewportsIntoThisScene();
	}
}

void FLexUIPrefabEditorViewportClient::AbortTracking()
{
	if (TrackingTransaction.IsActive())
	{
		// Applying the global undo here will reset the drag operation
		if (GUndo)
		{
			GUndo->Apply();
		}
		TrackingTransaction.Cancel();
		StopTracking();
	}
}

bool FLexUIPrefabEditorViewportClient::HaveSelectedObjectsBeenChanged() const
{
	return (TrackingTransaction.TransCount > 0 || TrackingTransaction.IsActive()) && (MouseDeltaTracker->HasReceivedDelta() || MouseDeltaTracker->WasExternalMovement());
}


#undef LOCTEXT_NAMESPACE
