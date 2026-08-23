// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabEditorViewportClient.h"
#include "Core/DreamGUISettings.h"
#include "DreamUIDesignScreenSizes.h"
#include "DreamUIPrefabEditorViewport.h"
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
#include "DreamUIPrefabEditor.h"
#include "DreamUIWidgetPicking.h"
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
#include "DreamUIPrefabViewportClickHandlers.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamCanvasViewFit.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamWidget.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "PrefabAnimation/DreamUIPrefabSequenceEditor.h"
#include "ISequencer.h"
#include "KeyPropertyParams.h"
#include "PropertyPath.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "PrefabSystem/DreamUIPrefabInstanceScene.h"
#include "Utils/DreamUIUtils.h"
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "Engine/Canvas.h"
#include "CanvasItem.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorViewportClient"

// UE5.8: HLevelSocketProxy is now declared AND implemented/exported by the engine
// (ViewportSelectionUtilities.h), so re-implementing it here is a duplicate (C4273).


FDreamUIPrefabEditorViewportClient::FDreamUIPrefabEditorViewportClient(TWeakPtr<FDreamUIPrefabEditor> InPrefabEditorPtr
	, const TSharedRef<SDreamUIPrefabEditorViewport>& InEditorViewportPtr)
	// UE5.8: pass nullptr (NOT &GLevelEditorModeTools()) so the base creates a PRIVATE
	// FAssetEditorModeManager for this viewport. Sharing the global level-editor mode tools
	// routes InputKey/ProcessClick through the 5.8 Interactive Tools Framework's global
	// InputRouter, which is not valid in a custom asset-editor viewport -> null-deref crash
	// in FEditorViewportClient::InputKey (EditorInteractiveToolsFramework). Same root cause
	// and fix as the DreamGUI3 Anchor Tool issue.
	: FEditorViewportClient(nullptr, nullptr, StaticCastSharedRef<SEditorViewport>(InEditorViewportPtr))
	, TrackingTransaction()
	, CachedElementsToManipulate(UTypedElementRegistry::GetInstance()->CreateElementList())
{
	PrefabEditorPtr = InPrefabEditorPtr;
	EditorViewportPtr = InEditorViewportPtr;
	ModeTools->SetWidgetMode(UE::Widget::WM_Translate);
	Widget->SetUsesEditorModeTools(ModeTools.Get());

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
	// Assigning ViewportType above deliberately bypasses SetViewportType, so its side effects have
	// to be repeated here or they never fire on opening a prefab.
	DrawHelper.bDrawGrid = !ShouldUseCanvasView();

}

