// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
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
#if WITH_EDITOR
void UDreamWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName AnchorDataName = GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData);
	const FName ChangedMemberName = PropertyChangedEvent.GetMemberPropertyName();
	// Component/prefab notifications dispatched by Super can run a layout pass immediately.
	// Preserve a direct anchor edit before that pass has a chance to restore stale geometry.
	if (ChangedMemberName == AnchorDataName && IsValid(PanelSlot))
	{
		PanelSlot->SyncAuthoredGeometryAfterUserEdit();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		//MarkAllDirtyRecursive();
		auto MemberName = PropertyChangedEvent.GetMemberPropertyName();
		auto PropertyName = PropertyChangedEvent.GetPropertyName();

		static const FName WidgetActiveName = GET_MEMBER_NAME_CHECKED(UDreamWidget, bWidgetActive);
		static const FName RaycastableName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Raycastable);
		static const FName ClippingName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Clipping);
		static const FName ClippingCornerRadiusName = GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius);
		static const FName ClippingMarginName = GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingMargin);
		static const FName VisualName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Visual);
		static const FName LayoutContainerName = GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutContainer);
		static const FName LayoutSelfName = GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutSelf);
		static const FName PanelSlotName = GET_MEMBER_NAME_CHECKED(UDreamWidget, PanelSlot);
		static const FName VisibilityName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Visibility);
		static const FName IgnoreLayoutName = GET_MEMBER_NAME_CHECKED(UDreamWidget, bIgnoreLayout);
		static const FName InteractableName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Interactable);
		static const FName RenderOpacityName = GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderOpacity);

		if (MemberName == AnchorDataName
		|| MemberName == WidgetActiveName
		|| MemberName == ClippingCornerRadiusName
		|| MemberName == ClippingMarginName
		)
		{
			this->MarkAnchorDataChanged_Recursive(true, true, true);
			this->MarkLayoutForRebuild(this);
			this->MarkClipDirty(false);
		}
		else if (MemberName == ClippingName)
		{
			MarkClipDirty(true);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, SiblingIndex))
		{
			// Same order as SetSiblingIndex: settle the value first, then broadcast it.
			ApplySiblingIndex();
			this->Call_SiblingIndexChanged();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeLocation) || MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeRotation) || MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeScale))
		{
			if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeRotation))
			{
				// A reflection write lands in the quat without the setter that keeps the transient euler
				// mirror in step -- the same silence PostEditUndo and PostLoad already cover for their
				// paths. This one matters to the designer: the template's rotation arrives through
				// FObjectEditorUtils::MigratePropertyValue, which notifies through here, and the
				// write-back prints the EULER field into the .dui. Without the sync the file would
				// carry whatever rotation the template happened to have last session.
				this->RelativeRotationEuler = this->RelativeRotation.Rotator();
			}
			CalculateAnchorFromTransform();
			CalculateObjectToWorldTransform();
			OnUpdateTransform();
			MarkTransformChanged();
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderTranslation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderRotation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderScale)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderTransformPivot))
		{
			// The details panel writes the property memory and then tells us; it does not call the
			// setter. Without this the value lands in the field and the widget never moves, which
			// looks exactly like the feature not working.
			ApplyRenderTransformChange();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, bPerspective)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, PerspectiveFieldOfView)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, PerspectiveOrigin))
		{
			ApplyPerspectiveChange();
		}
		else if (MemberName == VisualName)
		{
			if (IsValid(Visual))
			{
				if (RenderCanvas.IsValid())
				{
					RenderCanvas->RegisterVisual(Visual);
				}
				if (HasBegunPlay())
				{
					Visual->BeginPlay();
				}
				Visual->Call_OnRegister();
			}
			MarkDimensionChanged(false, true, true);//change Visual could cause LayoutSelf size change
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == LayoutContainerName)
		{
			UDreamLayoutContainer* PreviousLayout = LayoutContainerBeforeEdit.Get();
			LayoutContainerBeforeEdit.Reset();
			// The dropdown assigns the instance directly, so the checks CreateNewLayoutContainer makes
			// before accepting a class are repeated here: a container that caps its child count is
			// refused when the widget already has more, and the previous container is put back.
			if (!IsValid(Cast<UDreamPanelLayoutBase>(LayoutContainer)))
			{
				for (UDreamWidget* Child : Children)
				{
					RemovePanelSlotFromChild(Child);
				}
			}
			if (IsValid(LayoutContainer) && LayoutContainer != PreviousLayout)
			{
				const int32 MaxChildren = LayoutContainer->GetMaxChildren();
				int32 ValidChildCount = 0;
				for (const UDreamWidget* Child : Children)
				{
					ValidChildCount += IsValid(Child) ? 1 : 0;
				}
				if (MaxChildren >= 0 && ValidChildCount > MaxChildren)
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s accepts at most %d children but '%s' has %d; keeping the previous panel."),
						ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *LayoutContainer->GetClass()->GetName(), MaxChildren, *GetDisplayName(), ValidChildCount);
					LayoutContainer = PreviousLayout;
				}
			}
			const bool bInitializeScaleBoxSlots = IsValid(LayoutContainer) && LayoutContainer->IsA<UDreamLayoutContainerScaleBox>()
				&& (!IsValid(PreviousLayout) || !PreviousLayout->IsA<UDreamLayoutContainerScaleBox>());
			if (IsValid(LayoutContainer))
			{
				if (HasBegunPlay())
				{
					LayoutContainer->BeginPlay();
				}
				LayoutContainer->Call_OnRegister();
				if (IsValid(Cast<UDreamPanelLayoutBase>(LayoutContainer)))
				{
					for (UDreamWidget* Child : Children)
					{
						if (bInitializeScaleBoxSlots && IsValid(Child))
						{
							// Same transition CreateNewLayoutContainer handles: UMG gives a ScaleBox child a fresh
							// Center/Center slot, Dream reuses the generic slot so the defaults are set here.
							if (UDreamPanelSlot* ExistingSlot = Child->GetPanelSlot(); IsValid(ExistingSlot))
							{
								ExistingSlot->Modify();
								ExistingSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
								ExistingSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
							}
						}
						EnsurePanelSlotForChild(this, Child, true);
					}
				}
				LayoutContainer->CalculateLayout();
			}
			// The details dropdown assigns the container without going through CreateNewLayoutContainer,
			// so the behaviour dependencies are reconciled here. This used to be skipped for a widget
			// inside a sub-prefab instance; a class model has none.
			SyncRequiredBehavioursForLayoutContainer(PreviousLayout, LayoutContainer);
			MarkDimensionChanged(false, true, true);//change LayoutContainer could cause LayoutSelf size change
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == LayoutSelfName)
		{
			if (IsValid(LayoutSelf))
			{
				if (HasBegunPlay())
				{
					LayoutSelf->BeginPlay();
				}
				LayoutSelf->Call_OnRegister();
			}
			MarkDimensionChanged(false, true, true);//change LayoutSelf could cause size change
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == PanelSlotName)
		{
			if (IsValid(PanelSlot))
			{
				if (HasBegunPlay())
				{
					PanelSlot->BeginPlay();
				}
				PanelSlot->Call_OnRegister();
			}
			MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
		}
		else if (MemberName == IgnoreLayoutName)
		{
			MarkDimensionChanged(false, true, true);//change LayoutSelf could cause size change
			//from the parent: the ancestor walk stops at an IgnoreLayout widget, see SetIgnoreLayout
			MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
		}
		if (MemberName == AnchorDataName)
		{
			CalculateTransformFromAnchor();
			this->CalculateObjectToWorldTransform();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeLocation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeRotation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeScale))
		{
			if (IsValid(PanelSlot))
			{
				PanelSlot->SyncAuthoredGeometryAfterUserEdit();
			}
		}
		if (MemberName == WidgetActiveName)
		{
			CalculateWidgetActive_Recursive();
			CalculateVisibility_Recursive();
		}
		if (MemberName == VisibilityName)
		{
			CalculateVisibility_Recursive();
		}
		if (MemberName == RaycastableName)
		{
			CalculateRaycastable_Recursive();
		}
		if (MemberName == InteractableName)
		{
			CalculateInteractable_Recursive();
		}
		if (MemberName == RenderOpacityName)
		{
			struct LOCAL
			{
				static void MarkDirty(const UDreamWidget* Widget)
				{
					// Children can hold nulls between a teardown and the next tidy-up -- deleting a widget
					// in the designer, undoing, or rebuilding the preview all leave one behind, and this
					// runs from the details panel right after any of them. The two runtime twins of this
					// lambda (SetRenderOpacity, SetPixelSnapping) have always guarded; the editor copy
					// dereferenced the null on its first line.
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
		DreamUI::DeferToLaterTick([WeakThis = MakeWeakObjectPtr(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->MarkCanvasUpdate(true);
			}
		}, 1);
	}
}

void UDreamWidget::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	// NULL is an ordinary argument here, not a broken caller: UObject::PreEditUndo() is literally
	// `PreEditChange(NULL)` (CoreUObject/Private/UObject/Obj.cpp), and the transaction buffer calls
	// it on every object in a transaction it replays -- so every Ctrl+Z that touched a widget comes
	// through this line. It survives today only because FField::GetFName() has a null-this
	// compatibility guard (CoreUObject/Public/UObject/Field.h), which is deprecated; when it goes,
	// an unguarded dereference here is "undo crashes the editor". "Which property?" has no answer
	// for an undo, and Super has already done the part that applies to all of them.
	if (PropertyAboutToChange == nullptr)
	{
		return;
	}

	const FName MemberName = PropertyAboutToChange->GetFName();
	if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, Visual))
	{
		if (IsValid(Visual))
		{
			if (RenderCanvas.IsValid())
			{
				RenderCanvas->MarkVisualWillChange(Visual);
				RenderCanvas->UnregisterVisual(Visual);
			}
			if (HasBegunPlay())
			{
				Visual->EndPlay();
			}
			Visual->Call_OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutContainer))
	{
		LayoutContainerBeforeEdit = LayoutContainer;
		if (IsValid(LayoutContainer))
		{
			if (HasBegunPlay())
			{
				LayoutContainer->EndPlay();
			}
			LayoutContainer->Call_OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutSelf))
	{
		if (IsValid(LayoutSelf))
		{
			if (HasBegunPlay())
			{
				LayoutSelf->EndPlay();
			}
			LayoutSelf->Call_OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, PanelSlot))
	{
		if (IsValid(PanelSlot))
		{
			if (HasBegunPlay())
			{
				PanelSlot->EndPlay();
			}
			PanelSlot->Call_OnUnregister();
		}
	}
}

