// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDetailTrace.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIGoneCount.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/World.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/PlayerController.h"
// FLayoutLocalization, for the Culture flow-direction preference. SlateCore is already a public
// dependency; this is the one header of it that answers "which way does the running culture read".
#include "Layout/FlowDirection.h"
#if WITH_EDITOR
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#endif

void UDreamWidget::MarkAllDirtyRecursive()
{
	MarkAllDirty();
	
	for (auto& uiChild : Children)
	{
		if (IsValid(uiChild))
		{
			uiChild->MarkAllDirtyRecursive();
		}
	}
}

void UDreamWidget::MarkAllDirty()
{
	bFlattenHierarchyIndexDirty = true;
	bClipDirty = true;
	MarkWorldRectBoundsDirty();

	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;
	
	if (IsValid(Visual))
	{
		Visual->MarkAllDirty();
	}
}

void UDreamWidget::MarkRenderModeChangeRecursive(UDreamCanvas* Canvas)
{
	// The old and new render modes used to be carried through here as parameters and were never read by
	// anything: the whole subtree is dirtied wholesale either way, because UE and DreamGUI mesh data are
	// not compatible in either direction. Dropped rather than left as a promise the signature cannot keep.
	if (this->RenderCanvas == Canvas)
	{
		MarkAllDirty();
		for (auto& uiChild : Children)
		{
			if (IsValid(uiChild))
			{
				uiChild->MarkRenderModeChangeRecursive(Canvas);
			}
		}
	}
}

void UDreamWidget::RegisterRenderCanvas(UDreamCanvas* InRenderCanvas)
{
	bIsCanvasWidget = true;
	UDreamCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		// Including the parent itself: GetComponentInParent starts ABOVE the widget it is asked on unless
		// told otherwise, and a canvas on the parent is the nearest one there is.
		ParentCanvas = ParentWidget->GetComponentInParent<UDreamCanvas>(/*bIncludeSelf*/ true);//@todo: replace with Canvas's ParentCanvas?
	}
	if (RenderCanvas != InRenderCanvas)
	{
		SetRenderCanvas(InRenderCanvas);
	}
	InRenderCanvas->SetParentCanvas(ParentCanvas);
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(InRenderCanvas);
		}
	}
}
void UDreamWidget::RefreshRenderCanvasFromParentChain()
{
	RenewRenderCanvasRecursive(GetComponentInParent<UDreamCanvas>(true));
}

void UDreamWidget::RefreshInheritedStateFromParentChain()
{
	// The same four walks OnRegister runs for a hierarchy root, plus the canvas. Each one reads the
	// parent's cached value and pushes the result down, and each only fires its changed-event when a
	// value actually moves -- so running this on a subtree that was already correct costs the walk
	// and nothing else.
	CalculateWidgetActive_Recursive();
	CalculateVisibility_Recursive();
	CalculateRaycastable_Recursive();
	CalculateInteractable_Recursive();
	RefreshRenderCanvasFromParentChain();

	// And the raycast's own ordering, which is the sixth thing a subtree inherits from the chain it
	// was just attached to and the one this function was missing.
	//
	// FlattenHierarchyIndex is born -1 and only recalculated when the HIERARCHY ROOT is marked
	// dirty; the mark rides the attachment event, which SetParentBeforeRegister deliberately does
	// not raise. So a subtree assembled that way keeps -1 on every node -- and unlike the render
	// canvas (patched just above, for this same hole), nothing downstream self-heals it.
	//
	// It still DRAWS, which is what makes the symptom so misleading. What it loses is the hit sort:
	// UDreamBaseRaycaster orders hits within a canvas by flatten index, descending, so -1 loses to
	// every widget that has one. Measured on a modal dialog whose two buttons could not be clicked
	// at all -- scrim, panel and both buttons all read -1 while the page behind them ran 0..215, so
	// every click was awarded to the page. Re-attaching the layer by hand in a live session moved
	// them to 216..228 and the dialog answered the pointer again.
	MarkFlattenHierarchyIndexDirty();

	// And where the subtree IS: the seventh thing it inherits, and the one every other attach road
	// already re-derives. TrySetParentInternal recomputes the world transform on each attach and
	// detach, and the editor's full refresh (EnsureDataForRebuild, which the designer runs after every
	// preview rebuild) ends with this same call -- which is why no designer preview ever showed this.
	//
	// The world transform is a cache of GetRenderLocalTransform() composed with the parent's, composed
	// again only when a change has marked it stale, and SetParentBeforeRegister, being the cheap attach,
	// composes nothing. A subtree hung that way keeps whatever it was last composed against: for anything
	// CreateDreamWidget builds, its own user widget before that had a parent -- the world origin. Nothing
	// afterwards is bound to correct it, because the setters and the layout write-back only mark when the
	// value they write differs from the one held. A control left at its default position never moves, so
	// it is never marked: SetAnchoredPosition((0, 0)) on a new control returns early, and so does the
	// CalculateTransformFromAnchor a layout pass runs, which derives the relative location the widget
	// already has. The early returns are right; the state they protect was never established.
	//
	// A screen-space root sits at the origin, so there the stale value and the right one are the same
	// transform and nothing showed. Under a world-space panel or a render-target canvas they are not:
	// a button added at its default place on a panel standing in the level stayed at the world origin
	// -- drawn there, traced there, projected there -- while the same button moved sideways appeared
	// on the panel, because the move is what finally recomputed it.
	//
	// Last, for two reasons. The render canvas was re-derived above, so the canvas update the
	// recompute asks for lands on the canvas this subtree now draws into. And RegisterDreamWidgetHierarchy
	// calls this after every OnRegister in the subtree, which is where the render-transform, perspective
	// and shear bits the composition reads were refreshed from serialized data; composed any earlier, a
	// saved render transform would be left out of it.
	//
	// Unlike the four walks above, this announces itself whether or not the value moved -- every widget
	// the mark reaches is announced by the flush that follows, so every behaviour bound in the subtree
	// hears one transform change (queued until its Awake in a game world). An attach through TrySetParent
	// opens with this same mark and says the same thing, so a behaviour now hears it whichever of the two
	// doors its widget came in by.
	CalculateObjectToWorldTransform(true);
}