FDreamUIPrefabEditorViewportClient::~FDreamUIPrefabEditorViewportClient()
{
	if (PrefabEditorPtr.IsValid() && OnSelectionChangedDelegateHandle.IsValid())
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
void FDreamUIPrefabEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
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
void FDreamUIPrefabEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{	
	if (GUnrealEd != nullptr && !IsInGameView())
	{
		GUnrealEd->DrawComponentVisualizersHUD(&InViewport, &View, &Canvas);
	}

	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
	DrawDesignerOverlay(InViewport, View, Canvas);
	DrawAnimationModeIndicator(InViewport, Canvas);
}

void FDreamUIPrefabEditorViewportClient::DrawAnimationModeIndicator(FViewport& InViewport, FCanvas& Canvas) const
{
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	UDreamUIPrefabSequence* Animation = Editor.IsValid() ? Editor->GetAnimationBeingEdited() : nullptr;
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

void FDreamUIPrefabEditorViewportClient::AutoKeyAnimatedTransform(const TArray<UDreamWidget*>& InWidgets, bool bLocation, bool bRotation, bool bScale) const
{
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	if (!Editor.IsValid() || !Editor->IsInAnimationEditMode())
	{
		return;
	}
	const TSharedPtr<SDreamUIPrefabSequenceEditor> SequencerEditor = Editor->GetSequencerEditor();
	const TSharedPtr<ISequencer> Sequencer = SequencerEditor.IsValid() ? SequencerEditor->GetSequencer() : nullptr;
	if (!Sequencer.IsValid())
	{
		return;
	}

	TArray<UObject*> ObjectsToKey;
	for (UDreamWidget* KeyedWidget : InWidgets)
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
		if (FProperty* Property = UDreamWidget::StaticClass()->FindPropertyByName(InPropertyName))
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

namespace
{
	/**
	 * WorldToPixel, but refusing points at or behind the eye instead of mirroring them.
	 * FSceneView::ScreenToPixel flips a negative W on purpose -- so a manipulator stays grabbable
	 * when the camera is right on top of it -- and returns true. Under the orthographic view W was
	 * always positive, so every "if (!WorldToPixel) return" in this file was unreachable. Once the
	 * 2D view projects through the canvas it is reachable, and without this an outline behind the
	 * canvas eye folds inside out and its handles stay clickable at pixels nothing occupies.
	 */
	bool DreamWorldToPixelInFront(const FSceneView& InView, const FVector& InWorldPoint, FVector2D& OutPixel)
	{
		const FVector4 Screen = InView.WorldToScreen(InWorldPoint);
		if (Screen.W <= UE_KINDA_SMALL_NUMBER)return false;
		return InView.ScreenToPixel(Screen, OutPixel);
	}

	/** A widget's four rect corners in pixels, bottom-left first, or false if any is behind the eye. */
	bool DreamProjectWidgetCorners(const FSceneView& InView, const UDreamWidget* InWidget, TArray<FVector2D>& OutCorners)
	{
		OutCorners.Reset();
		if (!IsValid(InWidget))return false;
		const float Left = -InWidget->GetPivot().X * InWidget->GetWidth();
		const float Right = (1.0f - InWidget->GetPivot().X) * InWidget->GetWidth();
		const float Bottom = -InWidget->GetPivot().Y * InWidget->GetHeight();
		const float Top = (1.0f - InWidget->GetPivot().Y) * InWidget->GetHeight();
		const FTransform& Transform = InWidget->GetWorldTransform();
		for (const FVector& Local : { FVector(0, Left, Bottom), FVector(0, Right, Bottom), FVector(0, Right, Top), FVector(0, Left, Top) })
		{
			FVector2D Pixel;
			if (!DreamWorldToPixelInFront(InView, Transform.TransformPosition(Local), Pixel))return false;
			OutCorners.Add(Pixel);
		}
		return true;
	}

	/**
	 * How close to a quarter gridline the cursor has to be for an anchor to land on it, as a fraction
	 * of the anchor space rather than in pixels, so the gesture behaves the same at every zoom. It is
	 * well under half the 0.25 gap, so two gridlines can never both claim the cursor.
	 */
	constexpr double DreamAnchorSnapTolerance = 0.02;
}

bool FDreamUIPrefabEditorViewportClient::UpdateDesignerScreenGeometry(FSceneView& View)
{
	DesignerScreenCorners.Reset();
	DesignerHandlePositions.Reset();
	DesignerAnchorSpaceCorners.Reset();
	DesignerAnchorHandlePositions.Reset();
	DesignerScreenBounds = FBox2D(EForceInit::ForceInit);
	bDesignerMoveAvailable = false;
	if (!IsOrtho() || !PrefabEditorPtr.IsValid())return false;
	TArray<UDreamWidget*> Selected;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
	{
		if (UDreamWidget* SelectedWidget = WeakWidget.Get())
		{
			if (!PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(SelectedWidget) && !PrefabEditorPtr.Pin()->IsWidgetHiddenInDesigner(SelectedWidget))Selected.Add(SelectedWidget);
		}
	}
	if (Selected.IsEmpty())return false;
	bDesignerMoveAvailable = CanMoveSelection(Selected);

	auto ProjectWidgetCorners = [&View](UDreamWidget* InWidget, TArray<FVector2D>& OutCorners) -> bool
	{
		const float Left = -InWidget->GetPivot().X * InWidget->GetWidth();
		const float Right = (1.0f - InWidget->GetPivot().X) * InWidget->GetWidth();
		const float Bottom = -InWidget->GetPivot().Y * InWidget->GetHeight();
		const float Top = (1.0f - InWidget->GetPivot().Y) * InWidget->GetHeight();
		const FTransform& Transform = InWidget->GetWorldTransform();
		for (const FVector& Local : { FVector(0, Left, Bottom), FVector(0, Right, Bottom), FVector(0, Right, Top), FVector(0, Left, Top) })
		{
			FVector2D Pixel;
			if (!DreamWorldToPixelInFront(View, Transform.TransformPosition(Local), Pixel))return false;
			OutCorners.Add(Pixel);
		}
		return true;
	};

	TArray<FVector2D> SingleCorners;
	for (UDreamWidget* SelectedWidget : Selected)
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
		UDreamWidget* SelectedWidget = Selected[0];
		// "The parent has a layout container" is not the same as "the parent owns this child's
		// size". A CanvasPanel arranges its children and writes their size only when the slot says
		// Auto Size, so suppressing every handle for every child of every panel took the handles
		// away from the one panel whose children you resize by hand. Ask per axis instead; UMG's
		// rule is the same shape (STransformHandle::CanResize is a per-slot question).
		const FDreamLayoutControlAnchorData Control = GetEffectiveLayoutControl(SelectedWidget);
		const bool bWidthFree = !Control.bCanControlHorizontalSize;
		const bool bHeightFree = !Control.bCanControlVerticalSize;
		if (bWidthFree && bHeightFree)
		{
			DesignerHandlePositions.Add(EDesignerHandle::BottomLeft, SingleCorners[0]);
			DesignerHandlePositions.Add(EDesignerHandle::BottomRight, SingleCorners[1]);
			DesignerHandlePositions.Add(EDesignerHandle::TopRight, SingleCorners[2]);
			DesignerHandlePositions.Add(EDesignerHandle::TopLeft, SingleCorners[3]);
		}
		if (bHeightFree)
		{
			DesignerHandlePositions.Add(EDesignerHandle::Bottom, (SingleCorners[0] + SingleCorners[1]) * 0.5f);
			DesignerHandlePositions.Add(EDesignerHandle::Top, (SingleCorners[2] + SingleCorners[3]) * 0.5f);
		}
		if (bWidthFree)
		{
			DesignerHandlePositions.Add(EDesignerHandle::Right, (SingleCorners[1] + SingleCorners[2]) * 0.5f);
			DesignerHandlePositions.Add(EDesignerHandle::Left, (SingleCorners[3] + SingleCorners[0]) * 0.5f);
		}
		FVector2D PivotPixel;
		if (DreamWorldToPixelInFront(View, SelectedWidget->GetWorldTransform().GetLocation(), PivotPixel))DesignerHandlePositions.Add(EDesignerHandle::Pivot, PivotPixel);
		UpdateAnchorScreenGeometry(View, SelectedWidget);
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

bool FDreamUIPrefabEditorViewportClient::IsAnchorHandle(EDesignerHandle InHandle)
{
	return InHandle == EDesignerHandle::AnchorBottomLeft || InHandle == EDesignerHandle::AnchorBottomRight
		|| InHandle == EDesignerHandle::AnchorTopRight || InHandle == EDesignerHandle::AnchorTopLeft;
}

void FDreamUIPrefabEditorViewportClient::UpdateAnchorScreenGeometry(FSceneView& View, UDreamWidget* InWidget)
{
	bool bHorizontal = false, bVertical = false;
	GetAnchorEditableAxes(InWidget, bHorizontal, bVertical);
	if (!bHorizontal && !bVertical)return;
	UDreamWidget* ParentWidget = InWidget->GetParent();
	const float ParentWidth = ParentWidget->GetWidth();
	const float ParentHeight = ParentWidget->GetHeight();
	if (ParentWidth <= UE_KINDA_SMALL_NUMBER || ParentHeight <= UE_KINDA_SMALL_NUMBER)return;
	if (!DreamProjectWidgetCorners(View, ParentWidget, DesignerAnchorSpaceCorners))
	{
		DesignerAnchorSpaceCorners.Reset();
		return;
	}

	// Unstretched anchors put all four corners on one point, which is the common case, so each marker
	// is pushed out along its own diagonal to stay separately grabbable. The diagonals come from the
	// projected parent rather than from screen axes so they still point outwards under a rotated or
	// mirrored parent; the drag reads travel from the press pixel, so the offset costs it nothing.
	const FVector2D RightDir = (DesignerAnchorSpaceCorners[1] - DesignerAnchorSpaceCorners[0]).GetSafeNormal();
	const FVector2D UpDir = (DesignerAnchorSpaceCorners[3] - DesignerAnchorSpaceCorners[0]).GetSafeNormal();
	const FTransform& ParentTransform = ParentWidget->GetWorldTransform();
	const float ParentLeft = ParentWidget->GetLocalSpaceLeft();
	const float ParentBottom = ParentWidget->GetLocalSpaceBottom();
	const FVector2D AnchorMin = InWidget->GetAnchorMin();
	const FVector2D AnchorMax = InWidget->GetAnchorMax();
	constexpr float MarkerOffset = 11.0f;
	auto PlaceMarker = [&](EDesignerHandle InHandle, double InFractionX, double InFractionY, float InSignX, float InSignY)
	{
		const FVector Local(0, ParentLeft + ParentWidth * InFractionX, ParentBottom + ParentHeight * InFractionY);
		FVector2D Pixel;
		if (!DreamWorldToPixelInFront(View, ParentTransform.TransformPosition(Local), Pixel))return;
		DesignerAnchorHandlePositions.Add(InHandle, Pixel + (RightDir * InSignX + UpDir * InSignY) * MarkerOffset);
	};
	PlaceMarker(EDesignerHandle::AnchorBottomLeft, AnchorMin.X, AnchorMin.Y, -1.0f, -1.0f);
	PlaceMarker(EDesignerHandle::AnchorBottomRight, AnchorMax.X, AnchorMin.Y, 1.0f, -1.0f);
	PlaceMarker(EDesignerHandle::AnchorTopRight, AnchorMax.X, AnchorMax.Y, 1.0f, 1.0f);
	PlaceMarker(EDesignerHandle::AnchorTopLeft, AnchorMin.X, AnchorMax.Y, -1.0f, 1.0f);
}

FDreamUIPrefabEditorViewportClient::EDesignerHandle FDreamUIPrefabEditorViewportClient::HitTestDesignerHandle(const FVector2D& PixelPosition) const
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
	// After the selection's own handles: an anchor marker sitting on top of a resize handle would be
	// stealing the more common gesture, and it is the one that can be reached from the details panel.
	for (const auto& Pair : DesignerAnchorHandlePositions)
	{
		if (FVector2D::Distance(Pair.Value, PixelPosition) <= HandleRadius)return Pair.Key;
	}
	// The rectangle is only a Move handle while a move can land. With every position axis arranged,
	// answering Move here would swallow the press and hand back a discarded write; answering None
	// lets it fall through to the click that selects whatever is inside the panel instead.
	if (bDesignerMoveAvailable
		&& DesignerScreenBounds.bIsValid
		&& PixelPosition.X >= DesignerScreenBounds.Min.X && PixelPosition.X <= DesignerScreenBounds.Max.X
		&& PixelPosition.Y >= DesignerScreenBounds.Min.Y && PixelPosition.Y <= DesignerScreenBounds.Max.Y)
	{
		return EDesignerHandle::Move;
	}
	return EDesignerHandle::None;
}

bool FDreamUIPrefabEditorViewportClient::IntersectDesignerPlane(const FVector2D& PixelPosition, const FTransform& PlaneTransform, FVector& OutPoint) const
{
	if (!Viewport)return false;
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = const_cast<FDreamUIPrefabEditorViewportClient*>(this)->CalcSceneView(&ViewFamily);
	if (!View)return false;
	FVector RayOrigin, RayDirection;
	FSceneView::DeprojectScreenToWorld(PixelPosition, View->UnscaledViewRect,
		View->ViewMatrices.GetClipToWorld(), RayOrigin, RayDirection);
	OutPoint = FMath::LinePlaneIntersection(RayOrigin, RayOrigin + RayDirection * 100000000.0f,
		PlaneTransform.GetLocation(), PlaneTransform.GetUnitAxis(EAxis::X));
	return true;
}

void FDreamUIPrefabEditorViewportClient::DrawWidgetScreenOutline(UDreamWidget* InWidget, FSceneView& View, FCanvas& Canvas,
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
		if (!DreamWorldToPixelInFront(View, Transform.TransformPosition(Local), Pixel))return;
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

void FDreamUIPrefabEditorViewportClient::DrawDesignerCanvasBoundary(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const
{
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	UDreamWidget* RootAgent = Editor.IsValid() ? Editor->GetRootAgentWidget() : nullptr;
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
		if (!DreamWorldToPixelInFront(View, Transform.TransformPosition(Local), Pixel))
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

void FDreamUIPrefabEditorViewportClient::DrawResolutionGuides(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const
{
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	UDreamWidget* RootAgent = Editor.IsValid() ? Editor->GetRootAgentWidget() : nullptr;
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
	for (const FDreamUIDesignScreenSize& ScreenSize : GetDreamUIDesignScreenSizes())
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
			if (!DreamWorldToPixelInFront(View, Transform.TransformPosition(LocalCorners[Corner]), Pixels[Corner]))
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

void FDreamUIPrefabEditorViewportClient::DrawSafeZoneGuide(FSceneView& View, FCanvas& Canvas) const
{
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	UDreamWidget* RootAgent = Editor.IsValid() ? Editor->GetRootAgentWidget() : nullptr;
	if (!IsValid(RootAgent) || RootAgent->GetWidth() <= 0.0f || RootAgent->GetHeight() <= 0.0f)return;
	if (!FSlateApplication::IsInitialized())return;

	// Whatever the platform declares, so a desktop that declares no safe area draws nothing rather
	// than an invented inset. r.DebugSafeZone.TitleRatio is what makes one appear here, and it is the
	// same knob UMG's designer reads.
	FMargin SafeZonePadding;
	FSlateApplication::Get().GetSafeZoneSize(SafeZonePadding, FVector2f(RootAgent->GetWidth(), RootAgent->GetHeight()));
	if (SafeZonePadding.Left <= 0.0f && SafeZonePadding.Right <= 0.0f && SafeZonePadding.Top <= 0.0f && SafeZonePadding.Bottom <= 0.0f)return;

	const FBox2D SafeRect = GetSafeZoneLocalRect(
		FVector2D(RootAgent->GetWidth(), RootAgent->GetHeight()), RootAgent->GetPivot(),
		FVector4(SafeZonePadding.Left, SafeZonePadding.Top, SafeZonePadding.Right, SafeZonePadding.Bottom));
	if (!SafeRect.bIsValid)return;

	const FTransform& Transform = RootAgent->GetWorldTransform();
	const float DpiScale = Canvas.GetDPIScale();
	const FVector LocalCorners[4] = {
		FVector(0, SafeRect.Min.X, SafeRect.Min.Y),
		FVector(0, SafeRect.Max.X, SafeRect.Min.Y),
		FVector(0, SafeRect.Max.X, SafeRect.Max.Y),
		FVector(0, SafeRect.Min.X, SafeRect.Max.Y),
	};
	FVector2D Pixels[4];
	for (int32 Corner = 0; Corner < 4; Corner++)
	{
		if (!DreamWorldToPixelInFront(View, Transform.TransformPosition(LocalCorners[Corner]), Pixels[Corner]))return;
		Pixels[Corner] /= DpiScale;
	}
	const FLinearColor SafeColor(1.0f, 0.78f, 0.2f, 0.85f);
	for (int32 Corner = 0; Corner < 4; Corner++)
	{
		FCanvasLineItem Line(Pixels[Corner], Pixels[(Corner + 1) % 4]);
		Line.SetColor(SafeColor);
		Line.LineThickness = 1.0f;
		Canvas.DrawItem(Line);
	}
	if (UFont* Font = GEngine->GetSmallFont())
	{
		FCanvasTextItem Label(Pixels[3] + FVector2D(4.0f, 2.0f), LOCTEXT("SafeZoneGuide", "safe area"), Font, SafeColor);
		Label.EnableShadow(FLinearColor::Black);
		Canvas.DrawItem(Label);
	}
}

void FDreamUIPrefabEditorViewportClient::DrawCursorReadout(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) const
{
	if (!bCursorInViewport)return;
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	UDreamWidget* RootAgent = Editor.IsValid() ? Editor->GetRootAgentWidget() : nullptr;
	UFont* Font = GEngine->GetSmallFont();
	if (!IsValid(RootAgent) || Font == nullptr)return;

	const FTransform& Transform = RootAgent->GetWorldTransform();
	FVector RayOrigin, RayDirection;
	FSceneView::DeprojectScreenToWorld(FVector2D(HoverPixel.X, HoverPixel.Y), View.UnscaledViewRect,
		View.ViewMatrices.GetClipToWorld(), RayOrigin, RayDirection);
	const FVector OnPlane = FMath::LinePlaneIntersection(RayOrigin, RayOrigin + RayDirection * 100000000.0,
		Transform.GetLocation(), Transform.GetUnitAxis(EAxis::X));
	// The canvas's own rect space -- X right, Y up, measured from its pivot -- which is the space the
	// details panel's numbers are written in. Reporting screen-style top-left units instead would
	// read as a position nothing in this editor can be set to.
	const FVector Local = Transform.InverseTransformPosition(OnPlane);

	const float DpiScale = Canvas.GetDPIScale();
	const FVector2D ViewSize = FVector2D(InViewport.GetSizeXY()) / DpiScale;
	FCanvasTextItem Readout(FVector2D(8.0f, ViewSize.Y - 18.0f),
		FText::FromString(FString::Printf(TEXT("%.0f, %.0f"), Local.Y, Local.Z)), Font, FLinearColor(0.72f, 0.78f, 0.85f));
	Readout.EnableShadow(FLinearColor::Black);
	Canvas.DrawItem(Readout);
}

void FDreamUIPrefabEditorViewportClient::DrawShippedImageOutline(UDreamWidget* InWidget, FSceneView& View, FCanvas& Canvas) const
{
	// Where the widget actually draws, marked on a surface whose handles deliberately stay where it
	// is LAID OUT.
	//
	// The 2D view now projects through the canvas, so a widget inside a Perspective scope is drawn
	// away from its layout rect. The selection outline and its eight handles could have followed it
	// there, and were not, on purpose: the drag path deprojects onto the layout plane, and the drawn
	// surface sits at a different depth, so a handle drawn on one and dragged against the other
	// stops tracking the cursor by the ratio of those depths. Handles that do not stick to the
	// cursor are the worst thing that can happen to a design surface, and the layout rect is also
	// what the numbers in the panel mean. So the blue outline says where it is laid out, and this
	// one says where it lands.
	//
	// Still computed by folding onto the canvas plane rather than by projecting the drawn corners
	// directly, because both paths have to work: under the canvas view the two agree exactly, and
	// under the orthographic fallback -- a world-space canvas, or a canvas that cannot see its own
	// plane -- folding is the only thing that makes the foreshortening visible at all.
	if (!IsValid(InWidget))return;
	UDreamCanvas* RootCanvas = GetPreviewRootCanvas();
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
		if (!DreamWorldToPixelInFront(View, OnPlane, Pixel))return;
		if (!DreamWorldToPixelInFront(View, LayoutTransform.TransformPosition(Local), LayoutPixel))return;
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
		NSLOCTEXT("DreamUIPrefabEditor", "ShippedImageOutline", "shipped"),
		GEngine->GetSmallFont(), ShippedColor);
	Label.EnableShadow(FLinearColor::Black);
	Canvas.DrawItem(Label);
}

void FDreamUIPrefabEditorViewportClient::DrawDesignerOverlay(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	// Drop feedback, hover and the marquee answer "what is this gesture about to do", so they are
	// not what the chrome switch is for -- turning them off mid-drag would leave a gesture running
	// with nothing on screen saying so. They also project through whatever camera is in use, so
	// unlike the plane-bound chrome below they do not belong behind the ortho gate either.
	if (PaletteDropPreviewWidget.IsValid())
	{
		DrawWidgetScreenOutline(PaletteDropPreviewWidget.Get(), View, Canvas, FLinearColor(1.0f, 0.55f, 0.05f), 2.0f);
	}
	DrawHoverOutline(View, Canvas);
	DrawDesignerMarquee(Canvas);
	// What remains is the editor talking about the prefab rather than the prefab itself, so one
	// switch turns the lot off and leaves the art on its own. Only the drawing goes: the handles
	// stay where they were and stay grabbable, because hiding a gesture is not taking it away.
	if (PrefabEditorPtr.IsValid() && !PrefabEditorPtr.Pin()->GetShowDesignerChrome())return;
	if (!IsOrtho())return;
	DrawDesignerCanvasBoundary(InViewport, View, Canvas);
	if (PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->GetShowResolutionGuides())
	{
		DrawResolutionGuides(InViewport, View, Canvas);
		DrawSafeZoneGuide(View, Canvas);
	}
	DrawLayoutDebugOverlay(InViewport, Canvas);
	DrawCursorReadout(InViewport, View, Canvas);
	if (PrefabEditorPtr.IsValid())
	{
		for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
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
	DrawAnchorMedallion(Canvas);
	if (PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->GetSelectedWidgets().Num() == 1)
	{
		if (UDreamWidget* SelectedWidget = PrefabEditorPtr.Pin()->GetSelectedWidgets()[0].Get())
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

void FDreamUIPrefabEditorViewportClient::DrawAnchorMedallion(FCanvas& Canvas) const
{
	if (DesignerAnchorSpaceCorners.Num() != 4 || DesignerAnchorHandlePositions.IsEmpty())return;
	const float DpiScale = Canvas.GetDPIScale();
	// The parent's rect is the anchor space, and the whole point of drawing it is that a fraction
	// means nothing without the thing it is a fraction of.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FCanvasLineItem Line(DesignerAnchorSpaceCorners[Index] / DpiScale, DesignerAnchorSpaceCorners[(Index + 1) % 4] / DpiScale);
		Line.SetColor(FLinearColor(0.08f, 0.4f, 0.22f));
		Canvas.DrawItem(Line);
	}
	for (const auto& Pair : DesignerAnchorHandlePositions)
	{
		constexpr float Size = 8.0f;
		FCanvasTileItem Tile(Pair.Value / DpiScale - FVector2D(Size * 0.5f), FVector2D(Size), FLinearColor(0.15f, 0.9f, 0.5f));
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas.DrawItem(Tile);
	}
}

FBox2D FDreamUIPrefabEditorViewportClient::GetDesignerMarqueeBox() const
{
	FBox2D Box(EForceInit::ForceInit);
	Box += DesignerMarqueePressPixel;
	Box += DesignerMarqueeCurrentPixel;
	return Box;
}

void FDreamUIPrefabEditorViewportClient::DrawDesignerMarquee(FCanvas& Canvas) const
{
	if (!bDesignerMarqueeActive)return;
	const float DpiScale = Canvas.GetDPIScale();
	const FBox2D Box = GetDesignerMarqueeBox();
	FCanvasTileItem Fill(Box.Min / DpiScale, Box.GetSize() / DpiScale, FLinearColor(0.1f, 0.65f, 1.0f, 0.12f));
	Fill.BlendMode = SE_BLEND_Translucent;
	Canvas.DrawItem(Fill);
	const FVector2D Corners[4] = { Box.Min, FVector2D(Box.Max.X, Box.Min.Y), Box.Max, FVector2D(Box.Min.X, Box.Max.Y) };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		FCanvasLineItem Line(Corners[Index] / DpiScale, Corners[(Index + 1) % 4] / DpiScale);
		Line.SetColor(FLinearColor(0.1f, 0.65f, 1.0f));
		Canvas.DrawItem(Line);
	}
}

void FDreamUIPrefabEditorViewportClient::DrawLayoutDebugOverlay(FViewport& InViewport, FCanvas& Canvas) const
{
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	if (!Editor.IsValid() || !Editor->GetShowLayoutDebug() || Editor->GetSelectedWidgets().Num() != 1)
	{
		return;
	}

	UDreamWidget* SelectedWidget = Editor->GetSelectedWidgets()[0].Get();
	if (!IsValid(SelectedWidget))
	{
		return;
	}
	UDreamLayoutContainer* Layout = nullptr;
	if (UDreamWidget* Parent = SelectedWidget->GetParent(); IsValid(Parent))
	{
		Layout = Parent->GetLayoutContainer();
	}
	if (!IsValid(Layout))
	{
		Layout = SelectedWidget->GetLayoutContainer();
	}
	FDreamLayoutDebugInfo Info;
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

void FDreamUIPrefabEditorViewportClient::ReceivedFocus(FViewport* InViewport)
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

void FDreamUIPrefabEditorViewportClient::LostFocus(FViewport* InViewport)
{
	if (bDesignerDragging)FinishDesignerDrag(true);
	bDesignerMarqueePending = false;
	bDesignerMarqueeActive = false;
	bRightMouseButtonDown = false;
	bRightMouseMoved = false;
	RightMouseDownPosition = FIntPoint::ZeroValue;
	FEditorViewportClient::LostFocus(InViewport);

	GEditor->SetPreviewMeshMode(false);
}

void FDreamUIPrefabEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	TickWorld(DeltaSeconds);

	SyncViewFOVToCanvas();

	if (bDesignerDragPending)TryPromoteDesignerDrag();
	if (bDesignerDragging)UpdateDesignerDrag();
	if (bDesignerMarqueePending)TryPromoteDesignerMarquee();
	UpdateHoveredWidget();
}


bool FDreamUIPrefabEditorViewportClient::HandleDesignerInputKey(const FInputKeyEventArgs& EventArgs)
{
	if (!IsOrtho() || !PrefabEditorPtr.IsValid())return false;
	if (EventArgs.Key == EKeys::Escape && EventArgs.Event == IE_Pressed && (bDesignerDragging || bDesignerDragPending || bDesignerMarqueePending))
	{
		bDesignerDragPending = false;
		PendingDesignerHandle = EDesignerHandle::None;
		bDesignerMarqueePending = false;
		bDesignerMarqueeActive = false;
		if (bDesignerDragging)FinishDesignerDrag(true);
		return true;
	}
	if (EventArgs.Key != EKeys::LeftMouseButton)return false;
	if (EventArgs.Event == IE_Released)
	{
		if (bDesignerDragging)
		{
			FinishDesignerDrag(false);
			return true;
		}
		if (bDesignerDragPending)
		{
			// The press never travelled, so it was a click. The Move "handle" is the whole selection
			// rectangle, and a click inside it has to be able to select the child sitting there --
			// otherwise nothing inside a selected panel is ever reachable with the mouse. The edge,
			// corner and pivot handles keep swallowing it: there is nothing under them to select.
			// Anchor markers land in the middle of a centre-anchored widget, so swallowing their
			// click would put four dead discs over exactly the place you click to reach a child.
			const bool bWasMove = PendingDesignerHandle == EDesignerHandle::Move || IsAnchorHandle(PendingDesignerHandle);
			bDesignerDragPending = false;
			PendingDesignerHandle = EDesignerHandle::None;
			if (bWasMove && EventArgs.Viewport)
			{
				SelectWidgetAtPixel(FVector2D(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY()), IsCtrlPressed());
			}
			return true;
		}
		// A marquee is never consumed on release. One that never travelled has to reach the backdrop
		// click that clears the selection; one that did is already past the engine's own click
		// threshold, so ProcessClick will not fire behind it and undo what the marquee just selected.
		if (bDesignerMarqueePending)FinishDesignerMarquee();
		return false;
	}
	if (EventArgs.Event != IE_Pressed || bDesignerDragging || bDesignerDragPending || !Viewport)return false;

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = CalcSceneView(&ViewFamily);
	const FVector2D MousePixel(EventArgs.Viewport->GetMouseX(), EventArgs.Viewport->GetMouseY());
	// No selection means no handles, but it is also the state a marquee is most often started from,
	// so a press that finds no geometry has to fall through to the empty-space case, not bail out.
	const EDesignerHandle HitHandle = View && UpdateDesignerScreenGeometry(*View) ? HitTestDesignerHandle(MousePixel) : EDesignerHandle::None;
	if (HitHandle == EDesignerHandle::None)
	{
		// Any press that missed every handle arms a marquee, and consumes nothing: until it travels
		// it is still the plain click, which goes on selecting or clearing exactly as it did.
		// Asking for nothing under the cursor first would arm nowhere -- the prefab root's own rect
		// covers the canvas, so a pick always answers something.
		bDesignerMarqueePending = true;
		bDesignerMarqueeActive = false;
		DesignerMarqueePressPixel = MousePixel;
		DesignerMarqueeCurrentPixel = MousePixel;
		return false;
	}
	if (!CanBeginDesignerDrag(HitHandle))return false;

	// Consume the press, but stay a click until the mouse actually travels. Opening the transaction
	// here instead would put an empty "Transform Widgets" entry on the undo stack for every click.
	PendingDesignerHandle = HitHandle;
	bDesignerDragPending = true;
	DesignerPressPixel = MousePixel;
	return true;
}

bool FDreamUIPrefabEditorViewportClient::CanBeginDesignerDrag(EDesignerHandle InHandle) const
{
	if (!PrefabEditorPtr.IsValid())return false;
	int32 DraggableCount = 0;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
	{
		if (UDreamWidget* SelectedWidget = WeakWidget.Get())
		{
			if (!PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(SelectedWidget))DraggableCount++;
		}
	}
	if (DraggableCount == 0)return false;
	return InHandle == EDesignerHandle::Move || DraggableCount == 1;
}

void FDreamUIPrefabEditorViewportClient::TryPromoteDesignerDrag()
{
	if (!bDesignerDragPending || !Viewport)return;
	const FVector2D MousePixel(Viewport->GetMouseX(), Viewport->GetMouseY());
	const FVector2D Travel = MousePixel - DesignerPressPixel;
	if (FMath::Square(Travel.X) + FMath::Square(Travel.Y) < MOUSE_CLICK_DRAG_DELTA)return;
	const EDesignerHandle Handle = PendingDesignerHandle;
	bDesignerDragPending = false;
	PendingDesignerHandle = EDesignerHandle::None;
	BeginDesignerDrag(Handle, DesignerPressPixel);
}

void FDreamUIPrefabEditorViewportClient::TryPromoteDesignerMarquee()
{
	if (!bDesignerMarqueePending || !Viewport)return;
	DesignerMarqueeCurrentPixel = FVector2D(Viewport->GetMouseX(), Viewport->GetMouseY());
	if (!bDesignerMarqueeActive)
	{
		const FVector2D Travel = DesignerMarqueeCurrentPixel - DesignerMarqueePressPixel;
		if (FMath::Square(Travel.X) + FMath::Square(Travel.Y) < MOUSE_CLICK_DRAG_DELTA)return;
		bDesignerMarqueeActive = true;
	}
	Invalidate();
}

void FDreamUIPrefabEditorViewportClient::FinishDesignerMarquee()
{
	const bool bWasActive = bDesignerMarqueeActive;
	const FBox2D Box = GetDesignerMarqueeBox();
	bDesignerMarqueePending = false;
	bDesignerMarqueeActive = false;
	if (!bWasActive || !PrefabEditorPtr.IsValid() || !Viewport)return;
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = CalcSceneView(&ViewFamily);
	if (!View)return;

	TArray<UDreamWidget*> Widgets;
	DreamUIWidgetPicking::CollectPickableWidgets(GetWorld(), Widgets);
	TArray<UDreamWidget*> Caught;
	TArray<FVector2D> Corners;
	for (UDreamWidget* Candidate : Widgets)
	{
		// The gates picking applies, for the same reasons: the render-visible flag already folds in
		// hidden-in-designer, and a locked widget is not selectable by any gesture.
		if (!IsValid(Candidate) || !Candidate->GetRenderVisibleInHierarchy())continue;
		if (PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(Candidate))continue;
		if (!DreamProjectWidgetCorners(*View, Candidate, Corners))continue;
		if (DoesMarqueeMeetQuad(Box, Corners))Caught.Add(Candidate);
	}

	TArray<UDreamWidget*> Current;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
	{
		if (UDreamWidget* SelectedWidget = WeakWidget.Get())Current.Add(SelectedWidget);
	}
	const EMarqueeMode Mode = IsCtrlPressed() ? EMarqueeMode::Add : (IsAltPressed() ? EMarqueeMode::Remove : EMarqueeMode::Replace);
	TSet<UDreamWidget*> NewSelection;
	CombineMarqueeSelection(Mode, Current, Caught, NewSelection);
	// SelectWidgets' append mode toggles rather than adds, so the finished set is handed over whole
	// instead: add and remove then mean what they say whatever happened to be selected already.
	PrefabEditorPtr.Pin()->SelectWidgets(NewSelection, false);
	Invalidate();
}

void FDreamUIPrefabEditorViewportClient::SelectWidgetAtPixel(const FVector2D& InPixel, bool bIsControlDown)
{
	if (!PrefabEditorPtr.IsValid())return;
	const FIntPoint ClickPixel(FMath::RoundToInt(InPixel.X), FMath::RoundToInt(InPixel.Y));
	FVector LineStart, LineEnd;
	if (!ComputePickRay(ClickPixel.X, ClickPixel.Y, LineStart, LineEnd))return;
	TArray<UDreamWidget*> Widgets;
	DreamUIWidgetPicking::CollectPickableWidgets(GetWorld(), Widgets);
	// This path and ProcessClick share the one cycle index, so they have to share the rule that
	// resets it -- a click here after a cycle there would otherwise resume that stack's depth.
	IndexOfClickSelectUI = ResolveClickCycleIndex(LastClickPixel, ClickPixel, IndexOfClickSelectUI);
	LastClickPixel = ClickPixel;
	UDreamWidget* Hit = DreamUIWidgetPicking::PickTopmostWidget(GetWorld(), Widgets, LineStart, LineEnd, IndexOfClickSelectUI);
	if (Hit != nullptr && PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(Hit))Hit = nullptr;
	if (Hit != nullptr)PrefabEditorPtr.Pin()->SelectWidgets({Hit}, bIsControlDown);
}

void FDreamUIPrefabEditorViewportClient::BeginDesignerDrag(EDesignerHandle InHandle, const FVector2D& InPressPixel)
{
	if (!PrefabEditorPtr.IsValid())return;
	const EDesignerHandle HitHandle = InHandle;
	const FVector2D MousePixel = InPressPixel;

	TArray<UDreamWidget*> Widgets;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
	{
		if (UDreamWidget* SelectedWidget = WeakWidget.Get())
		{
			if (!PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(SelectedWidget))Widgets.Add(SelectedWidget);
		}
	}
	if (Widgets.IsEmpty())return;
	if (HitHandle != EDesignerHandle::Move && Widgets.Num() != 1)return;

	DesignerSnapshots.Reset();
	DesignerTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("DesignerTransformWidgets", "Transform Widgets"));
	if (UDreamUIPrefabHelperObject* Helper = PrefabEditorPtr.Pin()->GetPrefabHelperObject())
	{
		Helper->Modify();
	}
	for (UDreamWidget* SelectedWidget : Widgets)
	{
		SelectedWidget->Modify();
		FDesignerWidgetSnapshot& Snapshot = DesignerSnapshots.AddDefaulted_GetRef();
		Snapshot.Widget = SelectedWidget;
		Snapshot.AnchoredPosition = SelectedWidget->GetAnchoredPosition();
		Snapshot.AnchorMin = SelectedWidget->GetAnchorMin();
		Snapshot.AnchorMax = SelectedWidget->GetAnchorMax();
		Snapshot.SizeDelta = SelectedWidget->GetSizeDelta();
		Snapshot.Pivot = SelectedWidget->GetPivot();
		Snapshot.Width = SelectedWidget->GetWidth();
		Snapshot.Height = SelectedWidget->GetHeight();
		Snapshot.WorldTransform = SelectedWidget->GetWorldTransform();
		// An anchor lives in the parent's rect, so an anchor drag reads the parent's plane just as a
		// move does; everything else is measured in the widget's own.
		Snapshot.PlaneTransform = (HitHandle == EDesignerHandle::Move || IsAnchorHandle(HitHandle)) && SelectedWidget->GetParent()
			? SelectedWidget->GetParent()->GetWorldTransform() : SelectedWidget->GetWorldTransform();
		const FDreamLayoutControlAnchorData Control = GetEffectiveLayoutControl(SelectedWidget);
		Snapshot.bHorizontalPositionFree = !Control.bCanControlHorizontalPosition;
		Snapshot.bVerticalPositionFree = !Control.bCanControlVerticalPosition;
		IntersectDesignerPlane(MousePixel, Snapshot.PlaneTransform, Snapshot.StartPlanePoint);
	}
	ActiveDesignerHandle = HitHandle;
	DesignerDragStartPixel = MousePixel;
	bDesignerDragging = true;
	bDesignerChanged = false;
	DesignerGuideX.Reset();
	DesignerGuideY.Reset();
}

void FDreamUIPrefabEditorViewportClient::UpdateDesignerDrag()
{
	if (!bDesignerDragging || !Viewport || DesignerSnapshots.IsEmpty() || !PrefabEditorPtr.IsValid())return;
	const FVector2D MousePixel(Viewport->GetMouseX(), Viewport->GetMouseY());
	DesignerGuideX.Reset();
	DesignerGuideY.Reset();
	// A guide line is only worth drawing where the grid actually took over, and it belongs on the
	// value the grid caught: the anchored position for a move -- which is where the pivot sits --
	// the dragged edge for a resize, and the anchor line for an anchor. A pivot drag is clamped
	// rather than snapped, so there is nothing for it to show and it draws none.
	bool bGuideSnappedHorizontal = false, bGuideSnappedVertical = false;
	FVector GuideWorldPoint = FVector::ZeroVector;

	if (ActiveDesignerHandle == EDesignerHandle::Move)
	{
		TArray<UDreamWidget*> Moving;
		TArray<FMoveDragTarget> Targets;
		for (const FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
		{
			UDreamWidget* SelectedWidget = Snapshot.Widget.Get();
			if (!SelectedWidget)continue;
			// Nothing to write on either axis, so skip the widget entirely: SetAnchoredPosition
			// re-dirties the parent whether or not the value changed, and the arrange that follows
			// puts it straight back.
			if (!Snapshot.bHorizontalPositionFree && !Snapshot.bVerticalPositionFree)continue;
			FMoveDragTarget Target;
			if (!IntersectDesignerPlane(MousePixel, Snapshot.PlaneTransform, Target.CurrentPlanePoint))continue;
			Target.PlaneTransform = Snapshot.PlaneTransform;
			Target.StartPlanePoint = Snapshot.StartPlanePoint;
			Target.StartPosition = Snapshot.AnchoredPosition;
			Target.bHorizontalFree = Snapshot.bHorizontalPositionFree;
			Target.bVerticalFree = Snapshot.bVerticalPositionFree;
			Moving.Add(SelectedWidget);
			Targets.Add(Target);
		}
		const float GridSize = PrefabEditorPtr.Pin()->IsDesignerGridSnapEnabled() ? PrefabEditorPtr.Pin()->GetDesignerGridSize() : 0.0f;
		TArray<FMoveDragResult> Results;
		ResolveMoveDrag(Targets, GridSize, Results);
		for (int32 Index = 0; Index < Moving.Num(); ++Index)
		{
			Moving[Index]->SetAnchoredPosition(Results[Index].Position);
		}
		if (!Results.IsEmpty())
		{
			bGuideSnappedHorizontal = Results[0].bSnappedHorizontal;
			bGuideSnappedVertical = Results[0].bSnappedVertical;
			GuideWorldPoint = Moving[0]->GetWorldTransform().GetLocation();
		}
		UpdateDesignerReparentTarget(MousePixel);
	}
	else if (IsAnchorHandle(ActiveDesignerHandle))
	{
		FDesignerWidgetSnapshot& Snapshot = DesignerSnapshots[0];
		UDreamWidget* SelectedWidget = Snapshot.Widget.Get();
		UDreamWidget* ParentWidget = SelectedWidget ? SelectedWidget->GetParent() : nullptr;
		if (!ParentWidget)return;
		const float ParentWidth = ParentWidget->GetWidth();
		const float ParentHeight = ParentWidget->GetHeight();
		if (ParentWidth <= UE_KINDA_SMALL_NUMBER || ParentHeight <= UE_KINDA_SMALL_NUMBER)return;
		FVector CurrentPoint;
		if (!IntersectDesignerPlane(MousePixel, Snapshot.PlaneTransform, CurrentPoint))return;
		// Travel from the press, not the cursor's absolute place in the parent: the markers are drawn
		// a few pixels off their own anchor points so four coincident ones stay separately grabbable,
		// and reading the cursor absolutely would snap the anchor onto the marker the instant it was
		// grabbed -- which is exactly the teleport the rest of this branch exists to avoid.
		const FVector StartLocal = Snapshot.PlaneTransform.InverseTransformPosition(Snapshot.StartPlanePoint);
		const FVector CurrentLocal = Snapshot.PlaneTransform.InverseTransformPosition(CurrentPoint);
		const FVector2D FractionDelta((CurrentLocal.Y - StartLocal.Y) / ParentWidth, (CurrentLocal.Z - StartLocal.Z) / ParentHeight);
		bool bHorizontal = false, bVertical = false;
		GetAnchorEditableAxes(SelectedWidget, bHorizontal, bVertical);
		const bool bMovesMinX = ActiveDesignerHandle == EDesignerHandle::AnchorBottomLeft || ActiveDesignerHandle == EDesignerHandle::AnchorTopLeft;
		const bool bMovesMinY = ActiveDesignerHandle == EDesignerHandle::AnchorBottomLeft || ActiveDesignerHandle == EDesignerHandle::AnchorBottomRight;
		FVector2D NewMin = Snapshot.AnchorMin;
		FVector2D NewMax = Snapshot.AnchorMax;
		if (bHorizontal)
		{
			const double Raw = FMath::Clamp((bMovesMinX ? Snapshot.AnchorMin.X : Snapshot.AnchorMax.X) + FractionDelta.X, 0.0, 1.0);
			const double Moved = SnapAnchorFraction(Raw, DreamAnchorSnapTolerance);
			bGuideSnappedHorizontal = !FMath::IsNearlyEqual(Moved, Raw);
			// The min line may not cross the max line, or the pair describes a rect turned inside out.
			if (bMovesMinX)NewMin.X = FMath::Min(Moved, NewMax.X);
			else NewMax.X = FMath::Max(Moved, NewMin.X);
		}
		if (bVertical)
		{
			const double Raw = FMath::Clamp((bMovesMinY ? Snapshot.AnchorMin.Y : Snapshot.AnchorMax.Y) + FractionDelta.Y, 0.0, 1.0);
			const double Moved = SnapAnchorFraction(Raw, DreamAnchorSnapTolerance);
			bGuideSnappedVertical = !FMath::IsNearlyEqual(Moved, Raw);
			if (bMovesMinY)NewMin.Y = FMath::Min(Moved, NewMax.Y);
			else NewMax.Y = FMath::Max(Moved, NewMin.Y);
		}
		SetAnchorsPreservingRect(SelectedWidget, NewMin, NewMax);
		// The anchor line the gridline caught, in the parent's rect: an anchor fraction is measured
		// from the parent's own left/bottom edge, which sits at -Pivot of its size.
		const FVector2D ParentPivot = ParentWidget->GetPivot();
		GuideWorldPoint = Snapshot.PlaneTransform.TransformPosition(FVector(0,
			((bMovesMinX ? NewMin.X : NewMax.X) - ParentPivot.X) * ParentWidth,
			((bMovesMinY ? NewMin.Y : NewMax.Y) - ParentPivot.Y) * ParentHeight));
	}
	else
	{
		FDesignerWidgetSnapshot& Snapshot = DesignerSnapshots[0];
		UDreamWidget* SelectedWidget = Snapshot.Widget.Get();
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
			const float SnappedY = PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPoint.Y);
			const float SnappedZ = PrefabEditorPtr.Pin()->SnapDesignerValue(LocalPoint.Z);
			if (bChangeLeft)Left = FMath::Min(SnappedY, Right - 1.0f);
			if (bChangeRight)Right = FMath::Max(SnappedY, Left + 1.0f);
			if (bChangeBottom)Bottom = FMath::Min(SnappedZ, Top - 1.0f);
			if (bChangeTop)Top = FMath::Max(SnappedZ, Bottom + 1.0f);
			const float NewWidth = Right - Left;
			const float NewHeight = Top - Bottom;
			const FVector NewOriginLocal(0, Left + Snapshot.Pivot.X * NewWidth, Bottom + Snapshot.Pivot.Y * NewHeight);
			SelectedWidget->SetWidth(NewWidth);
			SelectedWidget->SetHeight(NewHeight);
			SelectedWidget->SetWorldLocation(Snapshot.WorldTransform.TransformPosition(NewOriginLocal));
			// The guide belongs on the edge the grid caught, not on the widget's pivot.
			bGuideSnappedHorizontal = (bChangeLeft || bChangeRight) && !FMath::IsNearlyEqual(SnappedY, (float)LocalPoint.Y);
			bGuideSnappedVertical = (bChangeBottom || bChangeTop) && !FMath::IsNearlyEqual(SnappedZ, (float)LocalPoint.Z);
			GuideWorldPoint = Snapshot.WorldTransform.TransformPosition(
				FVector(0, bChangeLeft ? Left : (bChangeRight ? Right : LocalPoint.Y), bChangeBottom ? Bottom : (bChangeTop ? Top : LocalPoint.Z)));
		}
	}
	bDesignerChanged = false;
	for (const FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
	{
		if (const UDreamWidget* SelectedWidget = Snapshot.Widget.Get())
		{
			bDesignerChanged = !SelectedWidget->GetAnchoredPosition().Equals(Snapshot.AnchoredPosition)
				|| !SelectedWidget->GetAnchorMin().Equals(Snapshot.AnchorMin)
				|| !SelectedWidget->GetAnchorMax().Equals(Snapshot.AnchorMax)
				|| !SelectedWidget->GetPivot().Equals(Snapshot.Pivot)
				|| !FMath::IsNearlyEqual(SelectedWidget->GetWidth(), Snapshot.Width)
				|| !FMath::IsNearlyEqual(SelectedWidget->GetHeight(), Snapshot.Height)
				|| !SelectedWidget->GetWorldTransform().Equals(Snapshot.WorldTransform);
			if (bDesignerChanged)break;
		}
	}

	if (bGuideSnappedHorizontal || bGuideSnappedVertical)
	{
		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
		FVector2D GuidePixel;
		if (FSceneView* View = CalcSceneView(&ViewFamily); View && DreamWorldToPixelInFront(*View, GuideWorldPoint, GuidePixel))
		{
			if (bGuideSnappedHorizontal)DesignerGuideX = GuidePixel.X;
			if (bGuideSnappedVertical)DesignerGuideY = GuidePixel.Y;
		}
	}
	Invalidate();
}

void FDreamUIPrefabEditorViewportClient::GetDraggedWidgets(TArray<UDreamWidget*>& OutWidgets) const
{
	OutWidgets.Reset();
	for (const FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
	{
		if (UDreamWidget* DraggedWidget = Snapshot.Widget.Get())OutWidgets.Add(DraggedWidget);
	}
}

void FDreamUIPrefabEditorViewportClient::UpdateDesignerReparentTarget(const FVector2D& InPixel)
{
	UDreamWidget* Previous = PendingReparentTarget.Get();
	PendingReparentTarget.Reset();
	// The whole selection, not only the widgets whose position the move could write: a reparent is
	// asked about the same set the drop will move, or the hover promises something else than lands.
	TArray<UDreamWidget*> Dragged;
	GetDraggedWidgets(Dragged);
	UDreamWidget* Container = nullptr;
	FVector LineStart, LineEnd;
	if (!Dragged.IsEmpty() && ComputePickRay(FMath::RoundToInt(InPixel.X), FMath::RoundToInt(InPixel.Y), LineStart, LineEnd))
	{
		TArray<UDreamWidget*> Widgets;
		DreamUIWidgetPicking::CollectPickableWidgets(GetWorld(), Widgets);
		// What is being dragged sits under the cursor for the whole gesture, so a pick that can see
		// it answers "the thing in your hand" every frame and never the container you are holding it
		// over. Its descendants go with it: they travel with the widget and are equally in the way.
		Widgets.RemoveAll([&Dragged](UDreamWidget* Candidate)
		{
			if (!IsValid(Candidate))return true;
			for (const UDreamWidget* DraggedWidget : Dragged)
			{
				if (Candidate == DraggedWidget || Candidate->IsChildOf(DraggedWidget))return true;
			}
			return false;
		});
		int32 CycleIndex = INDEX_NONE;
		UDreamWidget* Hit = DreamUIWidgetPicking::PickTopmostWidget(GetWorld(), Widgets, LineStart, LineEnd, CycleIndex);
		TSharedPtr<FDreamUIPrefabEditor> PrefabEditor = PrefabEditorPtr.Pin();
		Container = ResolveDragDropContainer(Hit, PrefabEditor.IsValid() ? PrefabEditor->GetLoadedRootWidget() : nullptr, Dragged,
			[&PrefabEditor](const UDreamWidget* InWidget){ return PrefabEditor.IsValid() && PrefabEditor->IsWidgetLockedForInteraction(InWidget); });
	}
	PendingReparentTarget = Container;
	// The palette's drop outline already says "this is where it would land", and a drag can only be
	// one of the two, so a second colour for the same answer would only invite reading a difference.
	if (Container != Previous)
	{
		if (Container != nullptr)SetPaletteDropPreview(Container);
		else ClearPaletteDropPreview();
	}
}

UDreamWidget* FDreamUIPrefabEditorViewportClient::ResolveDragDropContainer(UDreamWidget* InHit, UDreamWidget* InRoot, TConstArrayView<UDreamWidget*> InDragged, TFunctionRef<bool(const UDreamWidget*)> InIsLocked)
{
	// A lock refuses the drop where it is rather than sending the selection somewhere else, so the
	// walk stops here instead of falling through to the root: dropping a widget out of its parent
	// and onto the page because the panel under the cursor was locked is not what the lock asked for.
	if (InHit != nullptr && InIsLocked(InHit))return nullptr;
	UDreamWidget* Container = DreamUIWidgetPicking::ResolveDropContainer(InHit);
	// The container answers for itself, not only for the pixel: the resolve walks up to the nearest
	// ancestor holding one, and that ancestor is what receives the children.
	if (Container != nullptr && InIsLocked(Container))return nullptr;
	// Nothing above the cursor holds a container at all -- the container-less prefab -- whose answer
	// is its own root, the same answer a palette drop on this pixel already gives.
	if (Container == nullptr && InRoot != nullptr && !InIsLocked(InRoot))Container = InRoot;
	return CanReparentSelectionUnder(InDragged, Container) ? Container : nullptr;
}

bool FDreamUIPrefabEditorViewportClient::CanReparentSelectionUnder(TConstArrayView<UDreamWidget*> InWidgets, const UDreamWidget* InNewParent)
{
	if (!IsValid(InNewParent) || InWidgets.IsEmpty())return false;
	TArray<UDreamWidget*> Candidates;
	Candidates.Reserve(InWidgets.Num());
	for (UDreamWidget* Widget : InWidgets)
	{
		if (!IsValid(Widget))return false;
		// Already there, so there is nothing to reparent and the gesture stays the move it was.
		if (Widget->GetParent() == InNewParent)return false;
		Candidates.Add(Widget);
	}
	// CanAcceptChildren carries the cycle refusals as well as the capacity one: a widget onto itself,
	// and a parent that is one of the dragged widgets' own descendants.
	return InNewParent->CanAcceptChildren(Candidates);
}

bool FDreamUIPrefabEditorViewportClient::ApplyPendingReparent()
{
	UDreamWidget* NewParent = PendingReparentTarget.Get();
	if (!IsValid(NewParent))return false;
	TArray<UDreamWidget*> Dragged;
	GetDraggedWidgets(Dragged);
	// Asked again at the drop rather than trusted from the hover: a pointer stays down for as long
	// as the author holds it, and anything else in the editor may have moved the hierarchy meanwhile.
	if (!CanReparentSelectionUnder(Dragged, NewParent))return false;
	NewParent->SetFlags(RF_Transactional);
	NewParent->Modify();
	bool bReparented = false;
	for (UDreamWidget* DraggedWidget : Dragged)
	{
		if (UDreamWidget* OldParent = DraggedWidget->GetParent(); IsValid(OldParent))
		{
			OldParent->SetFlags(RF_Transactional);
			OldParent->Modify();
		}
		// Keeping the world transform is what leaves the widget where it was dropped. It does not
		// carry the rect a stretch-anchored widget resolves from its anchors, though: those are
		// fractions of a parent that is now a different size, so the extent is put back by hand.
		const float Width = DraggedWidget->GetWidth();
		const float Height = DraggedWidget->GetHeight();
		if (!DraggedWidget->TrySetParent(NewParent, true))continue;
		DraggedWidget->SetWidth(Width);
		DraggedWidget->SetHeight(Height);
		bReparented = true;
	}
	if (bReparented && PrefabEditorPtr.IsValid())
	{
		PrefabEditorPtr.Pin()->RefreshOutliner();
	}
	return bReparented;
}

void FDreamUIPrefabEditorViewportClient::FinishDesignerDrag(bool bCancel)
{
	if (!bDesignerDragging)return;
	if (bCancel)
	{
		for (const FDesignerWidgetSnapshot& Snapshot : DesignerSnapshots)
		{
			if (UDreamWidget* SelectedWidget = Snapshot.Widget.Get())
			{
				// One write, because the anchors and the offsets only mean anything together: putting
				// the size back before the anchors it was measured against would restore a different
				// rect than the one that was snapshotted.
				FDreamUIAnchorData Restored;
				Restored.Pivot = Snapshot.Pivot;
				Restored.AnchorMin = Snapshot.AnchorMin;
				Restored.AnchorMax = Snapshot.AnchorMax;
				Restored.AnchoredPosition = Snapshot.AnchoredPosition;
				Restored.SizeDelta = Snapshot.SizeDelta;
				SelectedWidget->SetAnchorData(Restored);
				SelectedWidget->SetWorldTransform(Snapshot.WorldTransform);
			}
		}
	}
	else
	{
		// The drop into a new container is part of the gesture that positioned the widget, so it goes
		// into the transaction that gesture already opened: one drag, one undo step.
		if (ApplyPendingReparent())bDesignerChanged = true;
		if (bDesignerChanged && PrefabEditorPtr.IsValid())
		{
			if (UDreamUIPrefabHelperObject* Helper = PrefabEditorPtr.Pin()->GetPrefabHelperObject())
			{
				Helper->SetAnythingDirty();
			}
			// Dragging in the designer moves the widget by its anchored position, which the widget
			// resolves into RelativeLocation; that is the property the animation keys.
			TArray<UDreamWidget*> DraggedWidgets;
			GetDraggedWidgets(DraggedWidgets);
			AutoKeyAnimatedTransform(DraggedWidgets, true, false, false);
		}
	}
	if (DesignerTransaction.IsValid() && (bCancel || !bDesignerChanged))DesignerTransaction->Cancel();
	DesignerTransaction.Reset();
	DesignerSnapshots.Reset();
	PendingReparentTarget.Reset();
	ClearPaletteDropPreview();
	ActiveDesignerHandle = EDesignerHandle::None;
	bDesignerDragging = false;
	bDesignerChanged = false;
	DesignerGuideX.Reset();
	DesignerGuideY.Reset();
	Invalidate();
}

bool FDreamUIPrefabEditorViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
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
			if (TSharedPtr<SDreamUIPrefabEditorViewport> EditorViewport = EditorViewportPtr.Pin())
			{
				bHandled |= EditorViewport->SummonContextMenu();
			}
		}
	}

	return bHandled;
}

void FDreamUIPrefabEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	const FViewportClick Click(&View, this, Key, Event, HitX, HitY);

	FVector RayOrigin, RayDirection;
	View.DeprojectScreenToWorld(FVector2D(HitX, HitY), View.UnscaledViewRect, View.ViewMatrices.GetInvViewProjectionMatrix(), RayOrigin, RayDirection);
	const FVector LineStart = RayOrigin;
	const FVector LineEnd = RayOrigin + RayDirection * 100000000.0f;
	TArray<UDreamWidget*> AllWidgetArray;
	DreamUIWidgetPicking::CollectPickableWidgets(this->GetWorld(), AllWidgetArray);
	const FIntPoint ClickPixel((int32)HitX, (int32)HitY);
	IndexOfClickSelectUI = ResolveClickCycleIndex(LastClickPixel, ClickPixel, IndexOfClickSelectUI);
	LastClickPixel = ClickPixel;
	UDreamWidget* ClickHitWidget = DreamUIWidgetPicking::PickTopmostWidget(this->GetWorld(), AllWidgetArray, LineStart, LineEnd, IndexOfClickSelectUI);
	if (ClickHitWidget != nullptr && PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(ClickHitWidget))
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
		DreamUIPrefabViewportClickHandlers::ClickViewport(this, Click);
		return;
	}
	if (!ModeTools->HandleClick(this, HitProxy, Click))
	{
		const FTypedElementHandle HitElement = HitProxy ? HitProxy->GetElementHandle() : FTypedElementHandle();

		if (HitProxy == NULL)
		{
			DreamUIPrefabViewportClickHandlers::ClickBackdrop(this, Click);
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
		else if (HitElement && DreamUIPrefabViewportClickHandlers::ClickElement(this, HitElement, Click))
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
					bComponentSelected = DreamUIPrefabViewportClickHandlers::ClickComponent(this, ActorHitProxy, Click);
				}

				if (!bComponentSelected)
				{
					DreamUIPrefabViewportClickHandlers::ClickActor(this, ConsideredActor, Click, true);
				}

				// We clicked an actor, allow the pivot to reposition itself.
				// GUnrealEd->SetPivotMovedIndependently(false);
			}
		}
		else if (HitProxy->IsA(HInstancedStaticMeshInstance::StaticGetType()))
		{
			DreamUIPrefabViewportClickHandlers::ClickActor(this, ((HInstancedStaticMeshInstance*)HitProxy)->Component->GetOwner(), Click, true);
		}
		//else if (HitProxy->IsA(HBSPBrushVert::StaticGetType()) && ((HBSPBrushVert*)HitProxy)->Brush.IsValid())
		//{
		//	FVector Vertex = FVector(*((HBSPBrushVert*)HitProxy)->Vertex);
		//	DreamGUIPrefabViewportClickHandlers::ClickBrushVertex(this, ((HBSPBrushVert*)HitProxy)->Brush.Get(), &Vertex, Click);
		//}
		else if (HitProxy->IsA(HStaticMeshVert::StaticGetType()))
		{
			DreamUIPrefabViewportClickHandlers::ClickStaticMeshVertex(this, ((HStaticMeshVert*)HitProxy)->Actor, ((HStaticMeshVert*)HitProxy)->Vertex, Click);
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
				DreamUIPrefabViewportClickHandlers::ClickSurface(this, ModelHit->GetModel(), SurfaceIndex, Click);
			}
		}
		else if (HitProxy->IsA(HLevelSocketProxy::StaticGetType()))
		{
			DreamUIPrefabViewportClickHandlers::ClickLevelSocket(this, HitProxy, Click);
		}
	}
}