bool UDreamWidget::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	return bIsEditable;
}

bool UDreamWidget::CanEditChange(const FEditPropertyChain& PropertyChain) const
{
	bool bIsEditable = UObject::CanEditChange( PropertyChain );
	return bIsEditable;
}

void UDreamWidget::PostEditUndo()
{
	Super::PostEditUndo();
	if (!IsValid(this))
	{
		// The transaction took this widget away -- undid its creation, or redid its deletion -- by marking
		// it garbage, which runs none of its exit. Nothing else will: the world's teardown passes over
		// garbage, and the undo buffer keeps the object alive until a collection finds it still
		// registered. So it leaves here, and alone: every widget the transaction took gets its own
		// PostEditUndo, and one it did not take -- a child that undoing a wrap put back under its old
		// parent -- still hangs from this widget's Children and is not this widget's to end. Parent is
		// left as it is, though the restored parent no longer lists this widget: a redo lists it again,
		// and the back-pointer is what puts it back in its place (below).
		EndPlay();
		OnUnregister();
		// A redo that brings it back revives it (ReviveLifecycleAfterUndo below).
		Lifecycle = EDreamWidgetLifecycle::Destroyed;
		return;
	}
	// Undo restores RelativeRotation straight into the property, bypassing the setter that keeps
	// the transient euler mirror in step.
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
	// Same silence for the bits derived from the render transform, shear and perspective properties.
	RefreshRenderTransformFlag();
	RefreshPerspectiveInHierarchy();
	RefreshShearInHierarchy();
	// Parent is Transient, so the transaction did NOT restore it: after an undo it still names
	// whoever this widget was attached to when the undo began. Children IS restored, and it is the
	// structural truth -- so the parent's restored array is the thing to ask, not the back-pointer.
	//
	// Asking the back-pointer instead is how undoing a duplicate put the copy back. Undo removed it
	// from the parent's Children and invalidated the object; the re-insert below then wrote it back
	// in at its old sibling index, where it sat as a dead slot that no IsValid-filtered walk could
	// see. Instancing does not filter: the next preview rebuild carried it across as a live widget,
	// so the designer showed the undone copy again, with its child reachable from two parents.
	if (Parent.IsValid() && Parent->Children.Contains(this))
	{
		//restore SiblingIndex
		Parent->Children.Remove(this);
		const int32 RestoredSiblingIndex = FMath::Clamp(SiblingIndex, 0, Parent->Children.Num());
		Parent->Children.Insert(this, RestoredSiblingIndex);
		Parent->EnsureUIChildrenValid();
		for (int i = 0; i < Parent->Children.Num(); i++)
		{
			auto& UIChild = Parent->Children[i];
			if (UIChild->SiblingIndex != i)
			{
				UIChild->SiblingIndex = i;
			}
		}
	}
	else if (Parent.IsValid())
	{
		// The parent the undo restored does not list this widget, which means the back-pointer is
		// stale rather than the array being wrong. Clear it: OnRegister and every walk upward read
		// it, and a widget claiming a parent that disowns it is worse than one claiming none.
		Parent = nullptr;
	}
	// An undo that took back a delete brought back a destroyed widget: the transaction cleared the
	// garbage mark, but Lifecycle is not a property and still says Destroyed. That life ended; the undo
	// has started another, from the beginning.
	ReviveLifecycleAfterUndo();
	// Re-register if unregistered (e.g., undo of a delete operation via DeleteForUndo).
	// Lifecycle is not a UPROPERTY so it is not saved/restored by the undo system;
	// after soft-delete it remains unregistered, so we need to call OnRegister() explicitly.
	// Only for a widget undo keeps, in a world -- one authored in a level. A tree made while a world
	// runs -- a presenter's, the designer preview's, a screen's -- belongs to the host that built it, and
	// whether it comes back is that host's call: registering it from here brought back trees their hosts
	// had destroyed. And the tree a widget class is authored in is a template no world holds: it was never
	// registered, and registering it from here left a live tree in no world, which nothing takes down.
	const bool bWasRegistered = HasRegistered();
	if (!bWasRegistered && DreamUI::IsKeptByUndo(*this) && GetWorld() != nullptr)
	{
		struct LOCAL
		{
			static void RegisterRecursive(UDreamWidget* Widget, TSet<const UDreamWidget*>& VisitedWidgets)
			{
				if (!IsValid(Widget) || VisitedWidgets.Contains(Widget))
				{
					return;
				}
				VisitedWidgets.Add(Widget);
				// The same undo brought the children back; their own PostEditUndo may simply not have
				// run yet, and the order the transaction calls them in is not this one.
				Widget->ReviveLifecycleAfterUndo();
				Widget->OnRegister();
				for (UDreamWidget* Child : Widget->Children)
				{
					if (IsValid(Child))
					{
						RegisterRecursive(Child, VisitedWidgets);
					}
				}
			}
		};
		TSet<const UDreamWidget*> VisitedWidgets;
		LOCAL::RegisterRecursive(this, VisitedWidgets);
	}

	// Transactional pointer swaps do not run the old layout's unregister path. Reset every
	// parent-owned transient before registering and rebuilding the currently restored layout.
	SetLayoutScale(FVector2f::UnitVector);
	SetLayoutVisibilitySuppressed(false);
	ClearLayoutClippingOverride();
	for (UDreamWidget* Child : Children)
	{
		if (!IsValid(Child))
		{
			continue;
		}
		Child->SetLayoutScale(FVector2f::UnitVector);
		Child->SetLayoutVisibilitySuppressed(false);
	}
	if (IsValid(LayoutContainer) && bWasRegistered)
	{
		LayoutContainer->Call_OnRegister();
	}
	if (IsValid(Cast<UDreamLayoutContainerScrollBox>(LayoutContainer)))
	{
		SetLayoutClippingOverride(EDreamWidgetClipping::ClipToBounds);
	}
	if (IsValid(Cast<UDreamPanelLayoutBase>(LayoutContainer)))
	{
		for (UDreamWidget* Child : Children)
		{
			EnsurePanelSlotForChild(this, Child);
		}
	}
	else
	{
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
				{
					Slot->RestoreAuthoredGeometry();
				}
			}
		}
	}
	CalculateVisibility_Recursive();
	MarkLayoutForRebuild(this);
}

