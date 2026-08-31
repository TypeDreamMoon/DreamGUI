// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIPopupLayer.h"

#include "Core/DreamScreenUISubsystem.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"
#include "Engine/World.h"

UDreamUIPopupLayer* UDreamUIPopupLayer::Get(const UObject* InWorldContext)
{
	const UWorld* World = IsValid(InWorldContext) ? InWorldContext->GetWorld() : nullptr;
	return World != nullptr ? World->GetSubsystem<UDreamUIPopupLayer>() : nullptr;
}

bool UDreamUIPopupLayer::Elevate(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	if (ElevatedHomes.Contains(InWidget))
	{
		// Already up. The owner opening twice without closing is a state question the owner settled;
		// re-recording a home here would overwrite the real one with the screen root.
		return true;
	}
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(InWidget->GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!IsValid(ScreenRoot))
	{
		return false;
	}
	UDreamWidget* Home = InWidget->GetParent();
	if (!IsValid(Home) || Home == ScreenRoot)
	{
		return Home == ScreenRoot;
	}
	// The move keeps the ON-SCREEN placement, and TrySetParent's keep-world flag cannot do that for
	// a laid-out widget: it preserves RelativeLocation, which the next layout pass re-derives from
	// the anchors -- and the anchors the owner authored (UUIDropdown::Show anchors the list against
	// its FACE) mean something entirely different measured against the screen root. So the anchors
	// are collapsed to a point, the size the layout had computed is pinned as authored size, and the
	// anchored position is the old world position expressed in the root's plane -- Y across, Z up,
	// the same axes UUISlider reads pointer positions in.
	const FTransform OldWorld = InWidget->GetLayoutWorldTransform();
	const float Width = InWidget->GetWidth();
	const float Height = InWidget->GetHeight();
	const FVector2D Pivot = InWidget->GetPivot();
	if (!InWidget->TrySetParent(ScreenRoot, /*InKeepWorldPosition*/false))
	{
		return false;
	}
	InWidget->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
	InWidget->SetWidth(Width);
	InWidget->SetHeight(Height);
	InWidget->SetPivot(Pivot);
	const FVector LocalInRoot = ScreenRoot->GetLayoutWorldTransform().InverseTransformPosition(OldWorld.GetLocation());
	InWidget->SetAnchoredPosition(FVector2D(LocalInRoot.Y, LocalInRoot.Z));
	ElevatedHomes.Add(InWidget, Home);
	return true;
}

void UDreamUIPopupLayer::Restore(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	TWeakObjectPtr<UDreamWidget> Home;
	if (!ElevatedHomes.RemoveAndCopyValue(InWidget, Home))
	{
		return;
	}
	if (UDreamWidget* HomeWidget = Home.Get())
	{
		// No position juggling on the way home: the widget is hidden the moment it lands, and the
		// owner's next open rewrites anchors, size and position from scratch anyway.
		InWidget->TrySetParent(HomeWidget, /*InKeepWorldPosition*/false);
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s' has nowhere to return to; its owner is gone."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InWidget->GetDisplayName());
	}
}
