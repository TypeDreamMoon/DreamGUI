// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/UITextHyperlink.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"

UUITextHyperlink::UUITextHyperlink()
{
	// It ticks only to follow a pointer across the text, so it starts out of the tick list -- BeginPlay hands
	// bStartWithTickEnabled to the tick switch once Awake has run, so turning it off in Awake left one tick at Start --
	// and comes in when a pointer does (UpdateTickEnabled). A move of the widget means nothing to it.
	bStartWithTickEnabled = false;
	bCanExecuteTick = false;
	DeclareTransformChangedUnused(StaticClass());
}

UDreamText* UUITextHyperlink::GetTextVisual()const
{
	if (IsValid(TextVisual))
	{
		return TextVisual;
	}
	// The usual arrangement is one text on the widget this behaviour is on, so nobody has to wire it.
	if (UDreamWidget* Widget = GetWidget())
	{
		return Cast<UDreamText>(Widget->GetVisual());
	}
	return nullptr;
}

void UUITextHyperlink::SetTextVisual(UDreamText* Value)
{
	// The colour put on the old text is the old text's to give back, not the new one's.
	HoveredLinkIndex = INDEX_NONE;
	PressedLinkIndex = INDEX_NONE;
	RefreshLinkColor();
	TextVisual = Value;
}

void UUITextHyperlink::SetUseHoverColor(bool Value)
{
	if (bUseHoverColor != Value)
	{
		bUseHoverColor = Value;
		// The pointer was not followed while there was no hover colour to show, so where it is now is asked afresh.
		if (const UDreamPointerEventData* Pointer = HoverPointer.Get())
		{
			LastHoverWorldPoint = Pointer->WorldPoint;
			LastLayoutRunCount = GetTextLayoutRunCount();
			HoveredLinkIndex = FindLinkIndexAt(Pointer->WorldPoint);
		}
		RefreshLinkColor();
		UpdateTickEnabled();
	}
}

void UUITextHyperlink::SetHoverColor(FColor Value)
{
	if (HoverColor != Value)
	{
		HoverColor = Value;
		RefreshLinkColor();
	}
}

void UUITextHyperlink::SetUsePressedColor(bool Value)
{
	if (bUsePressedColor != Value)
	{
		bUsePressedColor = Value;
		RefreshLinkColor();
	}
}

void UUITextHyperlink::SetPressedColor(FColor Value)
{
	if (PressedColor != Value)
	{
		PressedColor = Value;
		RefreshLinkColor();
	}
}

void UUITextHyperlink::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const UDreamPointerEventData* Pointer = HoverPointer.Get();
	if (Pointer == nullptr)
	{
		UpdateTickEnabled();
		return;
	}
	// Looked up again when the pointer has moved, and when the text has been laid out again under a pointer that has
	// not: an edit can put another link, or none, where the pointer is, and give the link a new index. Reading the
	// count lays a changed text out first, which also takes the colour off a tag that is no longer where it was
	// (UDreamText::SetTagColorOverride).
	const int32 LayoutRunCount = GetTextLayoutRunCount();
	const bool bLaidOutAgain = LayoutRunCount != LastLayoutRunCount;
	// The hit point is where the pointer meets whatever it is over in this widget, which a link test can take as it is.
	if (!bLaidOutAgain && Pointer->WorldPoint.Equals(LastHoverWorldPoint))
	{
		return;
	}
	LastHoverWorldPoint = Pointer->WorldPoint;
	LastLayoutRunCount = LayoutRunCount;
	if (bLaidOutAgain && PressedLinkIndex != INDEX_NONE && PressedLinkIndex == OverriddenLinkIndex)
	{
		// The press colour was on its link. If the text has taken it off, the tag at that index is another one now,
		// which the press never went down on, and the press colours nothing more until it comes up.
		FColor InForce;
		const UDreamText* Text = OverriddenText.Get();
		if (Text == nullptr || !Text->GetTagColorOverride(PressedLinkIndex, InForce))
		{
			PressedLinkIndex = INDEX_NONE;
		}
	}
	const int32 LinkIndex = FindLinkIndexAt(Pointer->WorldPoint);
	if (LinkIndex != HoveredLinkIndex || bLaidOutAgain)
	{
		HoveredLinkIndex = LinkIndex;
		RefreshLinkColor();
	}
}