void UDreamWidget::ReviveLifecycleAfterUndo()
{
	if (Lifecycle == EDreamWidgetLifecycle::Destroyed && IsValid(this))
	{
		Lifecycle = EDreamWidgetLifecycle::Constructed;
	}
}

void UDreamWidget::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
}

void UDreamWidget::EnsureChildrenAfterTransaction()
{
	struct LOCAL
	{
		static void CheckIt(UDreamWidget* Widget)
		{
			for (int i = 0;i < Widget->Children.Num(); i++)
			{
				auto Child = Widget->Children[i];
				if (!IsValid(Child))
				{
					Widget->Children.RemoveAt(i);
					i--;
					continue;
				}
				Child->SiblingIndex = i;
				CheckIt(Child);
			}
		}
	};
	LOCAL::CheckIt(this);
}

void UDreamWidget::EnsureDataForRebuild()
{
	if (!ensureMsgf(this == RootWidget, TEXT("%s: EnsureDataForRebuild is for the root of a hierarchy, and this widget is not one."), *GetPathName()))
	{
		return;
	}
	struct LOCAL
	{
		static void RenewRenderCanvas(UDreamWidget* Widget)
		{
			auto ThisRenderCanvas = Widget->GetComponent<UDreamCanvas>();
			Widget->RenewRenderCanvasRecursive(ThisRenderCanvas);
		}
		static void EnsureDataForRebuildRecursive(UDreamWidget* Widget)
		{
			Widget->EnsureUIChildrenValid();
			Widget->bNeedSortUIChildren = true;
			Widget->EnsureUIChildrenSorted();
			if (Widget->bIsCanvasWidget && Widget->RenderCanvas.IsValid())
			{
				Widget->RenderCanvas->EnsureDataForRebuild();
			}

			for (auto& uiChild : Widget->Children)
			{
				if (IsValid(uiChild))
				{
					EnsureDataForRebuildRecursive(uiChild);
				}
			}
		}
		/** force refresh render canvas, remove from old and add to new */
		static void ForceRefreshRenderCanvasRecursive(UDreamWidget* Widget)
		{
			auto NewRenderCanvas = Widget->GetComponentInParent<UDreamCanvas>(true);
			Widget->SetRenderCanvas(NewRenderCanvas);

			for (auto& uiChild : Widget->Children)
			{
				if (IsValid(uiChild))
				{
					ForceRefreshRenderCanvasRecursive(uiChild);
				}
			}
		}
	};
	MarkAllDirtyRecursive();
	LOCAL::RenewRenderCanvas(this);
	LOCAL::EnsureDataForRebuildRecursive(this);
	LOCAL::ForceRefreshRenderCanvasRecursive(this);
	CalculateWidgetActive_Recursive();
	CalculateVisibility_Recursive();
	CalculateRaycastable_Recursive();
	CalculateInteractable_Recursive();
	CalculateObjectToWorldTransform();
}



#endif
