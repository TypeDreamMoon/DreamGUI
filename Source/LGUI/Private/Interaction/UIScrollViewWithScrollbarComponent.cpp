// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIScrollViewWithScrollbarComponent.h"

#include "LTweenBPLibrary.h"
#include "Core/LexUIManager.h"
#include "Interaction/UIScrollbarComponent.h"
#include "PrefabSystem/LGUIPrefabManager.h"


UUIScrollViewWithScrollbarComponent::UUIScrollViewWithScrollbarComponent()
{
	
}

void UUIScrollViewWithScrollbarComponent::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	CheckScrollbarParameter();//Check and register scrollbar event
}

bool UUIScrollViewWithScrollbarComponent::OnPointerDrag_Implementation(ULexPointerEventData* eventData)
{
	return Super::OnPointerDrag_Implementation(eventData);
}
bool UUIScrollViewWithScrollbarComponent::OnPointerScroll_Implementation(ULexPointerEventData* eventData)
{
	return Super::OnPointerScroll_Implementation(eventData);
}
void UUIScrollViewWithScrollbarComponent::UpdateProgress(bool InFireEvent)
{
	Super::UpdateProgress(InFireEvent);
	if (CheckScrollbarParameter())
	{
		if (bAllowHorizontalScroll && HorizontalScrollbarWidget->GetWidgetActiveInHierarchy())
		{
			if (Progress.X > 1.0f)
			{
				HorizontalScrollbar->SetValueAndSize(1.0f, ContentParent->GetWidth() / (Content->GetWidth() + (HorizontalRange.Y - HorizontalRange.X) * (Progress.X - 1.0f)), false);
			}
			else if (Progress.X < 0.0f)
			{
				HorizontalScrollbar->SetValueAndSize(0.0f, ContentParent->GetWidth() / (Content->GetWidth() + (HorizontalRange.Y - HorizontalRange.X) * (0.0f - Progress.X)), false);
			}
			else
			{
				HorizontalScrollbar->SetValueWithoutNotify(Progress.X);
			}
		}
		if (bAllowVerticalScroll && VerticalScrollbarWidget->GetWidgetActiveInHierarchy())
		{
			if (Progress.Y > 1.0f)
			{
				VerticalScrollbar->SetValueAndSize(1.0f, ContentParent->GetHeight() / (Content->GetHeight() + (VerticalRange.Y - VerticalRange.X) * (Progress.Y - 1.0f)), false);
			}
			else if (Progress.Y < 0.0f)
			{
				VerticalScrollbar->SetValueAndSize(0.0f, ContentParent->GetHeight() / (Content->GetHeight() + (VerticalRange.Y - VerticalRange.X) * (0.0f - Progress.Y)), false);
			}
			else
			{
				VerticalScrollbar->SetValueWithoutNotify(Progress.Y);
			}
		}
	}
}
bool UUIScrollViewWithScrollbarComponent::CheckScrollbarParameter()
{
	bool bHorizontalValid = false;
	bool bVerticalValid = false;
	if (Horizontal)
	{
		if (HorizontalScrollbarWidget.IsValid())
		{
			bHorizontalValid = true;
		}
		else
		{
			if (HorizontalScrollbar.IsValid())
			{
				HorizontalScrollbar->GetOnValueChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnHorizontalScrollbar);
				HorizontalScrollbarWidget = HorizontalScrollbar->GetWidget();
				HorizontalScrollbarWidget->GetSiblingIndexChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnScrollbarSiblingIndexChanged);
				HorizontalScrollbarWidget->GetAttachmentChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnScrollbarAttachmentChanged);
				bHorizontalValid = true;
			}
		}
	}

	if (Vertical)
	{
		if (VerticalScrollbarWidget.IsValid())
		{
			bVerticalValid = true;
		}
		else
		{
			VerticalScrollbar->GetOnValueChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnVerticalScrollbar);
			VerticalScrollbarWidget = VerticalScrollbar->GetWidget();
			VerticalScrollbarWidget->GetSiblingIndexChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnScrollbarSiblingIndexChanged);
			VerticalScrollbarWidget->GetAttachmentChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnScrollbarAttachmentChanged);
			bVerticalValid = true;
		}
	}

	if (Horizontal && Vertical)
	{
		if (bHorizontalValid && bVerticalValid)
		{
			return true;
		}
	}
	else
	{
		if (Horizontal)
		{
			return bHorizontalValid;
		}
		if (Vertical)
		{
			return bVerticalValid;
		}
	}

	return false;
}
bool UUIScrollViewWithScrollbarComponent::CheckValidHit(USceneComponent* InHitComp)
{
	bool hitHorizontalScrollbar = HorizontalScrollbarWidget.IsValid() && (InHitComp->IsAttachedTo(HorizontalScrollbarWidget.Get()) || InHitComp == HorizontalScrollbarWidget);
	bool hitVerticalScrollbar = VerticalScrollbarWidget.IsValid() && (InHitComp->IsAttachedTo(VerticalScrollbarWidget.Get()) || InHitComp == VerticalScrollbarWidget);
	return Super::CheckValidHit(InHitComp)
		&& !hitHorizontalScrollbar && !hitVerticalScrollbar;//make sure hit component is not scrollbar
}
void UUIScrollViewWithScrollbarComponent::CalculateHorizontalRange()
{
	Super::CalculateHorizontalRange();
	if (CheckScrollbarParameter())
	{
		auto parentWidth = ContentParent->GetWidth();
		auto contentWidth = Content->GetWidth();
		if (parentWidth >= contentWidth)
		{
			if (HorizontalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToHide;
			}
		}
		else
		{
			if (HorizontalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToShow;
			}
		}
		LateUpdateScrollbarLayout();
	}
}
void UUIScrollViewWithScrollbarComponent::CalculateVerticalRange()
{
	Super::CalculateVerticalRange();
	if (CheckScrollbarParameter())
	{
		auto parentHeight = ContentParent->GetHeight();
		auto contentHeight = Content->GetHeight();
		if (parentHeight >= contentHeight)
		{
			if (VerticalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToHide;
			}
		}
		else
		{
			if (VerticalScrollbarVisibility != ELexUIScrollViewScrollbarVisibility::Permanent)
			{
				VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToShow;
			}
		}
		LateUpdateScrollbarLayout();
	}
}