void FDreamUIPrefabEditorViewportClient::GetGizmoWidgets(TArray<UDreamWidget*>& OutWidgets) const
{
	OutWidgets.Reset();
	if (!PrefabEditorPtr.IsValid())return;
	auto PrefabEditor = PrefabEditorPtr.Pin();
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : PrefabEditor->GetSelectedWidgets())
	{
		UDreamWidget* SelectedWidget = WeakWidget.Get();
		if (!SelectedWidget)continue;
		if (PrefabEditor->IsWidgetLockedForInteraction(SelectedWidget))continue;
		if (PrefabEditor->IsWidgetHiddenInDesigner(SelectedWidget))continue;
		OutWidgets.Add(SelectedWidget);
	}
}

void FDreamUIPrefabEditorViewportClient::ApplyDeltaToSelectedWidgets(const FVector& Drag, const FRotator& Rot, const FVector& Scale)
{
	TArray<UDreamWidget*> Widgets;
	GetGizmoWidgets(Widgets);
	if (Widgets.IsEmpty())return;
	const bool bHasDrag = !Drag.IsNearlyZero();
	const bool bHasRot = !Rot.IsNearlyZero();
	const bool bHasScale = !Scale.IsNearlyZero();
	if (!bHasDrag && !bHasRot && !bHasScale)return;

	// Rotation and scale pivot on the gizmo, which sits at the selection's centre; with one widget
	// selected that centre is its own origin, so both cases fall out of the same code.
	const FVector Pivot = GetWidgetLocation();
	const FQuat DeltaRotation = Rot.Quaternion();
	for (UDreamWidget* TargetWidget : Widgets)
	{
		TargetWidget->Modify();
		const FTransform WorldTransform = TargetWidget->GetWorldTransform();
		FVector NewLocation = WorldTransform.GetLocation();
		FQuat NewRotation = WorldTransform.GetRotation();
		if (bHasDrag)
		{
			NewLocation += Drag;
		}
		if (bHasRot)
		{
			NewRotation = DeltaRotation * NewRotation;
			NewLocation = Pivot + DeltaRotation.RotateVector(NewLocation - Pivot);
		}
		if (bHasScale)
		{
			// The widget's delta is additive on the relative scale, which is what the level editor does.
			const FVector NewScale = TargetWidget->GetRelativeScale() + Scale;
			FDreamUIUtils::ChangePropertyWithNotify(TargetWidget, UDreamWidget::GetPropertyName_RelativeScale(), [=]
			{
				TargetWidget->SetRelativeScale(NewScale);
			});
		}
		if (bHasRot)
		{
			FDreamUIUtils::ChangePropertyWithNotify(TargetWidget, UDreamWidget::GetPropertyName_RelativeRotation(), [=]
			{
				FDreamUIUtils::ChangePropertyWithNotify(TargetWidget, UDreamWidget::GetPropertyName_RelativeLocation(), [=]
				{
					TargetWidget->SetWorldLocationAndRotation(NewLocation, NewRotation);
				});
			});
		}
		else if (bHasDrag)
		{
			FDreamUIUtils::ChangePropertyWithNotify(TargetWidget, UDreamWidget::GetPropertyName_RelativeLocation(), [=]
			{
				TargetWidget->SetWorldLocation(NewLocation);
			});
		}
	}
}

bool FDreamUIPrefabEditorViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type InCurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (GUnrealEd->ComponentVisManager.IsActive() && GUnrealEd->ComponentVisManager.HandleInputDelta(this, InViewport, Drag, Rot, Scale))
	{
		return true;
	}

	// A prefab's contents are DreamWidgets, not actors, so the engine's widget hands its delta here
	// instead of to ApplyDeltaToActors. Everything else about the gizmo -- drawing, axis picking,
	// snapping, the coordinate space, the W/E/R modes -- stays the engine's.
	if (InCurrentAxis != EAxisList::None)
	{
		TArray<UDreamWidget*> Widgets;
		GetGizmoWidgets(Widgets);
		if (!Widgets.IsEmpty())
		{
			ApplyDeltaToSelectedWidgets(Drag, Rot, Scale);
			return true;
		}
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
UE::Widget::EWidgetMode FDreamUIPrefabEditorViewportClient::GetWidgetMode() const
{
	if (GUnrealEd->ComponentVisManager.IsActive() && GUnrealEd->ComponentVisManager.IsVisualizingArchetype())
	{
		return UE::Widget::WM_None;
	}
	// The designer view has its own rect handles; the gizmo belongs to the perspective view.
	if (!IsPerspective())
	{
		return UE::Widget::WM_None;
	}

	return FEditorViewportClient::GetWidgetMode();
}
FVector FDreamUIPrefabEditorViewportClient::GetWidgetLocation() const
{
	FVector ComponentVisWidgetLocation;
	if (GUnrealEd->ComponentVisManager.GetWidgetLocation(this, ComponentVisWidgetLocation))
	{
		return ComponentVisWidgetLocation;
	}

	TArray<UDreamWidget*> Widgets;
	GetGizmoWidgets(Widgets);
	if (!Widgets.IsEmpty())
	{
		FVector Centre = FVector::ZeroVector;
		for (const UDreamWidget* SelectedWidget : Widgets)
		{
			Centre += SelectedWidget->GetWorldTransform().GetLocation();
		}
		return Centre / (double)Widgets.Num();
	}

	return FEditorViewportClient::GetWidgetLocation();
}
FMatrix FDreamUIPrefabEditorViewportClient::GetWidgetCoordSystem() const
{
	FMatrix ComponentVisWidgetCoordSystem;
	if (GUnrealEd->ComponentVisManager.GetCustomInputCoordinateSystem(this, ComponentVisWidgetCoordSystem))
	{
		return ComponentVisWidgetCoordSystem;
	}

	if (GetWidgetCoordSystemSpace() == COORD_Local)
	{
		TArray<UDreamWidget*> Widgets;
		GetGizmoWidgets(Widgets);
		if (!Widgets.IsEmpty())
		{
			// The first of the selection, the same one whose rotation the old gizmo took.
			return FRotationMatrix::Make(Widgets[0]->GetWorldTransform().GetRotation());
		}
	}

	return FEditorViewportClient::GetWidgetCoordSystem();
}

UDreamCanvas* FDreamUIPrefabEditorViewportClient::GetPreviewRootCanvas()const
{
	if (!PrefabEditorPtr.IsValid())return nullptr;
	UDreamWidget* RootAgent = PrefabEditorPtr.Pin()->GetRootAgentWidget();
	if (!IsValid(RootAgent))return nullptr;
	UDreamCanvas* Canvas = RootAgent->GetComponent<UDreamCanvas>();
	// GetViewLocation and GetProjectionMatrix dereference GetWidget() with no null check of their
	// own. The runtime only ever called them mid-frame with everything alive; this runs on editor
	// ticks and across prefab close and world teardown, so the guard belongs here.
	if (!IsValid(Canvas) || !IsValid(Canvas->GetWidget()))return nullptr;
	return Canvas;
}

void FDreamUIPrefabEditorViewportClient::SyncViewFOVToCanvas()
{
	// The editor camera ships with a 90 degree lens; the canvas's is FieldOfView, 60 by default.
	// A Perspective scope bakes its geometry for the canvas's eye, so looking at that geometry
	// through a different lens shows a foreshortening that is nobody's -- not the authored intent
	// and not what ships. Matching the lens is half of making the 3D view honest. The other half is
	// standing in the right place, which is FrameFromCanvasEye; this half is unconditional because
	// there is no reading of this viewport for which a mismatched lens is the right answer.
	if (!IsPerspective())return;//an ortho view has no field of view to match
	UDreamCanvas* RootCanvas = GetPreviewRootCanvas();
	if (RootCanvas == nullptr || RootCanvas->GetProjectionType() != ECameraProjectionMode::Perspective)return;
	const float CanvasFOV = RootCanvas->GetFieldOfView();
	if (CanvasFOV > 0.0f && !FMath::IsNearlyEqual(ViewFOV, CanvasFOV))
	{
		ViewFOV = CanvasFOV;
		Invalidate();
	}
}

bool FDreamUIPrefabEditorViewportClient::CanFrameFromCanvasEye()const
{
	UDreamCanvas* RootCanvas = GetPreviewRootCanvas();
	// A world-space canvas does not project through its own camera, and an orthographic one has its
	// eye at infinity. In both cases there is no eye to stand at, and Perspective is inert anyway.
	return RootCanvas != nullptr
		&& !RootCanvas->IsRenderToWorldSpace()
		&& RootCanvas->GetProjectionType() == ECameraProjectionMode::Perspective;
}

void FDreamUIPrefabEditorViewportClient::FrameFromCanvasEye()
{
	UDreamCanvas* RootCanvas = GetPreviewRootCanvas();
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

bool FDreamUIPrefabEditorViewportClient::ShouldUseCanvasView()const
{
	// Only the 2D view. The 3D view has a camera the author is actually steering, and Canvas Eye is
	// how they align it with the canvas there.
	if (GetViewportType() != LVT_OrthoYZ)return false;
	if (!CanFrameFromCanvasEye())return false;
	UDreamCanvas* RootCanvas = GetPreviewRootCanvas();
	UDreamWidget* RootAgent = RootCanvas ? RootCanvas->GetWidget() : nullptr;
	if (!IsValid(RootAgent))return false;
	// Measured rather than recomputed from Width and FieldOfView: GetViewLocation honours
	// bOverrideViewLocation, and the near and far planes apply to where the eye actually is.
	const FTransform& RootTransform = RootAgent->GetWorldTransform();
	const FVector Normal = RootTransform.TransformVector(FVector::XAxisVector).GetSafeNormal();
	const double EyeToPlane = FVector::DotProduct(RootTransform.GetLocation() - RootCanvas->GetViewLocation(), Normal);
	return DreamCanvasViewFit::IsCanvasViewUsable(EyeToPlane, RootCanvas->GetNearClipPlane(), RootCanvas->GetFarClipPlane());
}

FSceneView* FDreamUIPrefabEditorViewportClient::CalcSceneView(FSceneViewFamily* ViewFamily, const int32 StereoViewIndex)
{
	FSceneView* View = FEditorViewportClient::CalcSceneView(ViewFamily, StereoViewIndex);
	if (View == nullptr || !ShouldUseCanvasView())return View;
	UDreamCanvas* RootCanvas = GetPreviewRootCanvas();
	UDreamWidget* RootAgent = RootCanvas ? RootCanvas->GetWidget() : nullptr;
	if (!IsValid(RootAgent) || RootAgent->GetWidth() <= 0.0f || RootAgent->GetHeight() <= 0.0f)return View;

	// Captured BEFORE anything is replaced: this is the framing the orthographic view was about to
	// produce, and it is what zoom and pan currently mean. The correction below reproduces it for
	// everything in the canvas plane, so switching the projection does not move the design surface.
	// Read from the matrices rather than from SceneViewInitOptions.ViewOrigin, which for an ortho
	// view has been pushed roughly two million units backwards by FSceneViewInitOptions::
	// UpdateOrthoPlanes -- and only in some view modes, so trusting it would work in Lit and fail
	// in Unlit.
	const FMatrix ReferenceWorldToClip = View->ViewMatrices.GetWorldToClip();
	const FTransform& RootTransform = RootAgent->GetWorldTransform();
	FMatrix Correction;
	if (!DreamCanvasViewFit::BuildClipCorrection(
		RootCanvas->GetViewProjectionMatrix(),
		ReferenceWorldToClip,
		RootTransform.GetLocation(),
		RootTransform.TransformVector(FVector::YAxisVector),
		RootTransform.TransformVector(FVector::ZAxisVector),
		RootAgent->GetWidth() * 0.5f,
		Correction))
	{
		return View;//no correction of that shape exists; keep the orthographic view rather than ship a wrong one
	}

	View->SceneViewInitOptions.ViewOrigin = RootCanvas->GetViewLocation();
	View->SceneViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(RootCanvas->GetViewRotator())
		* FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
	// UpdateProjectionMatrix, not a direct assignment to ViewMatrices: it rebuilds the whole
	// FViewMatrices from the init options just edited, and refreshes the unadjusted projection, the
	// device-Z transform and the frustum with it. The correction goes on the RIGHT because UE
	// composes row vectors, so it must act on clip coordinates rather than on view space.
	View->UpdateProjectionMatrix(RootCanvas->GetProjectionMatrix() * Correction);

	// ViewLocation and ViewRotation are set by the base from the editor transform and are what any
	// later UpdateViewMatrix would re-derive everything from, so they have to agree.
	View->ViewLocation = RootCanvas->GetViewLocation();
	View->ViewRotation = RootCanvas->GetViewRotator();
	// FSceneView zeroes these in its orthographic branch at construction and UpdateProjectionMatrix
	// does not revisit them, so without this the view renders perspective while reporting no field
	// of view at all.
	const float CanvasFOV = RootCanvas->GetFieldOfView();
	View->FOV = CanvasFOV;
	View->DesiredFOV = CanvasFOV;
	View->bUseFieldOfViewForLOD = true;
	return View;
}

void FDreamUIPrefabEditorViewportClient::SetViewportType(ELevelViewportType InViewportType)
{
	FEditorViewportClient::SetViewportType(InViewportType);
	GetPrefabBeingEdited()->GetPrefabInstanceScene()->SetSkyCubeVisibility(IsPerspective());
	// The editor's grid branches on the PROJECTION MATRIX, not on the viewport type, so once the 2D
	// view projects through the canvas it would swap the flat design grid for the world-space
	// perspective one -- seen edge-on from the canvas eye, i.e. a single stray horizontal line.
	DrawHelper.bDrawGrid = !ShouldUseCanvasView();
}

/**
 * Returns the horizontal axis for this viewport.
 */

EAxisList::Type FDreamUIPrefabEditorViewportClient::GetHorizAxis() const
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

EAxisList::Type FDreamUIPrefabEditorViewportClient::GetVertAxis() const
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
void FDreamUIPrefabEditorViewportClient::NudgeSelectedObjects(const struct FInputEventState& InputState)
{
	if (!PrefabEditorPtr.IsValid())return;
	FViewport* InViewport = InputState.GetViewport();
	EInputEvent Event = InputState.GetInputEvent();
	FKey Key = InputState.GetKey();

	FVector2D KeyDelta(0, 0);
	if (Key == EKeys::Left) KeyDelta.X = -1;
	else if (Key == EKeys::Right) KeyDelta.X = 1;
	else if (Key == EKeys::Up) KeyDelta.Y = 1;
	else if (Key == EKeys::Down) KeyDelta.Y = -1;

	// The release closes whatever the press opened, so it cannot stand behind a predicate that may
	// answer differently than it did at the press. Between the two an arranger can claim the axes --
	// a layout container added by another panel, an aspect-ratio layout-self switched on, the widget
	// reparented -- and the transaction would then stay open for every later edit to join. What it
	// may not do is end a transaction it never opened: the stack is the editor's, and the one
	// underneath is as likely to be the designer drag's, which would lose the ability to cancel.
	if (Event == IE_Released)
	{
		if (bNudgeTransactionOpen)
		{
			bNudgeTransactionOpen = false;
			GEditor->EndTransaction();
		}
		RedrawAllViewportsIntoThisScene();
		return;
	}

	TArray<UDreamWidget*> Movable;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : PrefabEditorPtr.Pin()->GetSelectedWidgets())
	{
		if (UDreamWidget* SelectedWidget = WeakWidget.Get())
		{
			// An axis someone else arranges is dropped rather than the widget: nudging a widget whose
			// X is decided and whose Y is free still has to move it in Y.
			if (PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(SelectedWidget))continue;
			if (FilterMoveDelta(KeyDelta, GetEffectiveLayoutControl(SelectedWidget)).IsZero())continue;
			Movable.Add(SelectedWidget);
		}
	}
	// Before the transaction, not after: an opened-and-closed transaction is an undo step, and the
	// prefab is dirtied by opening it, for a nudge whose every write the next arrange discards.
	if (Movable.IsEmpty())return;

	const int32 MouseX = InViewport->GetMouseX();
	const int32 MouseY = InViewport->GetMouseY();

	if (Event == IE_Pressed)
	{
		GEditor->BeginTransaction(LOCTEXT("MoveWidget", "Move Widget"));
		bNudgeTransactionOpen = true;
		if (PrefabEditorPtr.IsValid())
		{
			if (UDreamUIPrefabHelperObject* Helper = PrefabEditorPtr.Pin()->GetPrefabHelperObject())
			{
				Helper->Modify();
				Helper->SetAnythingDirty();
			}
		}
		for (UDreamWidget* DreamWidget : Movable)
		{
			DreamWidget->Modify();
		}
	}

	if (Event == IE_Pressed || Event == IE_Repeat)
	{
		FVector2D MouseDelta = KeyDelta;
		if (PrefabEditorPtr.Pin()->IsDesignerGridSnapEnabled())
		{
			MouseDelta *= PrefabEditorPtr.Pin()->GetDesignerGridSize();
		}

		for (UDreamWidget* DreamWidget : Movable)
		{
			DreamWidget->SetAnchoredPosition(DreamWidget->GetAnchoredPosition() + FilterMoveDelta(MouseDelta, GetEffectiveLayoutControl(DreamWidget)));
		}
	}

	RedrawAllViewportsIntoThisScene();
}

void FDreamUIPrefabEditorViewportClient::ApplyDeltaToActors(const FVector& InDrag, const FRotator& InRot, const FVector& InScale)
{
	ApplyDeltaToSelectedElements(FTransform(InRot, InDrag, InScale));
}

void FDreamUIPrefabEditorViewportClient::ApplyDeltaToActor(AActor* InActor, const FVector& InDeltaDrag, const FRotator& InDeltaRot, const FVector& InDeltaScale)
{
	if (FTypedElementHandle ActorElementHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor))
	{
		ApplyDeltaToElement(ActorElementHandle, FTransform(InDeltaRot, InDeltaDrag, InDeltaScale));
	}
}