void UDreamWidget::RenewRenderCanvasRecursive(UDreamCanvas* InParentRenderCanvas)
{
	// The self-canvas branch below used to be unreachable: a bare `ThisRenderCanvas = nullptr` sat
	// between the lookup and the test. It is what is left of an `&& !ThisRenderCanvas->IsRegistered()`
	// guard whose condition was dropped during the port -- UDreamUIBehaviour is a plain UObject and has
	// no such state to test, so the guard has no meaning here and the assignment is simply wrong.
	// With it in place a nested canvas kept none of its subtree: RegisterRenderCanvas,
	// UnregisterRenderCanvas and RefreshRenderCanvasFromParentChain all repointed the whole thing at
	// the ancestor canvas, losing the child canvas's own sorting, batching and clipping.
	auto ThisRenderCanvas = this->GetComponent<UDreamCanvas>();
	if (ThisRenderCanvas != nullptr)
	{
		if (InParentRenderCanvas != ThisRenderCanvas)
		{
			ThisRenderCanvas->SetParentCanvas(InParentRenderCanvas);//set parent Canvas for this actor's Canvas
		}
		return;
	}

	if (RenderCanvas != InParentRenderCanvas)//if attach to new Canvas, need to remove from old and add to new
	{
		SetRenderCanvas(InParentRenderCanvas);
	}

	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(InParentRenderCanvas);
		}
	}
}

void UDreamWidget::UnregisterRenderCanvas()
{
	bIsCanvasWidget = false;
	UDreamCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		// Including the parent itself: GetComponentInParent starts ABOVE the widget it is asked on unless
		// told otherwise, and a canvas on the parent is the nearest one there is.
		ParentCanvas = ParentWidget->GetComponentInParent<UDreamCanvas>(/*bIncludeSelf*/ true);//@todo: replace with Canvas's ParentCanvas?
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->SetParentCanvas(nullptr);
	}
	if (RenderCanvas != ParentCanvas)
	{
		SetRenderCanvas(ParentCanvas);
	}
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(ParentCanvas);
		}
	}
}

