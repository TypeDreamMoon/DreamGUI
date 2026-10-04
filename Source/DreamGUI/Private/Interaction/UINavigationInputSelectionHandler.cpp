// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UINavigationInputSelectionHandler.h"
#include "DreamTweenBPLibrary.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Engine/World.h"

namespace UINavigationInputSelectionHandlerLocal
{
	/**
	 * The ring of each root canvas no presenter hosts, by the canvas's widget. Weak both ways: a ring goes with the tree it
	 * hangs in -- moved under the widget it marks, it is destroyed with that widget -- and the next ask makes another; an
	 * entry whose canvas or ring is gone is cleared by the next ask that makes one. Game thread.
	 */
	TMap<TWeakObjectPtr<UDreamWidget>, TWeakObjectPtr<UUINavigationInputSelectionHandler>> GCanvasFocusRings;

	/** Where a ring for InWidget's screen hangs: its root canvas's widget, or null while no canvas draws it. */
	UDreamWidget* FindRingHost(const UDreamWidget* InWidget)
	{
		const UDreamCanvas* RootCanvas = InWidget->GetRootCanvas();
		return RootCanvas != nullptr ? RootCanvas->GetWidget() : nullptr;
	}

	void ForgetGoneRings()
	{
		for (auto It = GCanvasFocusRings.CreateIterator(); It; ++It)
		{
			if (!It->Key.IsValid() || !It->Value.IsValid())
			{
				It.RemoveCurrent();
			}
		}
	}

	/**
	 * Make the picture of a ring the handler sizes follow that size. The handler sizes the ring's root to the control it
	 * marks; a picture placed on the root's centre at a fixed size -- the plugin's own ring is a 100x100 frame drawn that
	 * way -- stayed 100x100 over a full-width row and a small icon alike. Each child of the root that covers it (anchored
	 * to its centre, at least its size) is anchored to stretch with it instead, keeping the margin it was authored with: a
	 * frame drawn a few pixels outside the root stays outside by the same few pixels. A child smaller than the root (a dot
	 * on the centre) or anchored elsewhere (a corner bracket) already places itself by the root's size, and keeps what it
	 * was authored with.
	 */
	void FitRingPicture(UDreamWidget* InRingWidget)
	{
		const float RootWidth = InRingWidget->GetWidth();
		const float RootHeight = InRingWidget->GetHeight();
		if (RootWidth <= UE_KINDA_SMALL_NUMBER || RootHeight <= UE_KINDA_SMALL_NUMBER)
		{
			return;
		}
		const FVector2D Centre(0.5, 0.5);
		constexpr float Tolerance = 0.5f;
		for (UDreamWidget* Child : InRingWidget->GetChildren())
		{
			if (!IsValid(Child) || !Child->GetAnchorMin().Equals(Centre, UE_KINDA_SMALL_NUMBER)
				|| !Child->GetAnchorMax().Equals(Centre, UE_KINDA_SMALL_NUMBER))
			{
				continue;
			}
			if (Child->GetWidth() + Tolerance < RootWidth || Child->GetHeight() + Tolerance < RootHeight)
			{
				continue;
			}
			// Same rectangle, new anchors: the size it keeps becomes a size relative to the root's.
			Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D::UnitVector, true, true);
		}
	}
}

UUINavigationInputSelectionHandler::UUINavigationInputSelectionHandler()
{
}

UUINavigationInputSelectionHandler* UUINavigationInputSelectionHandler::FindFor(const UDreamWidget* InWidget)
{
	using namespace UINavigationInputSelectionHandlerLocal;
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	if (const UDreamWidgetPresenterComponentBase* Presenter = Cast<UDreamWidgetPresenterComponentBase>(InWidget->GetAttachedRootSceneComponent()))
	{
		return Presenter->FindNavigationSelection();
	}
	UDreamWidget* Host = FindRingHost(InWidget);
	if (Host == nullptr)
	{
		return nullptr;
	}
	const TWeakObjectPtr<UUINavigationInputSelectionHandler>* Found = GCanvasFocusRings.Find(TWeakObjectPtr<UDreamWidget>(Host));
	return Found != nullptr ? Found->Get() : nullptr;
}