void FDreamUIPrefabEditorViewportClient::ApplyDeltaToComponent(USceneComponent* InComponent, const FVector& InDeltaDrag, const FRotator& InDeltaRot, const FVector& InDeltaScale)
{
	if (FTypedElementHandle ComponentElementHandle = UEngineElementsLibrary::AcquireEditorComponentElementHandle(InComponent))
	{
		ApplyDeltaToElement(ComponentElementHandle, FTransform(InDeltaRot, InDeltaDrag, InDeltaScale));
	}
}

void FDreamUIPrefabEditorViewportClient::ApplyDeltaToSelectedElements(const FTransform& InDeltaTransform)
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

void FDreamUIPrefabEditorViewportClient::ApplyDeltaToElement(const FTypedElementHandle& InElementHandle, const FTransform& InDeltaTransform)
{
	FInputDeviceState InputState;
	InputState.SetModifierKeyStates(IsShiftPressed(), IsAltPressed(), IsCtrlPressed(), IsCmdPressed());

	ViewportInteraction->ApplyDeltaToElement(InElementHandle, GetWidgetMode(), Widget ? Widget->GetCurrentAxis() : EAxisList::None, InputState, InDeltaTransform);
}

FTypedElementListConstRef FDreamUIPrefabEditorViewportClient::GetElementsToManipulate(const bool bForceRefresh)
{
	CacheElementsToManipulate(bForceRefresh);
	return CachedElementsToManipulate;
}

