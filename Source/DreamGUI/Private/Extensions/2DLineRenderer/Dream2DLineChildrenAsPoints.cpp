// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/2DLineRenderer/Dream2DLineChildrenAsPoints.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIWidgetRegistry.h"

UDream2DLineChildrenAsPoints::UDream2DLineChildrenAsPoints(const FObjectInitializer &ObjectInitializer) : Super(ObjectInitializer)
{
}

void UDream2DLineChildrenAsPoints::BeginPlay()
{
    Super::BeginPlay();
}

void UDream2DLineChildrenAsPoints::OnRegister()
{
    Super::OnRegister();
    RefreshChildSubscriptions();
}

void UDream2DLineChildrenAsPoints::OnUnregister()
{
    ClearChildSubscriptions();
    Super::OnUnregister();
}

/*
 * A child moving is what this line is FOR, and until now nothing told it. OnChildPositionChanged
 * exists to mark the vertices dirty and had no callers anywhere in the plugin, so dragging a point
 * around recalculated CurrentPointArray on whatever geometry pass happened along next and threw the
 * result away, because no dirty flag was set on the pass that mattered.
 *
 * The broadcast to hang it on is the CHILD's own transform-changed event: UDreamWidget fires it
 * from MarkTransformChanged, which is precisely the moment its relative location -- the number
 * CalculatePoints reads -- becomes something else. There is no parent-side equivalent to subscribe
 * to once, so the line subscribes per child and has to keep that set honest as children come and
 * go. Hence a stored set rather than a fire-and-forget hookup.
 *
 * A visual cannot be told about attach and detach directly. UDreamWidget::OnChildAttached walks its
 * Components array, and a Visual is not in it -- it hangs off its own member -- so the two hooks
 * UDreamUIBehaviour offers for exactly this, OnWidgetChildAttached and OnWidgetChildDetached, never
 * reach here. The two moments that DO reach here are used instead: CalculatePoints, which is the
 * one function guaranteed to run whenever the line rebuilds and already walks the same array, and
 * OnChildDimensionsChanged, which is the only child-originated event a Visual receives.
 */
void UDream2DLineChildrenAsPoints::RefreshChildSubscriptions()
{
    UDreamWidget* Widget = GetWidget();
    if (Widget == nullptr)
    {
        ClearChildSubscriptions();
        return;
    }

    const TArray<UDreamWidget*>& Children = Widget->GetChildren();

    // Drop what is no longer a child of ours, and what is no longer anything at all. Backwards so
    // the removals do not renumber the entries still to be looked at.
    for (int32 Index = SubscribedChildren.Num() - 1; Index >= 0; Index--)
    {
        UDreamWidget* Subscribed = SubscribedChildren[Index].Get();
        if (Subscribed == nullptr || !Children.Contains(Subscribed))
        {
            if (IsValid(Subscribed))
            {
                Subscribed->GetTransformChangedEvent().RemoveAll(this);
            }
            SubscribedChildren.RemoveAt(Index);
        }
    }

    for (UDreamWidget* Child : Children)
    {
        if (!IsValid(Child))
        {
            continue;
        }
        const bool bAlreadySubscribed = SubscribedChildren.ContainsByPredicate(
            [Child](const TWeakObjectPtr<UDreamWidget>& InSubscribed) { return InSubscribed.Get() == Child; });
        if (bAlreadySubscribed)
        {
            continue;
        }
        // Weak rather than plain: AddWeakLambda is what makes RemoveAll(this) above able to find
        // the delegate again, and it is also what keeps a surviving child from calling into a
        // destroyed line. A plain AddLambda binds no object, so neither would hold.
        Child->GetTransformChangedEvent().AddWeakLambda(this, [this]()
        {
            OnChildPositionChanged();
        });
        SubscribedChildren.Add(Child);
    }
}

void UDream2DLineChildrenAsPoints::ClearChildSubscriptions()
{
    for (const TWeakObjectPtr<UDreamWidget>& Subscribed : SubscribedChildren)
    {
        if (Subscribed.IsValid())
        {
            Subscribed->GetTransformChangedEvent().RemoveAll(this);
        }
    }
    SubscribedChildren.Reset();
}

/*
 * This line's own rect changing moves every child anchored inside it, and a child's relative
 * location is exactly what CalculatePoints reads -- so a resize of the line is a resize of the
 * path, without a single child having been touched.
 *
 * It is this and not a per-child dimension hook because a Visual has no such hook to override:
 * UDreamVisual descends from UDreamWidgetSubObjectBehaviour, which is a separate hierarchy from
 * the UDreamUIBehaviour components that receive OnChildDimensionsChanged. The per-child news
 * arrives instead through the transform delegates RefreshChildSubscriptions binds.
 *
 * The subscription set is re-synced here as well as in CalculatePoints, because a child that
 * arrived since the last rebuild has to start being watched from somewhere.
 */
void UDream2DLineChildrenAsPoints::OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
    Super::OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
    RefreshChildSubscriptions();
    OnChildPositionChanged();
}

void UDream2DLineChildrenAsPoints::CalculatePoints()
{
    // The one pass guaranteed to run whenever the line rebuilds, and it already walks the array the
    // subscriptions are derived from -- so it is where a child that appeared or vanished since the
    // last rebuild gets picked up, and where a dead entry gets dropped.
    RefreshChildSubscriptions();

    auto& SortedItemArray = GetWidget()->GetChildren();
    int pointCount = SortedItemArray.Num();
    CurrentPointArray.Reset(pointCount);
    for (int i = 0; i < pointCount; i++)
    {
        auto Location3D = SortedItemArray[i]->GetRelativeLocation();
        CurrentPointArray.Add(FVector2D(Location3D.Y, Location3D.Z));
    }
}

void UDream2DLineChildrenAsPoints::OnChildPositionChanged()
{
    MarkVertexPositionDirty();
}

DECLARE_DREAM_GUI_VISUAL("Line2DChildren", UDream2DLineChildrenAsPoints)