void UUIScrollViewWithScrollbarComponent::OnScrollbarSiblingIndexChanged()
{
	LateUpdateScrollbarLayout();
}
void UUIScrollViewWithScrollbarComponent::OnScrollbarAttachmentChanged()
{
	LateUpdateScrollbarLayout();
}

void UUIScrollViewWithScrollbarComponent::LateUpdateScrollbarLayout()
{
	//can't update layout immediately because it will break the attachment process and cause crash, so we delay and update
#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())
	{
		ULGUIPrefabManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->UpdateScrollbarLayout();
			}
		}, 1);
	}
	else
#endif
	{
		ULTweenBPLibrary::DelayFrameCall(this, 1, [WeakThis = MakeWeakObjectPtr(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->UpdateScrollbarLayout();
			}
		});
	}
}

void UUIScrollViewWithScrollbarComponent::UpdateScrollbarLayout()
{
	if (!Viewport.IsValid())return;
	if (!CheckParameters())return;
	if (!CheckScrollbarParameter())return;

	if (VerticalScrollbarWidget.IsValid())
	{
		if (VerticalScrollbarWidget->GetAttachParent() != this->GetWidget())
		{
			VerticalScrollbarWidget->AttachToComponent(this->GetWidget(), FAttachmentTransformRules::KeepWorldTransform);
		}
		auto ParentHeight = ContentParent->GetHeight();
		auto ContentHeight = Content->GetHeight();
		switch (VerticalScrollbarLayoutActionType)
		{
		case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToShow:
			{
				VerticalScrollbarWidget->SetWidgetActive(true);
			}
			break;
		case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToHide:
			{
				VerticalScrollbarWidget->SetWidgetActive(false);
			}
			break;
		}
		if (VerticalScrollbarVisibility == ELexUIScrollViewScrollbarVisibility::AutoHideAndExpandViewport)
		{
			if (VerticalScrollbarWidget->GetWidgetActiveInHierarchy())
			{
				if (VerticalScrollbarWidget->GetFlattenHierarchyIndex() > Viewport->GetFlattenHierarchyIndex())
				{
					Viewport->SetAnchorRight(VerticalScrollbarWidget->GetWidth());
					Viewport->SetAnchorLeft(0);

					VerticalScrollbarWidget->SetHorizontalAnchorMinMax(FVector2D(1, 1), true);
					float AnchorOffset = (VerticalScrollbarWidget->GetPivot().X - 1.0f) * VerticalScrollbarWidget->GetWidth();
					VerticalScrollbarWidget->SetHorizontalAnchoredPosition(AnchorOffset);
				}
				else
				{
					Viewport->SetAnchorLeft(VerticalScrollbarWidget->GetWidth());
					Viewport->SetAnchorRight(0);

					VerticalScrollbarWidget->SetHorizontalAnchorMinMax(FVector2D(0, 0), true);
					float AnchorOffset = VerticalScrollbarWidget->GetPivot().X * VerticalScrollbarWidget->GetWidth();
					VerticalScrollbarWidget->SetHorizontalAnchoredPosition(AnchorOffset);
				}
			}
			else
			{
				Viewport->SetAnchorLeft(0);
				Viewport->SetAnchorRight(0);
			}
		}
		if (VerticalScrollbar.IsValid())
		{
			VerticalScrollbar->SetValueAndSize(Progress.Y, ParentHeight / ContentHeight, false);
		}
		VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::None;
	}

	if (HorizontalScrollbarWidget.IsValid())
	{
		if (HorizontalScrollbarWidget->GetAttachParent() != this->GetWidget())
		{
			HorizontalScrollbarWidget->AttachToComponent(this->GetWidget(), FAttachmentTransformRules::KeepWorldTransform);
		}
		auto parentWidth = ContentParent->GetWidth();
		auto contentWidth = Content->GetWidth();
		switch (HorizontalScrollbarLayoutActionType)
		{
		case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToShow:
			{
				HorizontalScrollbarWidget->SetWidgetActive(true);
			}
			break;
		case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToHide:
			{
				HorizontalScrollbarWidget->SetWidgetActive(false);
			}
			break;
		}
		if (HorizontalScrollbarVisibility == ELexUIScrollViewScrollbarVisibility::AutoHideAndExpandViewport)
		{
			if (HorizontalScrollbarWidget->GetWidgetActiveInHierarchy())
			{
				if (HorizontalScrollbarWidget->GetFlattenHierarchyIndex() > Viewport->GetFlattenHierarchyIndex())
				{
					Viewport->SetAnchorBottom(HorizontalScrollbarWidget->GetHeight());
					Viewport->SetAnchorTop(0);

					HorizontalScrollbarWidget->SetVerticalAnchorMinMax(FVector2D(0, 0), true);
					float AnchorOffset = HorizontalScrollbarWidget->GetPivot().Y * HorizontalScrollbarWidget->GetHeight();
					HorizontalScrollbarWidget->SetVerticalAnchoredPosition(AnchorOffset);
				}
				else
				{
					Viewport->SetAnchorTop(HorizontalScrollbarWidget->GetHeight());
					Viewport->SetAnchorBottom(0);

					HorizontalScrollbarWidget->SetVerticalAnchorMinMax(FVector2D(1, 1), true);
					float AnchorOffset = (HorizontalScrollbarWidget->GetPivot().Y - 1.0f) * HorizontalScrollbarWidget->GetHeight();
					HorizontalScrollbarWidget->SetVerticalAnchoredPosition(AnchorOffset);
				}
			}
			else
			{
				Viewport->SetAnchorTop(0);
				Viewport->SetAnchorBottom(0);
			}
		}
		if (HorizontalScrollbar.IsValid())
		{
			HorizontalScrollbar->SetValueAndSize(Progress.X, parentWidth / contentWidth, false);
		}
		HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::None;
	}
}