void FDreamUIPrefabEditorViewportClient::CacheElementsToManipulate(const bool bForceRefresh)
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
void FDreamUIPrefabEditorViewportClient::ResetElementsToManipulate(const bool bClearList)
{
	if (bClearList)
	{
		CachedElementsToManipulate->Reset();
	}
	bHasCachedElementsToManipulate = false;
}

void FDreamUIPrefabEditorViewportClient::ResetElementsToManipulateFromSelectionChange(const UTypedElementSelectionSet* InSelectionSet)
{
	check(InSelectionSet == GetSelectionSet());

	// Don't clear the list immediately, as the selection may change from a construction script running (while we're still iterating the list!)
	// We'll process the clear on the next cache request, or when the typed element registry actually processes its pending deletion
	ResetElementsToManipulate(/*bClearList*/false);
}

void FDreamUIPrefabEditorViewportClient::ResetElementsToManipulateFromProcessingDeferredElementsToDestroy()
{
	if (!bHasCachedElementsToManipulate)
	{
		// If we have no cache, make sure the cached list is definitely empty now to ensure it doesn't contain any lingering references to things that are about to be deleted
		CachedElementsToManipulate->Reset();
	}
}

const UTypedElementSelectionSet* FDreamUIPrefabEditorViewportClient::GetSelectionSet() const
{
	return GEditor->GetSelectedActors()->GetElementSelectionSet();
}