void UDreamWidget::UpdateClip(UDreamUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FDreamUIClipData>>& ClipDataList)
{
	if (!bClipDirty)return;
	DREAMUI_DETAIL_SCOPE(DreamUI_UpdateClip);
	bClipDirty = false;
	
	if (bNeedRecreateClip && ClipData.IsValid())
	{
		if (ClipData.Pin()->GetWidget() == this)//remove old clip-data
		{
			ClipDataList.Remove(ClipData.Pin());
		}
		else
		{
			ClipData = nullptr;//will create new
		}
	}
	bNeedRecreateClip = false;
	
	TSharedPtr<FDreamUIClipData> ParentClip = nullptr;
	if (Parent.IsValid())
	{
		ParentClip = Parent->ClipData.Pin();
	}
	switch (GetClipping())
	{
	case EDreamWidgetClipping::Inherit:
		this->ClipData = ParentClip;
		break;
	case EDreamWidgetClipping::ClipToBounds:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FDreamUIClipData>(ParentClip, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case EDreamWidgetClipping::ClipToBoundsWithoutIntersecting:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FDreamUIClipData>(nullptr, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case EDreamWidgetClipping::Disabled:
		this->ClipData = nullptr;
		break;
	}
	if (Visual)
	{
		Visual->CheckClipDataStartPosition();
	}
}

void UDreamWidget::UpdateVisual() const
{
	if (IsValid(Visual))
	{
		Visual->UpdateGeometry();
	}
}

void UDreamWidget::SetRenderCanvas(UDreamCanvas* InNewCanvas)
{
	auto OldRenderCanvas = RenderCanvas;
	RenderCanvas = InNewCanvas;
	// Kept as it was handed in, alive (RenderCanvasRaw).
	RenderCanvasRawGone = DreamUIGone::Read();
	RenderCanvasRaw = InNewCanvas;
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)//delete old clip-data
	{
		if (OldRenderCanvas.IsValid())
		{
			OldRenderCanvas->RemoveClipData(ClipData.Pin());//remove it from old canvas
		}
	}
	if (OldRenderCanvas.IsValid())
	{
		OldRenderCanvas->RemoveDreamWidget(this);
		if (IsValid(Visual))
		{
			OldRenderCanvas->MarkVisualWillChange(Visual);
			OldRenderCanvas->UnregisterVisual(Visual);
		}
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->AddDreamWidget(this);
		bClipDirty = true;//mark it dirty so it will be added to new canvas
		if (IsValid(Visual))
		{
			RenderCanvas->RegisterVisual(Visual);
		}
	}
	// Last, with the widget already in the new canvas: the visual's clip and property data live at
	// positions in its canvas's data textures, and a visual registered with another canvas has none
	// there until it writes them again.
	OnRenderCanvasChanged(OldRenderCanvas.Get(), RenderCanvas.Get());
}

void UDreamWidget::OnHierarchyAttachmentChanged(UDreamCanvas* ParentRenderCanvas, UDreamWidget* ParentRoot)
{
	auto ThisRenderCanvas = this->GetComponent<UDreamCanvas>();
	if (ThisRenderCanvas != nullptr)
	{
		ParentRenderCanvas = ThisRenderCanvas;
	}

	if (RenderCanvas != ParentRenderCanvas)//if attach to new Canvas, need to remove from old and add to new
	{
		SetRenderCanvas(ParentRenderCanvas);
	}

	CheckRootWidget(ParentRoot);
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->OnHierarchyAttachmentChanged(ParentRenderCanvas, ParentRoot);
		}
	}

	// The raycast's order, as MarkFlattenHierarchyIndexDirty marks it. This widget came into its canvas or moved within
	// it, and the widgets already there keep their order: the canvas hears that, and looks at this widget alone, rather
	// than that its whole hierarchy changed. A canvas widget is ordered among its parent canvas's, which hears as before.
	if (!RenderCanvas.IsValid() || this->bIsCanvasWidget)
	{
		MarkFlattenHierarchyIndexDirty();
	}
	else
	{
		if (RootWidget.IsValid())
		{
			RootWidget->bFlattenHierarchyIndexDirty = true;
		}
		RenderCanvas->MarkWidgetCameOrWent(this);
	}

	{
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		
		MarkAnchorDataChanged_Recursive(false, true, true, false, false);
		MarkLayoutForRebuild(this);
		// Attached while a layout pass is running. The pass walks the pre-order of its tree that it collected
		// before this widget joined, so it never reaches this widget; and the walk above stops at a container that
		// is writing its results -- which may be the new parent itself, whose arrangement was decided without this
		// widget. So the parent is told to arrange again and queued for a later pass of the same frame, which walks
		// the tree as it now is (TickDreamUI rebuilds a tree changed mid-pass between passes), whenever there is a
		// layout on either side to run. Not on a detach, which has no new root, and not outside a pass, where the
		// walk above already queues what has to run.
		if (ParentRoot != nullptr && Parent.IsValid() && IsLayoutWriting())
		{
			UDreamLayoutContainer* ParentLayout = Parent->GetLayoutContainer();
			if (IsValid(ParentLayout))
			{
				ParentLayout->MarkLayoutDirty();
			}
			if (IsValid(ParentLayout) || IsValid(LayoutContainer) || IsValid(LayoutSelf))
			{
				Parent->MarkWidgetLayoutDirty();
			}
		}
	}

	Call_AttachmentChanged();
}

