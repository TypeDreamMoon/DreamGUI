// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUISequencePreviewViewport.h"
#include "DreamUISequenceEditorToolkit.h"
#include "PrefabEditor/DreamUIPrefabEdMode.h"
#include "DreamUIWidgetPicking.h"
#include "Core/Components/DreamWidget.h"
#include "Editor.h"
#include "EditorModeManager.h"
#include "UnrealWidget.h"
#include "SceneView.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "DreamUISequencePreviewViewport"

FDreamUISequencePreviewViewportClient::FDreamUISequencePreviewViewportClient(TWeakPtr<FDreamUISequenceEditorToolkit> InToolkit, const TSharedRef<SDreamUISequencePreviewViewport>& InViewport)
	// Same base call as the prefab editor viewport: a null mode-tools pointer makes the base create
	// a private FAssetEditorModeManager; sharing the global level-editor one routes input through
	// the 5.8 Interactive Tools Framework's global InputRouter, which has no valid context in a
	// custom asset-editor viewport and null-derefs in InputKey.
	: FEditorViewportClient(nullptr, nullptr, StaticCastSharedRef<SEditorViewport>(InViewport))
	, ToolkitPtr(InToolkit)
{
	ModeTools->SetWidgetMode(UE::Widget::WM_Translate);
	Widget->SetUsesEditorModeTools(ModeTools.Get());
	// Without an active mode whose ShouldDrawWidget says yes, the transform gizmo never draws for
	// DreamWidgets (the stock answer needs actors or components selected). The prefab editor's mode
	// exists for exactly this and carries no other state, so it serves here too.
	ModeTools->SetDefaultMode(UDreamUIPrefabEdMode::EM_DreamUIPrefab);
	ModeTools->ActivateDefaultMode();
	ModeTools->SetSupportsViewportITF(false);

	SetRealtime(true);

	EngineShowFlags.Game = 0;
	EngineShowFlags.SetSnap(false);
	EngineShowFlags.DisableAdvancedFeatures();
	EngineShowFlags.SetSeparateTranslucency(true);
	EngineShowFlags.SetCompositeEditorPrimitives(true);
	EngineShowFlags.SetSelection(true);
	EngineShowFlags.SetSelectionOutline(true);

	SetViewLocation(FVector(-1000.0, 0.0, 0.0));
	SetViewRotation(FRotator::ZeroRotator);
}

UWorld* FDreamUISequencePreviewViewportClient::GetWorld() const
{
	const TSharedPtr<FDreamUISequenceEditorToolkit> Toolkit = ToolkitPtr.Pin();
	return Toolkit.IsValid() ? Toolkit->GetPreviewWorld() : nullptr;
}

void FDreamUISequencePreviewViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	// The preview world belongs to no world context, so nothing else ticks it. Without this the
	// DreamUI manager subsystem never runs and the tree neither lays out nor renders.
	if (UWorld* PreviewWorld = GetWorld())
	{
		PreviewWorld->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FDreamUISequencePreviewViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	const TSharedPtr<FDreamUISequenceEditorToolkit> Toolkit = ToolkitPtr.Pin();
	if (!Toolkit.IsValid())
	{
		return;
	}
	// Selection outline: the widget's own rect in its plane.
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : Toolkit->GetViewportSelection())
	{
		const UDreamWidget* SelectedWidget = WeakWidget.Get();
		if (SelectedWidget == nullptr)
		{
			continue;
		}
		const FTransform WorldTransform = SelectedWidget->GetWorldTransform();
		const double Left = SelectedWidget->GetLocalSpaceLeft();
		const double Right = SelectedWidget->GetLocalSpaceRight();
		const double Bottom = SelectedWidget->GetLocalSpaceBottom();
		const double Top = SelectedWidget->GetLocalSpaceTop();
		const FVector Corners[4] =
		{
			WorldTransform.TransformPosition(FVector(0, Left, Bottom)),
			WorldTransform.TransformPosition(FVector(0, Right, Bottom)),
			WorldTransform.TransformPosition(FVector(0, Right, Top)),
			WorldTransform.TransformPosition(FVector(0, Left, Top)),
		};
		const FLinearColor OutlineColor = FLinearColor(0.9f, 0.7f, 0.1f);
		for (int32 Index = 0; Index < 4; ++Index)
		{
			PDI->DrawLine(Corners[Index], Corners[(Index + 1) % 4], OutlineColor, SDPG_Foreground, 1.5f);
		}
	}
}

void FDreamUISequencePreviewViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	const FViewportClick Click(&View, this, Key, Event, HitX, HitY);
	const TSharedPtr<FDreamUISequenceEditorToolkit> Toolkit = ToolkitPtr.Pin();
	if (!Toolkit.IsValid())
	{
		return;
	}

	FVector RayOrigin, RayDirection;
	View.DeprojectScreenToWorld(FVector2D(HitX, HitY), View.UnscaledViewRect, View.ViewMatrices.GetInvViewProjectionMatrix(), RayOrigin, RayDirection);
	const FVector LineStart = RayOrigin;
	const FVector LineEnd = RayOrigin + RayDirection * 100000000.0;

	// Clicking the same pixel repeatedly walks down through overlapping widgets; a moved cursor
	// restarts from the top. Same scheme as the prefab editor.
	const FIntPoint ClickPixel((int32)HitX, (int32)HitY);
	if (ClickPixel != LastClickPixel)
	{
		ClickCycleIndex = INDEX_NONE;
	}
	LastClickPixel = ClickPixel;

	TArray<UDreamWidget*> PickableWidgets;
	DreamUIWidgetPicking::CollectPickableWidgets(GetWorld(), PickableWidgets);
	UDreamWidget* HitWidget = DreamUIWidgetPicking::PickTopmostWidget(GetWorld(), PickableWidgets, LineStart, LineEnd, ClickCycleIndex);

	if (Click.GetKey() == EKeys::LeftMouseButton)
	{
		Toolkit->SelectWidgetFromViewport(HitWidget, Click.IsControlDown());
		return;
	}

	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

UE::Widget::EWidgetMode FDreamUISequencePreviewViewportClient::GetWidgetMode() const
{
	TArray<UDreamWidget*> Widgets;
	GetGizmoWidgets(Widgets);
	if (Widgets.IsEmpty())
	{
		return UE::Widget::WM_None;
	}
	return FEditorViewportClient::GetWidgetMode();
}

FVector FDreamUISequencePreviewViewportClient::GetWidgetLocation() const
{
	TArray<UDreamWidget*> Widgets;
	GetGizmoWidgets(Widgets);
	FVector Sum = FVector::ZeroVector;
	for (const UDreamWidget* SelectedWidget : Widgets)
	{
		Sum += SelectedWidget->GetWorldLocation();
	}
	return Widgets.IsEmpty() ? FVector::ZeroVector : Sum / Widgets.Num();
}

FMatrix FDreamUISequencePreviewViewportClient::GetWidgetCoordSystem() const
{
	TArray<UDreamWidget*> Widgets;
	GetGizmoWidgets(Widgets);
	if (Widgets.IsEmpty())
	{
		return FMatrix::Identity;
	}
	return FQuatRotationMatrix(Widgets[0]->GetWorldRotation());
}

bool FDreamUISequencePreviewViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type InCurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (InCurrentAxis != EAxisList::None)
	{
		TArray<UDreamWidget*> Widgets;
		GetGizmoWidgets(Widgets);
		if (!Widgets.IsEmpty())
		{
			ApplyDeltaToWidgets(Drag, Rot, Scale);
			return true;
		}
	}
	return FEditorViewportClient::InputWidgetDelta(InViewport, InCurrentAxis, Drag, Rot, Scale);
}

void FDreamUISequencePreviewViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDragging, bool bNudge)
{
	TArray<UDreamWidget*> Widgets;
	GetGizmoWidgets(Widgets);
	if (bIsDragging && !Widgets.IsEmpty() && !bIsTrackingDrag)
	{
		bIsTrackingDrag = true;
		bDragTouchedRenderTranslation = false;
		bDragTouchedRelativeLocation = false;
		bDragTouchedRotation = false;
		bDragTouchedScale = false;
		GEditor->BeginTransaction(LOCTEXT("MoveWidgetForAnimation", "Transform Widget (Animation)"));
	}
	FEditorViewportClient::TrackingStarted(InInputState, bIsDragging, bNudge);
}

void FDreamUISequencePreviewViewportClient::TrackingStopped()
{
	if (bIsTrackingDrag)
	{
		bIsTrackingDrag = false;
		GEditor->EndTransaction();

		TArray<UDreamWidget*> Widgets;
		GetGizmoWidgets(Widgets);
		if (const TSharedPtr<FDreamUISequenceEditorToolkit> Toolkit = ToolkitPtr.Pin(); Toolkit.IsValid() && !Widgets.IsEmpty())
		{
			Toolkit->KeyTransformProperties(Widgets, bDragTouchedRenderTranslation, bDragTouchedRelativeLocation, bDragTouchedRotation, bDragTouchedScale);
		}
	}
	FEditorViewportClient::TrackingStopped();
}