UTypedElementSelectionSet* FDreamUIPrefabEditorViewportClient::GetMutableSelectionSet() const
{
	return GEditor->GetSelectedActors()->GetElementSelectionSet();
}


void FDreamUIPrefabEditorViewportClient::TickWorld(float DeltaSeconds)
{
	GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
}

bool FDreamUIPrefabEditorViewportClient::FocusViewportToTargets()
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

FDreamLayoutControlAnchorData FDreamUIPrefabEditorViewportClient::GetEffectiveLayoutControl(const UDreamWidget* InWidget)
{
	FDreamLayoutControlAnchorData Control;
	if (!IsValid(InWidget))return Control;
	if (UDreamWidget* ParentWidget = InWidget->GetParent(); IsValid(ParentWidget))
	{
		if (UDreamLayoutContainer* ParentLayout = ParentWidget->GetLayoutContainer(); IsValid(ParentLayout))
		{
			Control.Or(ParentLayout->GetLayoutControlAnchor(InWidget));
		}
	}
	// A layout-self can claim an axis its parent left alone -- an aspect-ratio widget owns its own
	// size whether or not anything is arranging it.
	if (UDreamLayoutSelf* SelfLayout = InWidget->GetLayoutSelf(); IsValid(SelfLayout))
	{
		Control.Or(SelfLayout->GetLayoutControlAnchor(InWidget));
	}
	return Control;
}

void FDreamUIPrefabEditorViewportClient::GetAnchorEditableAxes(const UDreamWidget* InWidget, bool& bOutHorizontal, bool& bOutVertical)
{
	bOutHorizontal = false;
	bOutVertical = false;
	// No parent, no anchor space: an anchor is a fraction of the rect a widget is placed inside.
	if (!IsValid(InWidget) || !IsValid(InWidget->GetParent()))return;
	const FDreamLayoutControlAnchorData Control = GetEffectiveLayoutControl(InWidget);
	bOutHorizontal = !Control.bCanControlHorizontalPosition && !Control.bCanControlHorizontalSize;
	bOutVertical = !Control.bCanControlVerticalPosition && !Control.bCanControlVerticalSize;
}

bool FDreamUIPrefabEditorViewportClient::CanMoveSelection(TConstArrayView<UDreamWidget*> InWidgets)
{
	for (const UDreamWidget* Widget : InWidgets)
	{
		if (!IsValid(Widget))continue;
		const FDreamLayoutControlAnchorData Control = GetEffectiveLayoutControl(Widget);
		if (!Control.bCanControlHorizontalPosition || !Control.bCanControlVerticalPosition)return true;
	}
	return false;
}

FVector2D FDreamUIPrefabEditorViewportClient::FilterMoveDelta(const FVector2D& InDelta, const FDreamLayoutControlAnchorData& InControl)
{
	return FVector2D(InControl.bCanControlHorizontalPosition ? 0.0 : InDelta.X, InControl.bCanControlVerticalPosition ? 0.0 : InDelta.Y);
}

void FDreamUIPrefabEditorViewportClient::ResolveMoveDrag(TConstArrayView<FMoveDragTarget> InTargets, float InGridSize, TArray<FMoveDragResult>& OutResults)
{
	OutResults.Reset();
	OutResults.Reserve(InTargets.Num());
	auto RawPositionOf = [](const FMoveDragTarget& InTarget)
	{
		const FVector StartLocal = InTarget.PlaneTransform.InverseTransformPosition(InTarget.StartPlanePoint);
		const FVector CurrentLocal = InTarget.PlaneTransform.InverseTransformPosition(InTarget.CurrentPlanePoint);
		return InTarget.StartPosition + FVector2D(CurrentLocal.Y - StartLocal.Y, CurrentLocal.Z - StartLocal.Z);
	};
	// Snapping every target onto its own nearest gridline collapses the gaps between widgets that
	// started at different sub-grid offsets: the selection deforms as it is dragged and two of them
	// can land on top of each other. So the grid is consulted once and everyone rides the same
	// correction.
	//
	// That correction has to travel through WORLD space, not as a bare pair of numbers. Each target
	// is measured in its own parent's frame, so parents at different scales or rotations give the
	// same on-screen distance different local values -- adding one parent's number to another
	// parent's position is the very deformation this is here to prevent.
	//
	// Per axis, too, and off the first target free on that axis: a target whose X an arranger owns
	// is never given the X measured for it, so a correction read from that X bends everyone else
	// towards a gridline nothing was going to sit on.
	FVector CorrectionWorld = FVector::ZeroVector;
	bool bSnappedX = false;
	bool bSnappedY = false;
	if (InGridSize > 0.0f)
	{
		auto AccumulateAxis = [&](bool bHorizontal)
		{
			const FMoveDragTarget* Leader = InTargets.FindByPredicate([bHorizontal](const FMoveDragTarget& InTarget)
			{
				return bHorizontal ? InTarget.bHorizontalFree : InTarget.bVerticalFree;
			});
			if (Leader == nullptr)return false;
			const FVector2D Raw = RawPositionOf(*Leader);
			const double Axis = bHorizontal ? Raw.X : Raw.Y;
			const double Delta = FMath::GridSnap(Axis, (double)InGridSize) - Axis;
			if (FMath::IsNearlyZero(Delta))return false;
			// Local Y is the horizontal axis of a widget's plane and local Z the vertical one,
			// which is the same mapping RawPositionOf reads back out.
			CorrectionWorld += Leader->PlaneTransform.TransformVector(
				bHorizontal ? FVector(0.0, Delta, 0.0) : FVector(0.0, 0.0, Delta));
			return true;
		};
		bSnappedX = AccumulateAxis(true);
		bSnappedY = AccumulateAxis(false);
	}
	for (const FMoveDragTarget& Target : InTargets)
	{
		const FVector Local = Target.PlaneTransform.InverseTransformVector(CorrectionWorld);
		const FVector2D Snapped = RawPositionOf(Target) + FVector2D(Local.Y, Local.Z);
		FDreamLayoutControlAnchorData Control;
		Control.bCanControlHorizontalPosition = !Target.bHorizontalFree;
		Control.bCanControlVerticalPosition = !Target.bVerticalFree;
		FMoveDragResult& Result = OutResults.AddDefaulted_GetRef();
		Result.Position = Target.StartPosition + FilterMoveDelta(Snapped - Target.StartPosition, Control);
		Result.bSnappedHorizontal = Target.bHorizontalFree && bSnappedX;
		Result.bSnappedVertical = Target.bVerticalFree && bSnappedY;
	}
}

int32 FDreamUIPrefabEditorViewportClient::ResolveClickCycleIndex(const FIntPoint& InLastClickPixel, const FIntPoint& InClickPixel, int32 InCurrentIndex)
{
	return InClickPixel == InLastClickPixel ? InCurrentIndex : INDEX_NONE;
}

FBox2D FDreamUIPrefabEditorViewportClient::GetSafeZoneLocalRect(const FVector2D& InCanvasSize, const FVector2D& InCanvasPivot, const FVector4& InSafeZonePadding)
{
	const double Left = -InCanvasPivot.X * InCanvasSize.X;
	const double Bottom = -InCanvasPivot.Y * InCanvasSize.Y;
	const FVector2D Min(Left + InSafeZonePadding.X, Bottom + InSafeZonePadding.W);
	const FVector2D Max(Left + InCanvasSize.X - InSafeZonePadding.Z, Bottom + InCanvasSize.Y - InSafeZonePadding.Y);
	// Padding wider than the canvas leaves no safe area at all. Answering with the turned-inside-out
	// rect would draw a shape an author could read as one.
	if (Min.X >= Max.X || Min.Y >= Max.Y)return FBox2D(EForceInit::ForceInit);
	return FBox2D(Min, Max);
}

double FDreamUIPrefabEditorViewportClient::SnapAnchorFraction(double InFraction, double InTolerance)
{
	// The five stops the details panel's preset grid offers, so the surface gesture can land on a
	// value that grid will recognise instead of on 0.2487.
	for (const double Gridline : { 0.0, 0.25, 0.5, 0.75, 1.0 })
	{
		if (FMath::Abs(InFraction - Gridline) <= InTolerance)return Gridline;
	}
	return InFraction;
}

void FDreamUIPrefabEditorViewportClient::SetAnchorsPreservingRect(UDreamWidget* InWidget, const FVector2D& InAnchorMin, const FVector2D& InAnchorMax)
{
	if (!IsValid(InWidget))return;
	UDreamWidget* ParentWidget = InWidget->GetParent();
	if (!IsValid(ParentWidget))return;
	const FVector2D ParentSize(ParentWidget->GetWidth(), ParentWidget->GetHeight());
	const FVector2D Size(InWidget->GetWidth(), InWidget->GetHeight());
	// The offsets are measured from the anchor lines, so a line that travels by D leaves the rect
	// sitting D further from it than it was. Hand the offsets that D back and nothing has moved.
	const FVector2D OffsetMin(InWidget->GetAnchorOffsetLeft(), InWidget->GetAnchorOffsetBottom());
	const FVector2D NewOffsetMin = OffsetMin + ParentSize * (InWidget->GetAnchorMin() - InAnchorMin);
	FDreamUIAnchorData NewData = InWidget->GetAnchorData();
	NewData.AnchorMin = InAnchorMin;
	NewData.AnchorMax = InAnchorMax;
	// The stretched span is parent-driven, so SizeDelta is what is left of the size after it.
	NewData.SizeDelta = Size - ParentSize * (InAnchorMax - InAnchorMin);
	NewData.AnchoredPosition = NewOffsetMin + NewData.SizeDelta * NewData.Pivot;
	InWidget->SetAnchorData(NewData);
}