void UDreamWidget::OnRenderCanvasChanged(UDreamCanvas* OldCanvas, UDreamCanvas* NewCanvas)
{
	// SetRenderCanvas has already moved the widget and its visual between the canvases' lists.
	if (IsValid(Visual))
	{
		Visual->OnRenderCanvasChanged(OldCanvas, NewCanvas);
	}
}

void UDreamWidget::CalculateWidgetActive_Recursive()
{
	// What a ray can hit changes with it; see UDreamUIManagerWorldSubsystem::GetHitTestGeneration.
	UDreamUIManagerWorldSubsystem::BumpHitTestGenerationFor(this);
	// MarkRebuildAllLayoutTree empties the WHOLE MapWidgetToLayoutTree, so it says nothing about which
	// widget raised it and one call covers every change this walk can make. It used to be raised from
	// inside the recursion, once per descendant whose cached value moved: deactivating a page of a few
	// hundred widgets wiped the map a few hundred times over, each wipe costing the rebuild of every
	// cached layout tree in the world. Hoisted here it is one wipe for the whole subtree. The
	// MarkLayoutForRebuild below stays per widget on purpose -- unlike the wipe it is about a specific
	// widget, dirtying the containers on its own ancestor chain, and collapsing it to the root would
	// leave nested containers inside the subtree never re-arranging.
	bool bAnyActiveChanged = false;
	struct LOCAL
	{
		static void CalculateWidgetActive(UDreamWidget* Widget, bool& bOutAnyChanged)
		{
			bool bResultActive = true;
			if (!Widget->bWidgetActive || Widget->bParked)
				bResultActive = false;
			else if (Widget->Parent.IsValid())
				bResultActive = Widget->Parent->GetWidgetActiveInHierarchy();
			else
				bResultActive = true;

			if (Widget->bCacheWidgetActiveInHierarchy != bResultActive)
			{
				Widget->bCacheWidgetActiveInHierarchy = bResultActive;
				bOutAnyChanged = true;
				//callback
				Widget->Call_WidgetActiveChanged();
				//canvas update
				Widget->MarkCanvasUpdate(true);
				//tell layout
				MarkLayoutForRebuild(Widget);
			}
			// A copy of the children, not the live array: the active-changed callback above runs behaviour code, and a
			// behaviour going to sleep can take a child away or bring one in -- a dropdown closing its open list
			// destroys the blocker it hung on the root -- under the walk. Walking the live array then broke the
			// iteration.
			const TArray<UDreamWidget*, TInlineAllocator<16>> ChildrenAtStart(Widget->GetChildren());
			for (UDreamWidget* Child : ChildrenAtStart)
			{
				// Children can hold nulls -- garbage collection clears a reference to a widget it
				// took while this one lived, and this walk runs from OnDetachedFromParent, which is
				// exactly when a hierarchy is coming apart. Every other walk in this file guards;
				// this one did not, and dereferenced the null on its first line.
				if (IsValid(Child))
				{
					CalculateWidgetActive(Child, bOutAnyChanged);
				}
			}
		}
	};
	LOCAL::CalculateWidgetActive(this, bAnyActiveChanged);
	if (bAnyActiveChanged)
	{
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			DreamUIManager->MarkRebuildAllLayoutTree();
		}
	}
}

