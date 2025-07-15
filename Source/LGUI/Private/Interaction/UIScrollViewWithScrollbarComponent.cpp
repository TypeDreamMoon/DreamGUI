// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIScrollViewWithScrollbarComponent.h"
#include "LGUI.h"
#include "Interaction/UIScrollbarComponent.h"
#include "LTweenManager.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/LGUIManager.h"


UUIScrollViewWithScrollbarComponent::UUIScrollViewWithScrollbarComponent()
{
	bLayoutDirty = false;
}

void UUIScrollViewWithScrollbarComponent::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	CheckScrollbarParameter();//Check and register scrollbar event
}

bool UUIScrollViewWithScrollbarComponent::OnPointerDrag_Implementation(ULGUIPointerEventData* eventData)
{
	return Super::OnPointerDrag_Implementation(eventData);
}
bool UUIScrollViewWithScrollbarComponent::OnPointerScroll_Implementation(ULGUIPointerEventData* eventData)
{
	return Super::OnPointerScroll_Implementation(eventData);
}
void UUIScrollViewWithScrollbarComponent::UpdateProgress(bool InFireEvent)
{
	Super::UpdateProgress(InFireEvent);
	if (CheckScrollbarParameter())
	{
		if (bAllowHorizontalScroll && HorizontalScrollbar->GetLexWidget()->IsVisibleForLayout())
		{
			if (Progress.X > 1.0f)
			{
				HorizontalScrollbarComp->SetValueAndSize(1.0f, ContentParentUIItem->GetRenderWidth() / (ContentUIItem->GetRenderWidth() + (HorizontalRange.Y - HorizontalRange.X) * (Progress.X - 1.0f)), false);
			}
			else if (Progress.X < 0.0f)
			{
				HorizontalScrollbarComp->SetValueAndSize(0.0f, ContentParentUIItem->GetRenderWidth() / (ContentUIItem->GetRenderWidth() + (HorizontalRange.Y - HorizontalRange.X) * (0.0f - Progress.X)), false);
			}
			else
			{
				HorizontalScrollbarComp->SetValue(Progress.X, false);
			}
		}
		if (bAllowVerticalScroll && VerticalScrollbar->GetLexWidget()->IsVisibleForLayout())
		{
			if (Progress.Y > 1.0f)
			{
				VerticalScrollbarComp->SetValueAndSize(1.0f, ContentParentUIItem->GetRenderHeight() / (ContentUIItem->GetRenderHeight() + (VerticalRange.Y - VerticalRange.X) * (Progress.Y - 1.0f)), false);
			}
			else if (Progress.Y < 0.0f)
			{
				VerticalScrollbarComp->SetValueAndSize(0.0f, ContentParentUIItem->GetRenderHeight() / (ContentUIItem->GetRenderHeight() + (VerticalRange.Y - VerticalRange.X) * (0.0f - Progress.Y)), false);
			}
			else
			{
				VerticalScrollbarComp->SetValue(Progress.Y, false);
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
		if (HorizontalScrollbar.IsValid())
		{
			if (HorizontalScrollbarComp.IsValid())
			{
				bHorizontalValid = true;
			}
			else
			{
				HorizontalScrollbarComp = HorizontalScrollbar->FindComponentByClass<UUIScrollbarComponent>();
				if (HorizontalScrollbarComp.IsValid())
				{
					HorizontalScrollbarComp->RegisterSlideEvent(FLGUIFloatDelegate::CreateUObject(this, &UUIScrollViewWithScrollbarComponent::OnHorizontalScrollbar));
					HorizontalScrollbar->GetLexWidget()->GetSiblingIndexChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnChildSiblingIndexChanged);
					HorizontalScrollbar->GetLexWidget()->GetAttachmentChangedEvent().AddUObject(this, &UUIScrollViewWithScrollbarComponent::OnChildAttachmentChanged);
					bHorizontalValid = true;
				}
			}
		}
	}

	if (Vertical)
	{
		if (VerticalScrollbar.IsValid())
		{
			if (VerticalScrollbarComp.IsValid())
			{
				bVerticalValid = true;
			}
			else
			{
				VerticalScrollbarComp = VerticalScrollbar->FindComponentByClass<UUIScrollbarComponent>();
				if (VerticalScrollbarComp.IsValid())
				{
					VerticalScrollbarComp->RegisterSlideEvent(FLGUIFloatDelegate::CreateUObject(this, &UUIScrollViewWithScrollbarComponent::OnVerticalScrollbar));
					bVerticalValid = true;
				}
			}
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
	bool hitHorizontalScrollbar = HorizontalScrollbar.IsValid() && (InHitComp->IsAttachedTo(HorizontalScrollbar->GetLexWidget()) || InHitComp == HorizontalScrollbar->GetLexWidget());
	bool hitVerticalScrollbar = VerticalScrollbar.IsValid() && (InHitComp->IsAttachedTo(VerticalScrollbar->GetLexWidget()) || InHitComp == VerticalScrollbar->GetLexWidget());
	return Super::CheckValidHit(InHitComp)
		&& !hitHorizontalScrollbar && !hitVerticalScrollbar;//make sure hit component is not scrollbar
}
void UUIScrollViewWithScrollbarComponent::CalculateHorizontalRange()
{
	Super::CalculateHorizontalRange();
	if (CheckScrollbarParameter())
	{
		auto parentWidth = ContentParentUIItem->GetRenderWidth();
		auto contentWidth = ContentUIItem->GetRenderWidth();
		if (parentWidth >= contentWidth)
		{
			if (HorizontalScrollbarVisibility != EScrollViewScrollbarVisibility::Permanent)
			{
				HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToHide;
			}
		}
		else
		{
			if (HorizontalScrollbarVisibility != EScrollViewScrollbarVisibility::Permanent)
			{
				HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToShow;
			}
		}
		MarkLayoutDirty();
	}
}
void UUIScrollViewWithScrollbarComponent::CalculateVerticalRange()
{
	Super::CalculateVerticalRange();
	if (CheckScrollbarParameter())
	{
		auto parentHeight = ContentParentUIItem->GetRenderHeight();
		auto contentHeight = ContentUIItem->GetRenderHeight();
		if (parentHeight >= contentHeight)
		{
			if (VerticalScrollbarVisibility != EScrollViewScrollbarVisibility::Permanent)
			{
				VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToHide;
			}
		}
		else
		{
			if (VerticalScrollbarVisibility != EScrollViewScrollbarVisibility::Permanent)
			{
				VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::NeedToShow;
			}
		}
		MarkLayoutDirty();
	}
}

void UUIScrollViewWithScrollbarComponent::OnChildSiblingIndexChanged()
{
	MarkLayoutDirty();
}
void UUIScrollViewWithScrollbarComponent::OnChildAttachmentChanged()
{
	MarkLayoutDirty();
}

void UUIScrollViewWithScrollbarComponent::OnUpdateLayout_Implementation()
{
#if 0
	if (bLayoutDirty)
	{
		if (!Viewport.IsValid())return;
		if (!CheckParameters())return;
		if (!CheckScrollbarParameter())return;

		bLayoutDirty = false;

		auto ViewportUIItem = Viewport->GetLexWidget();
		if (ViewportUIItem->GetAttachParent() != this->GetRootUIComponent())
		{
			ViewportUIItem->AttachToComponent(this->GetRootUIComponent(), FAttachmentTransformRules::KeepWorldTransform);
		}

		if (VerticalScrollbar.IsValid())
		{
			auto VerticalScrollbarUIItem = VerticalScrollbar->GetLexWidget();
			if (VerticalScrollbarUIItem->GetAttachParent() != this->GetRootUIComponent())
			{
				VerticalScrollbarUIItem->AttachToComponent(this->GetRootUIComponent(), FAttachmentTransformRules::KeepWorldTransform);
			}
			auto parentHeight = ContentParentUIItem->GetHeight();
			auto contentHeight = ContentUIItem->GetHeight();
			switch (VerticalScrollbarLayoutActionType)
			{
			case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToShow:
			{
				VerticalScrollbarUIItem->SetWidgetVisibility(ESlateVisibility::Visible);
			}
			break;
			case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToHide:
			{
				VerticalScrollbarUIItem->SetWidgetVisibility(ESlateVisibility::Collapsed);
			}
			break;
			}
			if (VerticalScrollbarVisibility == EScrollViewScrollbarVisibility::AutoHideAndExpandViewport)
			{
				if (VerticalScrollbarUIItem->GetIsUIActiveInHierarchy())
				{
					if (VerticalScrollbarUIItem->GetFlattenHierarchyIndex() > ViewportUIItem->GetFlattenHierarchyIndex())
					{
						ViewportUIItem->SetAnchorRight(VerticalScrollbarUIItem->GetWidth());
						ViewportUIItem->SetAnchorLeft(0);

						VerticalScrollbarUIItem->SetHorizontalAnchorMinMax(FVector2D(1, 1), true);
						float AnchorOffset = (VerticalScrollbarUIItem->GetPivot().X - 1.0f) * VerticalScrollbarUIItem->GetWidth();
						VerticalScrollbarUIItem->SetHorizontalAnchoredPosition(AnchorOffset);
					}
					else
					{
						ViewportUIItem->SetAnchorLeft(VerticalScrollbarUIItem->GetWidth());
						ViewportUIItem->SetAnchorRight(0);

						VerticalScrollbarUIItem->SetHorizontalAnchorMinMax(FVector2D(0, 0), true);
						float AnchorOffset = VerticalScrollbarUIItem->GetPivot().X * VerticalScrollbarUIItem->GetWidth();
						VerticalScrollbarUIItem->SetHorizontalAnchoredPosition(AnchorOffset);
					}
				}
				else
				{
					ViewportUIItem->SetAnchorLeft(0);
					ViewportUIItem->SetAnchorRight(0);
				}
			}
			if (VerticalScrollbarComp.IsValid())
			{
				VerticalScrollbarComp->SetValueAndSize(Progress.Y, parentHeight / contentHeight, false);
			}
			VerticalScrollbarLayoutActionType = EScrollbarLayoutAction::None;
		}

		if (HorizontalScrollbar.IsValid())
		{
			auto HorizontalScrollbarUIItem = HorizontalScrollbar->GetLexWidget();
			if (HorizontalScrollbarUIItem->GetAttachParent() != this->GetRootUIComponent())
			{
				HorizontalScrollbarUIItem->AttachToComponent(this->GetRootUIComponent(), FAttachmentTransformRules::KeepWorldTransform);
			}
			auto parentWidth = ContentParentUIItem->GetWidth();
			auto contentWidth = ContentUIItem->GetWidth();
			switch (HorizontalScrollbarLayoutActionType)
			{
			case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToShow:
			{
				HorizontalScrollbarUIItem->SetIsUIActive(true);
			}
			break;
			case UUIScrollViewWithScrollbarComponent::EScrollbarLayoutAction::NeedToHide:
			{
				HorizontalScrollbar->GetLexWidget()->SetIsUIActive(false);
			}
			break;
			}
			if (HorizontalScrollbarVisibility == EScrollViewScrollbarVisibility::AutoHideAndExpandViewport)
			{
				if (HorizontalScrollbarUIItem->GetIsUIActiveInHierarchy())
				{
					if (HorizontalScrollbarUIItem->GetFlattenHierarchyIndex() > ViewportUIItem->GetFlattenHierarchyIndex())
					{
						ViewportUIItem->SetAnchorBottom(HorizontalScrollbarUIItem->GetHeight());
						ViewportUIItem->SetAnchorTop(0);

						HorizontalScrollbarUIItem->SetVerticalAnchorMinMax(FVector2D(0, 0), true);
						float AnchorOffset = HorizontalScrollbarUIItem->GetPivot().Y * HorizontalScrollbarUIItem->GetHeight();
						HorizontalScrollbarUIItem->SetVerticalAnchoredPosition(AnchorOffset);
					}
					else
					{
						ViewportUIItem->SetAnchorTop(HorizontalScrollbarUIItem->GetHeight());
						ViewportUIItem->SetAnchorBottom(0);

						HorizontalScrollbarUIItem->SetVerticalAnchorMinMax(FVector2D(1, 1), true);
						float AnchorOffset = (HorizontalScrollbarUIItem->GetPivot().Y - 1.0f) * HorizontalScrollbarUIItem->GetHeight();
						HorizontalScrollbarUIItem->SetVerticalAnchoredPosition(AnchorOffset);
					}
				}
				else
				{
					ViewportUIItem->SetAnchorTop(0);
					ViewportUIItem->SetAnchorBottom(0);
				}
			}
			if (HorizontalScrollbarComp.IsValid())
			{
				HorizontalScrollbarComp->SetValueAndSize(Progress.X, parentWidth / contentWidth, false);
			}
			HorizontalScrollbarLayoutActionType = EScrollbarLayoutAction::None;
		}
	}
#endif
}

void UUIScrollViewWithScrollbarComponent::OnHorizontalScrollbar(float InScrollValue)
{
	if (!ContentUIItem.IsValid())return;
	bCanUpdateAfterDrag = false;
	bAllowHorizontalScroll = true;

	InScrollValue = FMath::Clamp(InScrollValue, 0.0f, 1.0f);
	auto Position = ContentUIItem->GetRelativeLocation();
	Position.Y = FMath::Lerp(HorizontalRange.X, HorizontalRange.Y, 1.0f - InScrollValue);
	ContentUIItem->SetRelativeLocation(Position);
	Super::UpdateProgress();//use parent's function, skip the set scrollbar code
}
void UUIScrollViewWithScrollbarComponent::OnVerticalScrollbar(float InScrollValue)
{
	if (!ContentUIItem.IsValid())return;
	bCanUpdateAfterDrag = false;
	bAllowVerticalScroll = true;

	InScrollValue = FMath::Clamp(InScrollValue, 0.0f, 1.0f);
	auto Position = ContentUIItem->GetRelativeLocation();
	Position.Z = FMath::Lerp(VerticalRange.X, VerticalRange.Y, InScrollValue);
	ContentUIItem->SetRelativeLocation(Position);
	Super::UpdateProgress();//use parent's function, skip the set scrollbar code
}
void UUIScrollViewWithScrollbarComponent::SetHorizontalScrollbarVisibility(EScrollViewScrollbarVisibility value)
{
	if (HorizontalScrollbarVisibility != value)
	{
		HorizontalScrollbarVisibility = value;
		CalculateHorizontalRange();
	}
}
void UUIScrollViewWithScrollbarComponent::SetVerticalScrollbarVisibility(EScrollViewScrollbarVisibility value)
{
	if (VerticalScrollbarVisibility != value)
	{
		VerticalScrollbarVisibility = value;
		CalculateVerticalRange();
	}
}