void UUIScrollViewWithScrollbarComponent::OnHorizontalScrollbar(float InScrollValue)
{
	if (!Content.IsValid())return;
	bCanUpdateAfterDrag = false;
	bAllowHorizontalScroll = true;

	InScrollValue = FMath::Clamp(InScrollValue, 0.0f, 1.0f);
	auto Position = Content->GetRelativeLocation();
	Position.Y = FMath::Lerp(HorizontalRange.X, HorizontalRange.Y, 1.0f - InScrollValue);
	Content->SetRelativeLocation(Position);
	Super::UpdateProgress();//use parent's function, skip the set scrollbar code
}
void UUIScrollViewWithScrollbarComponent::OnVerticalScrollbar(float InScrollValue)
{
	if (!Content.IsValid())return;
	bCanUpdateAfterDrag = false;
	bAllowVerticalScroll = true;

	InScrollValue = FMath::Clamp(InScrollValue, 0.0f, 1.0f);
	auto Position = Content->GetRelativeLocation();
	Position.Z = FMath::Lerp(VerticalRange.X, VerticalRange.Y, InScrollValue);
	Content->SetRelativeLocation(Position);
	Super::UpdateProgress();//use parent's function, skip the set scrollbar code
}
void UUIScrollViewWithScrollbarComponent::SetHorizontalScrollbarVisibility(ELexUIScrollViewScrollbarVisibility value)
{
	if (HorizontalScrollbarVisibility != value)
	{
		HorizontalScrollbarVisibility = value;
		CalculateHorizontalRange();
	}
}
void UUIScrollViewWithScrollbarComponent::SetVerticalScrollbarVisibility(ELexUIScrollViewScrollbarVisibility value)
{
	if (VerticalScrollbarVisibility != value)
	{
		VerticalScrollbarVisibility = value;
		CalculateVerticalRange();
	}
}