void UDreamWidget::CalculateVisibility_Recursive()
{
	UDreamUIManagerWorldSubsystem::BumpHitTestGenerationFor(this);
	/** The same hoist as in CalculateWidgetActive_Recursive, and for the same reason. */
	bool bAnyLayoutVisibilityChanged = false;
	struct FVisibilityCalculator
	{
		static void Calculate(UDreamWidget* Widget, bool& bOutAnyLayoutChanged)
		{
			const bool bParentLayoutVisible = !Widget->Parent.IsValid() || Widget->Parent->bCacheLayoutVisibleInHierarchy;
			const bool bParentRenderVisible = !Widget->Parent.IsValid() || Widget->Parent->bCacheRenderVisibleInHierarchy;
			const bool bParentAllowsHitTest = !Widget->Parent.IsValid() || Widget->Parent->bCacheChildrenHitTestVisibleInHierarchy;
			const bool bActive = Widget->bCacheWidgetActiveInHierarchy;

			bool bDesignerVisible = true;
#if WITH_EDITOR
			if (Widget->bHiddenInDesigner && (!Widget->GetWorld() || !Widget->GetWorld()->IsGameWorld()))
			{
				bDesignerVisible = false;
			}
#endif

			const bool bCollapsed = Widget->Visibility == EDreamWidgetVisibility::Collapsed || Widget->bLayoutVisibilitySuppressed;
			const bool bPaints = Widget->Visibility != EDreamWidgetVisibility::Hidden && !bCollapsed;
			const bool bBlocksChildren = Widget->Visibility == EDreamWidgetVisibility::HitTestInvisible;
			const bool bSelfAcceptsHit = Widget->Visibility == EDreamWidgetVisibility::Visible;

			const bool bNewLayoutVisible = bActive && bParentLayoutVisible && !bCollapsed;
			const bool bNewRenderVisible = bActive && bDesignerVisible && bParentRenderVisible && bPaints;
			const bool bNewChildrenHitTestVisible = bNewRenderVisible && bParentAllowsHitTest && !bBlocksChildren;
			const bool bNewSelfHitTestVisible = bNewChildrenHitTestVisible && bSelfAcceptsHit;

			const bool bLayoutChanged = Widget->bCacheLayoutVisibleInHierarchy != bNewLayoutVisible;
			const bool bRenderChanged = Widget->bCacheRenderVisibleInHierarchy != bNewRenderVisible;
			const bool bHitTestChanged = Widget->bCacheSelfHitTestVisibleInHierarchy != bNewSelfHitTestVisible
				|| Widget->bCacheChildrenHitTestVisibleInHierarchy != bNewChildrenHitTestVisible;

			Widget->bCacheLayoutVisibleInHierarchy = bNewLayoutVisible;
			Widget->bCacheRenderVisibleInHierarchy = bNewRenderVisible;
			Widget->bCacheSelfHitTestVisibleInHierarchy = bNewSelfHitTestVisible;
			Widget->bCacheChildrenHitTestVisibleInHierarchy = bNewChildrenHitTestVisible;

			if (bLayoutChanged)
			{
				// Cached layout trees keep collapsed subtrees and filter at update time, so the wipe this
				// raises is belt-and-braces (it is also swallowed while a layout pass is executing); the
				// layout pass itself no longer depends on it to see a revealed subtree. It is raised once
				// for the whole walk, outside the recursion, rather than once per changed descendant.
				bOutAnyLayoutChanged = true;
				UDreamWidget::MarkLayoutForRebuild(Widget->Parent.IsValid() ? Widget->Parent.Get() : Widget);
			}
			if (bRenderChanged || bHitTestChanged)
			{
				Widget->MarkCanvasUpdate(true);
			}

			for (UDreamWidget* Child : Widget->GetChildren())
			{
				if (IsValid(Child))
				{
					Calculate(Child, bOutAnyLayoutChanged);
				}
			}
		}
	};
	FVisibilityCalculator::Calculate(this, bAnyLayoutVisibilityChanged);
	if (bAnyLayoutVisibilityChanged)
	{
		if (UDreamUIManagerWorldSubsystem* DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			DreamUIManager->MarkRebuildAllLayoutTree();
		}
	}
}
void UDreamWidget::CalculateInteractable_Recursive()
{
	UDreamUIManagerWorldSubsystem::BumpHitTestGenerationFor(this);
	struct LOCAL
	{
		static void CalculateInteractable(UDreamWidget* Widget)
		{
			// The enabled switch cascades on its own terms and is folded in below. It is computed in
			// this same walk rather than in one of its own because the two answers are always read
			// together, and a second recursion over the subtree would only make it possible for them
			// to disagree for a frame.
			const bool bParentEnabled = !Widget->Parent.IsValid() || Widget->Parent->bCacheEnabledInHierarchy;
			Widget->bCacheEnabledInHierarchy = Widget->bIsEnabled && bParentEnabled;

			bool bResultInteractable = true;
			switch (Widget->Interactable)
			{
			case EDreamWidgetInteractableType::Enabled:
				bResultInteractable = true;
				break;
			case EDreamWidgetInteractableType::Disabled:
				bResultInteractable = false;
				break;
			case EDreamWidgetInteractableType::Inherit:
				if (Widget->Parent.IsValid())
					bResultInteractable = Widget->Parent->GetInteractableInHierarchy();
				else
					bResultInteractable = true;
				break;
			}

			// A disabled ancestor beats a descendant that explicitly asked to be interactable. That
			// asymmetry is the point of having both: "disable this dialog" has to reach the one button
			// inside it that was pinned Enabled, or it does not mean anything.
			bResultInteractable = bResultInteractable && Widget->bCacheEnabledInHierarchy;

			if (Widget->bCacheInteractableInHierarchy != bResultInteractable)
			{
				Widget->bCacheInteractableInHierarchy = bResultInteractable;
				Widget->Call_InteractableChanged();
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Same guard the visibility walk already had, and for the same reason: Children can
				// hold nulls, and this runs from OnDetachedFromParent -- when a hierarchy is coming
				// apart is exactly when it will.
				if (IsValid(Child))
				{
					CalculateInteractable(Child);
				}
			}
		}
	};
	LOCAL::CalculateInteractable(this);
}
void UDreamWidget::CalculateRaycastable_Recursive()
{
	UDreamUIManagerWorldSubsystem::BumpHitTestGenerationFor(this);
	struct LOCAL
	{
		static void CalculateRaycastable(UDreamWidget* Widget)
		{
			bool bResult = true;
			switch (Widget->Raycastable)
			{
			case EDreamWidgetRaycastableType::Disabled:
				bResult = false;
				break;
			case EDreamWidgetRaycastableType::Enabled:
				bResult = true;
				break;
			case EDreamWidgetRaycastableType::Inherit:
				if (Widget->Parent.IsValid())
					bResult = Widget->Parent->GetRaycastableInHierarchy();
				else
					bResult = true;
				break;
			}

			if (Widget->bCacheRaycastableInHierarchy != bResult)
			{
				Widget->bCacheRaycastableInHierarchy = bResult;
				Widget->Call_RaycastableChanged();
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Same guard the visibility walk already had, and for the same reason: Children can
				// hold nulls, and this runs from OnDetachedFromParent -- when a hierarchy is coming
				// apart is exactly when it will.
				if (IsValid(Child))
				{
					CalculateRaycastable(Child);
				}
			}
		}
	};
	LOCAL::CalculateRaycastable(this);
}