void FDreamUISequencePreviewViewportClient::GetGizmoWidgets(TArray<UDreamWidget*>& OutWidgets) const
{
	const TSharedPtr<FDreamUISequenceEditorToolkit> Toolkit = ToolkitPtr.Pin();
	if (!Toolkit.IsValid())
	{
		return;
	}
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : Toolkit->GetViewportSelection())
	{
		if (UDreamWidget* SelectedWidget = WeakWidget.Get())
		{
			OutWidgets.Add(SelectedWidget);
		}
	}
}

void FDreamUISequencePreviewViewportClient::ApplyDeltaToWidgets(const FVector& Drag, const FRotator& Rot, const FVector& Scale)
{
	TArray<UDreamWidget*> Widgets;
	GetGizmoWidgets(Widgets);
	if (Widgets.IsEmpty())
	{
		return;
	}
	const bool bHasDrag = !Drag.IsNearlyZero();
	const bool bHasRot = !Rot.IsNearlyZero();
	const bool bHasScale = !Scale.IsNearlyZero();
	if (!bHasDrag && !bHasRot && !bHasScale)
	{
		return;
	}

	for (UDreamWidget* TargetWidget : Widgets)
	{
		TargetWidget->Modify();
		if (bHasDrag)
		{
			const UDreamWidget* ParentWidget = TargetWidget->GetParent();
			const bool bLayoutOwnsPosition = ParentWidget != nullptr && ParentWidget->GetLayoutContainer() != nullptr;
			if (bLayoutOwnsPosition)
			{
				// The layout re-arranges RelativeLocation every tick, so a drag there would be
				// snapped straight back (and a key on it would play back dead). Render translation
				// is the position channel the layout leaves alone -- the same rule the level
				// animation playback follows.
				const FVector LocalDrag = TargetWidget->GetWorldTransform().InverseTransformVector(Drag);
				TargetWidget->SetRenderTranslation(TargetWidget->GetRenderTranslation() + LocalDrag);
				bDragTouchedRenderTranslation = true;
			}
			else
			{
				TargetWidget->SetWorldLocation(TargetWidget->GetWorldLocation() + Drag);
				bDragTouchedRelativeLocation = true;
			}
		}
		if (bHasRot)
		{
			TargetWidget->SetRelativeRotation(Rot.Quaternion() * TargetWidget->GetRelativeRotation());
			bDragTouchedRotation = true;
		}
		if (bHasScale)
		{
			TargetWidget->SetRelativeScale(TargetWidget->GetRelativeScale() + Scale);
			bDragTouchedScale = true;
		}
	}
}

void FDreamUISequencePreviewViewportClient::FocusOnPreview()
{
	const TSharedPtr<FDreamUISequenceEditorToolkit> Toolkit = ToolkitPtr.Pin();
	const UDreamWidget* Root = Toolkit.IsValid() ? Toolkit->GetPreviewRootWidget() : nullptr;
	if (Root == nullptr)
	{
		return;
	}
	// Same pose the prefab editor opens with: on the -X side of the UI plane, looking straight at it.
	const FVector Center = Root->GetWorldLocation();
	const double HalfWidth = FMath::Abs(Root->GetLocalSpaceRight() - Root->GetLocalSpaceLeft()) * 0.5;
	const double HalfHeight = FMath::Abs(Root->GetLocalSpaceTop() - Root->GetLocalSpaceBottom()) * 0.5;
	const double Radius = FMath::Max3(HalfWidth, HalfHeight, 50.0);
	SetViewLocation(Center + FVector(-Radius * 2.4, 0.0, 0.0));
	SetViewRotation(FRotator::ZeroRotator);
	SetLookAtLocation(Center);
	Invalidate();
}

void SDreamUISequencePreviewViewport::Construct(const FArguments& InArgs, TSharedPtr<FDreamUISequenceEditorToolkit> InToolkit)
{
	ToolkitPtr = InToolkit;
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SDreamUISequencePreviewViewport::MakeEditorViewportClient()
{
	PreviewClient = MakeShareable(new FDreamUISequencePreviewViewportClient(ToolkitPtr, SharedThis(this)));
	PreviewClient->bSetListenerPosition = false;
	PreviewClient->SetRealtime(true);
	return PreviewClient.ToSharedRef();
}

void SDreamUISequencePreviewViewport::OnFocusViewportToSelection()
{
	if (PreviewClient.IsValid())
	{
		PreviewClient->FocusOnPreview();
	}
}

#undef LOCTEXT_NAMESPACE