UUINavigationInputSelectionHandler* UUINavigationInputSelectionHandler::FindOrCreateFor(UDreamWidget* InWidget)
{
	using namespace UINavigationInputSelectionHandlerLocal;
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	// A presenter keeps its own ring: it is what knows which ring class its owner configured.
	if (UDreamWidgetPresenterComponentBase* Presenter = Cast<UDreamWidgetPresenterComponentBase>(InWidget->GetAttachedRootSceneComponent()))
	{
		return Presenter->GetNavigationSelection();
	}
	// Every other screen -- the pages the screen subsystem shows above all -- had no ring at all, since only a presenter
	// ever made one. One per root canvas, from the project's class, made with no parent -- the manager's pool of free
	// roots holds it -- since the selection that asks for it may come from inside a walk over the canvas's tree (a scope
	// focusing as its screen wakes) that a new child must not land in; SelectWidget moves it onto the widget it marks.
	UDreamWidget* Host = FindRingHost(InWidget);
	UWorld* World = InWidget->GetWorld();
	if (Host == nullptr || World == nullptr || !World->IsGameWorld())
	{
		return nullptr;
	}
	if (UUINavigationInputSelectionHandler* Existing = FindFor(InWidget))
	{
		return Existing;
	}
	ForgetGoneRings();
	// No ring class is a project with no ring, not a mistake to report at every focus change.
	if (UDreamGUISettings::Get()->NavigationSelectionClass.IsNull())
	{
		return nullptr;
	}
	const TSubclassOf<UDreamUserWidget> SelectionClass = UDreamGUISettings::LoadSettingClass(
		UDreamGUISettings::Get()->NavigationSelectionClass, TEXT("NavigationSelectionClass"));
	if (SelectionClass == nullptr)
	{
		return nullptr;
	}
	UDreamUserWidget* RingWidget = CreateDreamWidget(World, SelectionClass, nullptr);
	UUINavigationInputSelectionHandler* Ring = MakeRing(RingWidget);
	if (Ring == nullptr)
	{
		if (RingWidget != nullptr)
		{
			RingWidget->DestroyWidget();
		}
		return nullptr;
	}
	GCanvasFocusRings.Add(TWeakObjectPtr<UDreamWidget>(Host), TWeakObjectPtr<UUINavigationInputSelectionHandler>(Ring));
	return Ring;
}

UUINavigationInputSelectionHandler* UUINavigationInputSelectionHandler::MakeRing(UDreamWidget* InRingWidget)
{
	using namespace UINavigationInputSelectionHandlerLocal;
	if (!IsValid(InRingWidget))
	{
		return nullptr;
	}
	// The class's own handler when it has one -- a Blueprint ring drives itself. The plugin's class has none: it is the
	// picture alone, and every ring made from it used to be thrown away (or, on a presenter, left null), so the ring never
	// showed anywhere. The handler added here moves, sizes and fades the root the picture is drawn on.
	UUINavigationInputSelectionHandler* Ring = InRingWidget->GetComponent<UUINavigationInputSelectionHandler>();
	if (Ring == nullptr)
	{
		Ring = InRingWidget->AddComponent<UUINavigationInputSelectionHandler>();
	}
	if (Ring != nullptr)
	{
		MakeRingInert(InRingWidget);
		// A Blueprint ring places its own picture; this handler sizes the root of every other, so the picture follows it.
		if (!Ring->bCanExecuteBlueprintEvent)
		{
			FitRingPicture(InRingWidget);
		}
	}
	return Ring;
}

void UUINavigationInputSelectionHandler::MakeRingInert(UDreamWidget* InRingWidget)
{
	if (!IsValid(InRingWidget))
	{
		return;
	}
	// The ring is parented under the control it marks and sized over it: hit, it would take the click meant for that
	// control; laid out, a control with a layout container would place it among its own children.
	InRingWidget->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
	InRingWidget->SetIgnoreLayout(true);
	InRingWidget->SetIsTabStop(false);
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

void UUINavigationInputSelectionHandler::MoveCursorTo(UDreamWidget* InWidget, const FVector& InLocation, const FVector2D& InSize)
{
	if (UDreamTweener* PositionTween = InWidget->LocalPositionTo(InLocation, AnimDuration, 0, EDreamTweenEase::InOutSine))
	{
		TweenerCollection.Add(PositionTween);
	}
	else
	{
		InWidget->SetRelativeLocation(InLocation);
	}
	if (UDreamTweener* SizeTween = InWidget->SizeDeltaTo(InSize, AnimDuration, 0, EDreamTweenEase::InOutSine))
	{
		TweenerCollection.Add(SizeTween);
	}
	else
	{
		InWidget->SetSizeDelta(InSize);
	}
	// The rotation is unwound rather than animated for its own sake: the cursor inherits whatever
	// rotation its new parent carries, and Identity here is what keeps a highlight square on a
	// rotated button instead of doubling the button's tilt.
	if (UDreamTweener* RotationTween = InWidget->LocalRotationQuaternionTo(FQuat::Identity, AnimDuration, 0, EDreamTweenEase::InOutSine))
	{
		TweenerCollection.Add(RotationTween);
	}
	else
	{
		InWidget->SetRelativeRotation(FQuat::Identity);
	}
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
		// The fade-in of a ring that has only just appeared was among the tweens killed above: a step taken inside its
		// quarter second -- or the same focus arriving twice, as a select and then a navigation enter -- left the ring at
		// whatever opacity the fade had reached, which on its first frame is none at all.
		if (Widget->GetRenderOpacity() < 1.0f)
		{
			FadeCursorTo(Widget, 1.0f);
		}
		Widget->SetParent(InSelected, true);
		const FVector2D Pos2D = InSelected->GetLocalSpaceCenter();
		const FVector Pos3D(0, Pos2D.X, Pos2D.Y);
		MoveCursorTo(Widget, Pos3D, InSelected->GetSize());

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