UDreamCanvas* UDreamWidget::GetRootCanvas()const
{
	if (RenderCanvas.IsValid())
	{
		return RenderCanvas->GetRootCanvas();
	}
	return nullptr;
}

USceneComponent* UDreamWidget::GetAttachedRootSceneComponent() const
{
	if (auto RootCanvas = GetRootCanvas())
	{
		return RootCanvas->GetAttachedRootSceneComponent();
	}
	return nullptr;
}

void UDreamWidget::MarkCanvasUpdate(bool bRebuildDrawCall)const
{
	if (RenderCanvas.IsValid())
	{
		// Named: a canvas woken by its widgets alone looks at those widgets alone.
		RenderCanvas->MarkWidgetUpdate(const_cast<UDreamWidget*>(this), bRebuildDrawCall);
	}
}

UDreamCanvas* UDreamWidget::GetRenderCanvas()const
{
	// The canvas found while the count of objects gone read the same as now (RenderCanvasRaw): no look-up of the
	// object array for every widget written, raycast and placed. Only read here, from any thread.
	const uint64 Gone = DreamUIGone::Peek();
	if (Gone != 0 && Gone == RenderCanvasRawGone)
	{
		if (DreamUIGone::IsVerifyingKept())
		{
			DreamUIGone::CheckKept(RenderCanvasRaw, RenderCanvas.Get(), TEXT("a widget's render canvas"));
		}
		return RenderCanvasRaw;
	}
	return RenderCanvas.Get();
}