void UUITextHyperlink::OnDisable()
{
	Super::OnDisable();
	// A link left coloured by a pointer that will never be followed out of it would stay lit for good.
	HoverPointer.Reset();
	HoveredLinkIndex = INDEX_NONE;
	PressedLinkIndex = INDEX_NONE;
	RefreshLinkColor();
	UpdateTickEnabled();
}

bool UUITextHyperlink::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	UDreamText* Text = GetTextVisual();
	if (Text == nullptr || EventData == nullptr)
	{
		return true;
	}
	// The event carries where the pointer met the widget's plane, which is the point the glyph quads
	// are in once the widget's transform is undone.
	if (Text->TryClickHyperlinkAtWorldPosition(EventData->GetWorldPointInPlane()))
	{
		return false;//the link took it
	}
	return bAllowEventBubbleUpWhenNoLinkHit;
}

/*
 * Hover and press only look; they never take an event. Enter and exit go to every widget the pointer crosses
 * anyway, and a press is the click's to claim or let through (OnPointerClick), as it was before links could be
 * coloured -- a parent button still sees the press it always saw.
 */
bool UUITextHyperlink::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData != nullptr)
	{
		HoverPointer = EventData;
		LastHoverWorldPoint = EventData->WorldPoint;
		LastLayoutRunCount = GetTextLayoutRunCount();
		HoveredLinkIndex = FindLinkIndexAt(EventData->WorldPoint);
		RefreshLinkColor();
		UpdateTickEnabled();
	}
	return true;
}

bool UUITextHyperlink::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData == nullptr || HoverPointer.Get() == EventData || !HoverPointer.IsValid())
	{
		HoverPointer.Reset();
		HoveredLinkIndex = INDEX_NONE;
		RefreshLinkColor();
		UpdateTickEnabled();
	}
	return true;
}

bool UUITextHyperlink::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData != nullptr)
	{
		PressedLinkIndex = FindLinkIndexAt(EventData->GetWorldPointInPlane());
		RefreshLinkColor();
	}
	return true;
}

bool UUITextHyperlink::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	PressedLinkIndex = INDEX_NONE;
	RefreshLinkColor();
	return true;
}

int32 UUITextHyperlink::FindLinkIndexAt(const FVector& InWorldPoint)const
{
	const UDreamText* Text = GetTextVisual();
	return Text != nullptr ? Text->FindHyperlinkIndexByWorldPosition(InWorldPoint) : INDEX_NONE;
}

int32 UUITextHyperlink::GetTextLayoutRunCount()const
{
	const UDreamText* Text = GetTextVisual();
	return Text != nullptr ? Text->GetCacheTextGeometryData().GetLayoutRunCount() : 0;
}

void UUITextHyperlink::RefreshLinkColor()
{
	// The press wins while it lasts, as :active does over :hover.
	int32 LinkIndex = INDEX_NONE;
	FColor LinkColor = FColor::White;
	if (bUsePressedColor && PressedLinkIndex != INDEX_NONE)
	{
		LinkIndex = PressedLinkIndex;
		LinkColor = PressedColor;
	}
	else if (bUseHoverColor && HoveredLinkIndex != INDEX_NONE)
	{
		LinkIndex = HoveredLinkIndex;
		LinkColor = HoverColor;
	}
	UDreamText* Text = LinkIndex != INDEX_NONE ? GetTextVisual() : nullptr;
	if (UDreamText* Previous = OverriddenText.Get())
	{
		if (Previous != Text || OverriddenLinkIndex != LinkIndex)
		{
			Previous->ClearTagColorOverride(OverriddenLinkIndex);
		}
	}
	OverriddenText.Reset();
	OverriddenLinkIndex = INDEX_NONE;
	if (Text != nullptr)
	{
		Text->SetTagColorOverride(LinkIndex, LinkColor);
		OverriddenText = Text;
		OverriddenLinkIndex = LinkIndex;
	}
}

void UUITextHyperlink::UpdateTickEnabled()
{
	SetCanExecuteTick(bUseHoverColor && HoverPointer.IsValid());
}
