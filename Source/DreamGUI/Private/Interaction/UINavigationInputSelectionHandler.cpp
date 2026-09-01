// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UINavigationInputSelectionHandler.h"
#include "DreamTweenBPLibrary.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"

UUINavigationInputSelectionHandler::UUINavigationInputSelectionHandler()
{
}

UDreamTweener* UUINavigationInputSelectionHandler::FadeCursorTo(UDreamWidget* InWidget, float InOpacity)
{
	UDreamTweener* Tweener = InWidget->RenderOpacityTo(InOpacity, AnimDuration, 0, EDreamTweenEase::Linear);
	if (Tweener != nullptr)
	{
		TweenerCollection.Add(Tweener);
		return Tweener;
	}
	// No tween to be had, so the animation is skipped and its DESTINATION is applied instead. The
	// fade exists so the cursor does not pop; the opacity it fades to is the actual outcome, and
	// dropping the whole statement on the floor would leave the cursor sitting at whatever the last
	// selection left it at -- invisible over the widget it is meant to be marking, or fully opaque
	// over nothing. Nothing is recorded in TweenerCollection either: a null entry is not an
	// animation, and storing it would make "is anything running" unanswerable by counting.
	InWidget->SetRenderOpacity(InOpacity);
	return nullptr;
}

void UUINavigationInputSelectionHandler::SelectWidget(UDreamWidget* InSelected)
{
	// UDreamUIBehaviour settled this in its constructor and the answer cannot change afterwards --
	// a class is compiled from a Blueprint or it is not. Recomputing the same expression here meant
	// walking the class flags on every selection change, and left a second copy of the rule that
	// could drift away from the one every other behaviour callback forks on.
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveSelectWidget(InSelected);
		return;
	}
	auto Widget = GetWidget();
	if (!Widget)return;

	for (auto& Tweener : TweenerCollection)
	{
		UDreamTweenBPLibrary::KillIfIsTweening(this, Tweener.Get());
	}
	TweenerCollection.Reset();

	auto PrevSelected = CurrentSelected;
	CurrentSelected = InSelected;
	if (InSelected != nullptr && PrevSelected.IsValid())
	{
		Widget->SetParent(InSelected, true);
		auto Pos2D = InSelected->GetLocalSpaceCenter();
		auto Pos3D = FVector(0, Pos2D.X, Pos2D.Y);
		// auto Tweener = UDreamTweenBPLibrary::LocalPositionTo(Widget, Pos3D, AnimDuration, 0, EDreamTweenEase::InOutSine);
		// TweenerCollection.Add(Tweener);
		// Tweener = Widget->SizeDeltaTo(InSelected->GetSize(), AnimDuration, 0, EDreamTweenEase::InOutSine);
		// TweenerCollection.Add(Tweener);
		// Tweener = UDreamTweenBPLibrary::LocalRotationQuaternionTo(Widget, FQuat::Identity, AnimDuration, 0, EDreamTweenEase::InOutSine);
		// TweenerCollection.Add(Tweener);

		if (ThisCanvas.IsValid())
		{
			ThisCanvas->SetSortOrderToHighestOfHierarchy(false);
		}
	}
	else if (InSelected != nullptr)
	{
		FadeCursorTo(Widget, 1.0f);
		Widget->SetParent(InSelected, true);
		auto Pos2D = InSelected->GetLocalSpaceCenter();
		auto Pos3D = FVector(0, Pos2D.X, Pos2D.Y);
		Widget->SetRelativeLocation(Pos3D);
		Widget->SetSizeDelta(InSelected->GetSize());
		Widget->SetRelativeRotation(FQuat::Identity);

		if (ThisCanvas.IsValid())
		{
			ThisCanvas->SetSortOrderToHighestOfHierarchy(false);
		}
	}
	else if (PrevSelected.IsValid())
	{
		FadeCursorTo(Widget, 0.0f);
	}
}

void UUINavigationInputSelectionHandler::SelectNone()
{
	// See SelectWidget on why this reads the cached flag rather than recomputing the expression.
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveSelectNone();
		return;
	}
	auto Widget = GetWidget();
	if (!Widget)return;
	if (!CurrentSelected.IsValid())return;

	for (auto& Tweener : TweenerCollection)
	{
		UDreamTweenBPLibrary::KillIfIsTweening(this, Tweener.Get());
	}
	TweenerCollection.Reset();

	// Cleared before the fade rather than after it, because the fallback path below tears the widget
	// down synchronously, and tearing a widget down runs this behaviour's own EndPlay on the way
	// through. Bookkeeping written after that point is bookkeeping written on a behaviour that has
	// already been told it is finished.
	CurrentSelected = nullptr;

	// The cursor is being retired, not merely faded: the destruction is the point and the fade is
	// how it leaves politely. So when there is no tween to hang the destruction off, the destruction
	// still has to happen -- deferring it to an OnComplete that can never fire would strand a fully
	// opaque cursor on the last widget it marked, for the rest of the session.
	if (UDreamTweener* Tweener = FadeCursorTo(Widget, 0.0f))
	{
		// The completion delegate keeps nothing alive, and a quarter of a second is ample time for
		// the screen this cursor belongs to to be torn down underneath it. A weak pointer to the
		// widget is therefore the only handle worth capturing: the alternative, reaching back
		// through this behaviour for its widget, is a dereference of whatever GetWidget answers on
		// an object that may itself be gone.
		const TWeakObjectPtr<UDreamWidget> FadingWidget = Widget;
		Tweener->OnComplete([FadingWidget]()
		{
			if (UDreamWidget* CompletedWidget = FadingWidget.Get())
			{
				CompletedWidget->DestroyWidget();
			}
		});
	}
	else
	{
		Widget->DestroyWidget();
	}
}