UDreamCanvas* UDreamWidget::KeepRenderCanvas()const
{
	// Read before the look-up: a canvas found alive by it is still alive while the count reads the same.
	const uint64 Gone = DreamUIGone::Read();
	if (Gone == RenderCanvasRawGone)
	{
		if (DreamUIGone::IsVerifyingKept())
		{
			DreamUIGone::CheckKept(RenderCanvasRaw, RenderCanvas.Get(), TEXT("a widget's render canvas, kept"));
		}
		return RenderCanvasRaw;
	}
	UDreamCanvas* const Canvas = RenderCanvas.Get();
	RenderCanvasRaw = Canvas;
	RenderCanvasRawGone = Gone;
	return Canvas;
}

bool UDreamWidget::IsScreenSpaceOverlayUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToScreenSpace();
}
bool UDreamWidget::IsRenderTargetUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToRenderTarget();
}
bool UDreamWidget::IsWorldSpaceUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToWorldSpace();
}

void UDreamWidget::MarkClipDirty(bool InClipTypeChanged) const
{
	bClipDirty = true;
	if (InClipTypeChanged)bNeedRecreateClip = true;
	// Waking the canvas is required, not optional. UDreamCanvas::UpdateCanvasDrawCall gates everything that
	// reconciles clipping behind bCanTickUpdate: UpdateClip (creates/destroys the FDreamUIClipData),
	// UDreamVisual::CheckClipDataStartPosition (refreshes the slot index baked into vertex data) and
	// MarkFinishUpdateCanvasDrawCall (uploads the clip blocks). Marking bClipDirty without waking the canvas
	// leaves a widget sitting on Clipping == ClipToBounds with no ClipData, while its visual still points at a
	// recycled clip slot — the shader then clips against a stale rectangle and the subtree renders nothing.
	MarkCanvasUpdate(true);
	struct LOCAL
	{
		static void MarkDirty(const UDreamWidget* Widget, bool InClipTypeChanged, FDreamVisitedWidgetSet& VisitedWidgets)
		{
			if (!IsValid(Widget) || VisitedWidgets.Contains(Widget))
			{
				return;
			}
			VisitedWidgets.Add(Widget);
			switch (Widget->GetClipping())
			{
			case EDreamWidgetClipping::Inherit:
			case EDreamWidgetClipping::ClipToBounds:
				Widget->bClipDirty = true;
				if (InClipTypeChanged)Widget->bNeedRecreateClip = true;
				// Descendants may render through a different (child) canvas, so wake each one individually.
				Widget->MarkCanvasUpdate(true);
				break;
			case EDreamWidgetClipping::ClipToBoundsWithoutIntersecting:
			case EDreamWidgetClipping::Disabled:
				return;
			}

			for (const UDreamWidget* Child : Widget->GetChildren())
			{
				MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
			}
		}
	};
	FDreamVisitedWidgetSet VisitedWidgets;
	VisitedWidgets.Add(this);
	for (const UDreamWidget* Child : this->GetChildren())
	{
		LOCAL::MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
	}
}
bool UDreamWidget::IsPointVisibleOnClip(const FVector& Value) const
{
	if (ClipData.IsValid())
	{
		return ClipData.Pin()->IsPointVisible(Value);
	}
	return true;
}
void UDreamWidget::SetClipping(EDreamWidgetClipping Value)
{
	if (Clipping != Value)
	{
		const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
		Clipping = Value;
		if (PreviousEffectiveClipping != GetClipping())
		{
			MarkClipDirty(true);
		}
	}
}

void UDreamWidget::SetLayoutClippingOverride(EDreamWidgetClipping Value)
{
	const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = true;
	LayoutClippingOverride = Value;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}

void UDreamWidget::ClearLayoutClippingOverride()
{
	if (!bHasLayoutClippingOverride)
	{
		return;
	}
	const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = false;
	LayoutClippingOverride = EDreamWidgetClipping::Inherit;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}
void UDreamWidget::SetClippingCornerRadius(FVector4f Value)
{
	if (ClippingCornerRadius != Value)
	{
		ClippingCornerRadius = Value;
		MarkClipDirty(false);
	}
}

void UDreamWidget::SetClippingMargin(FMargin Value)
{
	if (ClippingMargin != Value)
	{
		ClippingMargin = Value;
		MarkClipDirty(false);
	}
}