bool FDreamUIPrefabEditorViewportClient::DoesMarqueeMeetQuad(const FBox2D& InMarquee, TConstArrayView<FVector2D> InQuad)
{
	if (!InMarquee.bIsValid || InQuad.Num() != 4)return false;
	const FVector2D BoxCorners[4] = { InMarquee.Min, FVector2D(InMarquee.Max.X, InMarquee.Min.Y), InMarquee.Max, FVector2D(InMarquee.Min.X, InMarquee.Max.Y) };
	// Separating axes: the box's own two, plus the quad's edge normals. A projected widget rect is
	// only axis-aligned while the widget is unrotated, and judging a rotated one by its bounding box
	// would hand it to a marquee that passed through a corner it does not occupy.
	TArray<FVector2D, TInlineAllocator<6>> Axes;
	Axes.Add(FVector2D(1.0, 0.0));
	Axes.Add(FVector2D(0.0, 1.0));
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector2D Edge = InQuad[(Index + 1) % 4] - InQuad[Index];
		if (Edge.IsNearlyZero())continue;
		Axes.Add(FVector2D(-Edge.Y, Edge.X));
	}
	for (const FVector2D& Axis : Axes)
	{
		double BoxMin = TNumericLimits<double>::Max(), BoxMax = TNumericLimits<double>::Lowest();
		for (const FVector2D& Corner : BoxCorners)
		{
			const double Projected = FVector2D::DotProduct(Corner, Axis);
			BoxMin = FMath::Min(BoxMin, Projected);
			BoxMax = FMath::Max(BoxMax, Projected);
		}
		double QuadMin = TNumericLimits<double>::Max(), QuadMax = TNumericLimits<double>::Lowest();
		for (const FVector2D& Corner : InQuad)
		{
			const double Projected = FVector2D::DotProduct(Corner, Axis);
			QuadMin = FMath::Min(QuadMin, Projected);
			QuadMax = FMath::Max(QuadMax, Projected);
		}
		if (BoxMax < QuadMin || QuadMax < BoxMin)return false;
	}
	return true;
}

void FDreamUIPrefabEditorViewportClient::CombineMarqueeSelection(EMarqueeMode InMode, TConstArrayView<UDreamWidget*> InCurrent, TConstArrayView<UDreamWidget*> InCaught, TSet<UDreamWidget*>& OutSelection)
{
	OutSelection.Reset();
	if (InMode != EMarqueeMode::Replace)
	{
		for (UDreamWidget* Widget : InCurrent)OutSelection.Add(Widget);
	}
	for (UDreamWidget* Widget : InCaught)
	{
		if (InMode == EMarqueeMode::Remove)OutSelection.Remove(Widget);
		else OutSelection.Add(Widget);
	}
}

bool FDreamUIPrefabEditorViewportClient::ComputePickRay(int32 PixelX, int32 PixelY, FVector& OutLineStart, FVector& OutLineEnd)
{
	if (!Viewport || !GetWorld())return false;
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(Viewport, GetScene(), EngineShowFlags));
	FSceneView* View = CalcSceneView(&ViewFamily);
	if (!View)return false;
	FVector RayOrigin, RayDirection;
	FSceneView::DeprojectScreenToWorld(FVector2D(PixelX, PixelY), View->UnscaledViewRect,
		View->ViewMatrices.GetClipToWorld(), RayOrigin, RayDirection);
	OutLineStart = RayOrigin;
	OutLineEnd = RayOrigin + RayDirection * 100000000.0f;
	return true;
}

UDreamWidget* FDreamUIPrefabEditorViewportClient::GetWidgetUnderCursor(int32 PixelX, int32 PixelY, bool bRespectDesignerLock)
{
	FVector LineStart, LineEnd;
	if (!ComputePickRay(PixelX, PixelY, LineStart, LineEnd))return nullptr;
	TArray<UDreamWidget*> Widgets;
	DreamUIWidgetPicking::CollectPickableWidgets(GetWorld(), Widgets);
	int32 CycleIndex = INDEX_NONE;
	UDreamWidget* Result = DreamUIWidgetPicking::PickTopmostWidget(GetWorld(), Widgets, LineStart, LineEnd, CycleIndex);
	if (Result == nullptr)return nullptr;
	if (bRespectDesignerLock && PrefabEditorPtr.IsValid() && PrefabEditorPtr.Pin()->IsWidgetLockedForInteraction(Result))return nullptr;
	return Result;
}

UDreamWidget* FDreamUIPrefabEditorViewportClient::GetDropContainerUnderCursor(int32 PixelX, int32 PixelY)
{
	// The widget under the cursor is rarely the widget a drop belongs in: point at a Text and you
	// mean the box holding it. Resolve up to the nearest container that will actually arrange the
	// new child, and fall back to the prefab root, which is the container-less prefab's answer.
	UDreamWidget* Hit = GetWidgetUnderCursor(PixelX, PixelY);
	if (UDreamWidget* Container = DreamUIWidgetPicking::ResolveDropContainer(Hit))return Container;
	if (PrefabEditorPtr.IsValid())
	{
		if (UDreamWidget* Root = PrefabEditorPtr.Pin()->GetLoadedRootWidget())
		{
			return Root->CanAcceptAdditionalChildren() ? Root : nullptr;
		}
	}
	return nullptr;
}

bool FDreamUIPrefabEditorViewportClient::GetDropWorldPosition(int32 PixelX, int32 PixelY, UDreamWidget* ParentWidget, FVector& OutWorldPosition)
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

void FDreamUIPrefabEditorViewportClient::SetPaletteDropPreview(UDreamWidget* InWidget)
{
	PaletteDropPreviewWidget = InWidget;
	Invalidate();
}

void FDreamUIPrefabEditorViewportClient::ClearPaletteDropPreview()
{
	PaletteDropPreviewWidget.Reset();
	Invalidate();
}


// Begin override because PreviewScene is nullptr
// These implementation are copied from FEditorViewportClient
UWorld* FDreamUIPrefabEditorViewportClient::GetWorld()const
{
	return PrefabEditorPtr.Pin()->GetWorld();
}
void FDreamUIPrefabEditorViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
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
void FDreamUIPrefabEditorViewportClient::DrawPreviewLightVisualization(const FSceneView* View, FPrimitiveDrawInterface* PDI)
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
FLinearColor FDreamUIPrefabEditorViewportClient::GetBackgroundColor() const
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
bool FDreamUIPrefabEditorViewportClient::Internal_InputAxis(FViewport* InViewport, FInputDeviceId DeviceID, FKey Key, float Delta, float DeltaTime, int32 NumSamples, bool bGamepad)
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


UDreamUIPrefab* FDreamUIPrefabEditorViewportClient::GetPrefabBeingEdited()const
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

void FDreamUIPrefabEditorViewportClient::GetSelectedActorsAndComponentsForMove(TArray<AActor*>& OutActorsToMove, TArray<USceneComponent*>& OutComponentsToMove) const
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

bool FDreamUIPrefabEditorViewportClient::CanMoveActorInViewport(const AActor* InActor) const
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

void FDreamUIPrefabEditorViewportClient::TrackRightMouseMovement(int32 MouseX, int32 MouseY)
{
	if (!bRightMouseButtonDown || bRightMouseMoved)
	{
		return;
	}

	const int32 DeltaX = MouseX - RightMouseDownPosition.X;
	const int32 DeltaY = MouseY - RightMouseDownPosition.Y;
	bRightMouseMoved = FMath::Square(DeltaX) + FMath::Square(DeltaY) >= MOUSE_CLICK_DRAG_DELTA;
}

void FDreamUIPrefabEditorViewportClient::CapturedMouseMove(FViewport* InViewport, int32 InMouseX, int32 InMouseY)
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
	// The only move events delivered once the viewport has captured the mouse arrive here, which is
	// the whole of every designer drag -- exactly when the coordinate readout is worth reading. The
	// dirty flag is deliberately left alone: hover is not resolved while something is being dragged.
	HoverPixel = FIntPoint(InMouseX, InMouseY);
}

void FDreamUIPrefabEditorViewportClient::MouseEnter(FViewport* InViewport, int32 x, int32 y)
{
	bCursorInViewport = true;
	HoverPixel = FIntPoint(x, y);
	FEditorViewportClient::MouseEnter(InViewport, x, y);
}
void FDreamUIPrefabEditorViewportClient::MouseMove(FViewport* InViewport, int32 x, int32 y)
{
	TrackRightMouseMovement(x, y);
	// Click-through only makes sense while the ray stays put. CapturedMouseMove resets this too,
	// but that one fires only while the mouse is captured -- an uncaptured move between two clicks
	// used to carry the previous stack's depth over to an unrelated pixel.
	if (x != PrevMouseX || y != PrevMouseY)
	{
		IndexOfClickSelectUI = INDEX_NONE;
		PrevMouseX = x;
		PrevMouseY = y;
	}
	// Record the pixel and resolve it in Tick. Several move events can arrive per frame and each
	// resolve builds a scene view, so answering here would pay for hover several times a frame.
	HoverPixel = FIntPoint(x, y);
	bHoverPixelDirty = true;
	bCursorInViewport = true;
	FEditorViewportClient::MouseMove(InViewport, x, y);
}

void FDreamUIPrefabEditorViewportClient::UpdateHoveredWidget()
{
	if (!bHoverPixelDirty)return;
	bHoverPixelDirty = false;
	// Nothing is "under the cursor" while the cursor is dragging something; the drag owns it.
	if (bDesignerDragging || bDesignerDragPending)
	{
		HoveredWidget.Reset();
		return;
	}
	HoveredWidget = GetWidgetUnderCursor(HoverPixel.X, HoverPixel.Y);
}

void FDreamUIPrefabEditorViewportClient::DrawHoverOutline(FSceneView& View, FCanvas& Canvas) const
{
	UDreamWidget* HoverTarget = HoveredWidget.Get();
	if (!IsValid(HoverTarget))return;
	const TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	if (!Editor.IsValid())return;
	// A selected widget already has an outline; a second one over it would only say "still here".
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : Editor->GetSelectedWidgets())
	{
		if (WeakWidget.Get() == HoverTarget)return;
	}

	const FLinearColor HoverColor(0.35f, 0.75f, 1.0f, 0.9f);
	DrawWidgetScreenOutline(HoverTarget, View, Canvas, HoverColor, 1.0f);

	// The name matters more here than anywhere else in this editor: every widget is a UDreamWidget,
	// so the tree cannot tell you what you are pointing at and neither can the drawing.
	const float Left = -HoverTarget->GetPivot().X * HoverTarget->GetWidth();
	const float Top = (1.0f - HoverTarget->GetPivot().Y) * HoverTarget->GetHeight();
	FVector2D LabelPixel;
	if (!DreamWorldToPixelInFront(View, HoverTarget->GetWorldTransform().TransformPosition(FVector(0, Left, Top)), LabelPixel))return;
	LabelPixel /= Canvas.GetDPIScale();
	FCanvasTextItem Label(FVector2D(LabelPixel.X, LabelPixel.Y - 14.0f),
		FText::FromString(HoverTarget->GetDisplayName()), GEngine->GetSmallFont(), HoverColor);
	Label.EnableShadow(FLinearColor::Black);
	Canvas.DrawItem(Label);
}
void FDreamUIPrefabEditorViewportClient::MouseLeave(FViewport* InViewport)
{
	HoveredWidget.Reset();
	bHoverPixelDirty = false;
	bCursorInViewport = false;
	FEditorViewportClient::MouseLeave(InViewport);
}

void FDreamUIPrefabEditorViewportClient::TrackingStarted(const struct FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge)
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
void FDreamUIPrefabEditorViewportClient::TrackingStopped()
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

	// The gizmo writes location, rotation and scale, so key all three.
	if (MouseDeltaTracker->HasReceivedDelta())
	{
		TArray<UDreamWidget*> MovedWidgets;
		GetGizmoWidgets(MovedWidgets);
		if (!MovedWidgets.IsEmpty())
		{
			AutoKeyAnimatedTransform(MovedWidgets, true, true, true);
		}
	}

	if (bDidAnythingActuallyChange)
	{
		FScopedLevelDirtied LevelDirtyCallback;
		LevelDirtyCallback.Request();

		RedrawAllViewportsIntoThisScene();
	}
}

void FDreamUIPrefabEditorViewportClient::AbortTracking()
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

bool FDreamUIPrefabEditorViewportClient::HaveSelectedObjectsBeenChanged() const
{
	return (TrackingTransaction.TransCount > 0 || TrackingTransaction.IsActive()) && (MouseDeltaTracker->HasReceivedDelta() || MouseDeltaTracker->WasExternalMovement());
}


#undef LOCTEXT_NAMESPACE