float UDreamWidget::GetFinalRenderOpacity()const
{
	if (Parent.IsValid())
	{
		return this->RenderOpacity * Parent->GetFinalRenderOpacity();
	}
	return this->RenderOpacity;
}
void UDreamWidget::SetRenderOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (RenderOpacity != Value)
	{
		RenderOpacity = Value;
		struct LOCAL
		{
			static void MarkDirty(const UDreamWidget* Widget)
			{
				//Children can hold nulls between a teardown and the next tidy-up: a tween still running
				//on the parent walks straight into one on the frame after a child is deleted.
				if (!IsValid(Widget))
				{
					return;
				}
				if (Widget->Visual)
				{
					Widget->Visual->MarkColorDirty();
				}
				for (auto& Child : Widget->Children)
				{
					MarkDirty(Child);
				}
			}
		};
		LOCAL::MarkDirty(this);
	}
}

bool UDreamWidget::GetPixelSnappingInHierarchy() const
{
	switch (this->PixelSnapping)
	{
	case EWidgetPixelSnapping::SnapToPixel:
		return true;
	case EWidgetPixelSnapping::Disabled:
		return false;
	case EWidgetPixelSnapping::Inherit:
		if (Parent.IsValid())
		{
			return Parent->GetPixelSnappingInHierarchy();
		}
		return false;
	}
	return false;
}

void UDreamWidget::SetPixelSnapping(EWidgetPixelSnapping Value)
{
	if (PixelSnapping != Value)
	{
		PixelSnapping = Value;
		struct LOCAL
		{
			static void MarkChanged(const UDreamWidget* Widget)
			{
				//Children can hold nulls between a teardown and the next tidy-up.
				if (!IsValid(Widget))
				{
					return;
				}
				if (Widget->Visual)
				{
					Widget->Visual->OnPixelSnappingChanged();
				}
				for (auto& Child : Widget->GetChildren())
				{
					MarkChanged(Child);
				}
			}
		};
		LOCAL::MarkChanged(this);
	}
}

bool UDreamWidget::GetWidgetActiveInHierarchy() const
{
	return bCacheWidgetActiveInHierarchy;
}

#if WITH_EDITOR
void UDreamWidget::SetHiddenInDesigner(bool bHidden)
{
	if (bHiddenInDesigner != bHidden)
	{
		bHiddenInDesigner = bHidden;
		CalculateVisibility_Recursive();
	}
}
#endif

void UDreamWidget::SetParked(bool Value)
{
	if (bParked != Value)
	{
		bParked = Value;
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
	}
}

void UDreamWidget::SetWidgetActive(bool Value)
{
	if (bWidgetActive != Value)
	{
		bWidgetActive = Value;
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
	}
}

void UDreamWidget::SetVisibility(EDreamWidgetVisibility Value)
{
	if (Visibility != Value)
	{
		Visibility = Value;
		CalculateVisibility_Recursive();
		OnVisibilityChanged.Broadcast(Visibility);
	}
}

bool UDreamWidget::IsRendered() const
{
	return bCacheRenderVisibleInHierarchy && GetFinalRenderOpacity() > UE_KINDA_SMALL_NUMBER;
}

bool UDreamWidget::IsInViewport() const
{
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	if (ScreenUI == nullptr)
	{
		return false;
	}
	// Walk up: the subsystem tracks PAGES, and the thing a caller means by "am I on screen" is
	// usually a widget several levels inside one.
	for (const UDreamWidget* Ancestor = this; Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		if (ScreenUI->IsInViewport(const_cast<UDreamWidget*>(Ancestor)))
		{
			return true;
		}
	}
	return false;
}

void UDreamWidget::SetRaycastable(EDreamWidgetRaycastableType Value)
{
	if (Raycastable != Value)
	{
		Raycastable = Value;
		CalculateRaycastable_Recursive();
	}
}

void UDreamWidget::SetInteractable(EDreamWidgetInteractableType Value)
{
	if (Interactable != Value)
	{
		Interactable = Value;
		CalculateInteractable_Recursive();
	}
}

void UDreamWidget::SetIsEnabled(bool bInIsEnabled)
{
	if (bIsEnabled == bInIsEnabled)
	{
		return;
	}
	bIsEnabled = bInIsEnabled;
	// One walk does both halves: it recomputes the enabled cascade and the interactable answer that
	// input, navigation and every control's disabled look are already reading.
	CalculateInteractable_Recursive();
}
