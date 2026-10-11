// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/UITextHyperlink.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIButton.h"
#include "Engine/World.h"

UUITextHyperlink::UUITextHyperlink()
{
	// It ticks only to follow a pointer across the text and, in a game, to keep its links' stops on the links, so it
	// starts out of the tick list -- BeginPlay hands bStartWithTickEnabled to the tick switch once Awake has run, so
	// turning it off in Awake left one tick at Start -- and comes in when either has something to do (UpdateTickEnabled).
	// A move of the widget means nothing to it.
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

void UUITextHyperlink::SetLinksAreFocusStops(bool Value)
{
	if (bLinksAreFocusStops != Value)
	{
		bLinksAreFocusStops = Value;
		if (WantsLinkStops())
		{
			RefreshLinkStops();
		}
		else
		{
			DestroyLinkStops();
		}
		UpdateTickEnabled();
	}
}

UDreamWidget* UUITextHyperlink::GetLinkStop(int32 InTagIndex)const
{
	const int32 Slot = LinkStopTagIndices.IndexOfByKey(InTagIndex);
	return LinkStops.IsValidIndex(Slot) && IsValid(LinkStops[Slot]) ? LinkStops[Slot].Get() : nullptr;
}

bool UUITextHyperlink::WantsLinkStops()const
{
	// A game's world only: a designer's preview or an editor world is the authored tree, which stops added at run time must
	// never become part of.
	const UWorld* World = GetWorld();
	return bLinksAreFocusStops && World != nullptr && World->IsGameWorld() && IsValid(GetWidget());
}

void UUITextHyperlink::RefreshLinkStops()
{
	UDreamText* Text = GetTextVisual();
	UDreamWidget* TextWidget = Text != nullptr ? Text->GetWidget() : nullptr;
	if (!WantsLinkStops() || !IsValid(TextWidget))
	{
		DestroyLinkStops();
		return;
	}
	// One stop per link of the text, in the order the links come, so a stop keeps standing for the same link while the text
	// is laid out again around it -- and the focus on it stays where the player left it. The ids are copied out: making a
	// stop registers a widget, which may lay the text out again under a reference into its tags.
	TArray<int32> TagIndices;
	TArray<FName> TagIds;
	{
		const TArray<FDreamUIText_RichTextCustomTag>& Tags = Text->GetRichTextCustomTagArray();
		for (int32 TagIndex = 0; TagIndex < Tags.Num(); ++TagIndex)
		{
			if (Tags[TagIndex].bHyperlink)
			{
				TagIndices.Add(TagIndex);
				TagIds.Add(Tags[TagIndex].TagName);
			}
		}
	}
	// Stops made for another text's widget -- a TextVisual set since -- or gone with it are let go of first.
	bool bStopsChanged = false;
	for (int32 Slot = LinkStops.Num() - 1; Slot >= 0; --Slot)
	{
		UDreamWidget* Stop = LinkStops[Slot].Get();
		if (!IsValid(Stop) || Stop->GetParent() != TextWidget)
		{
			if (IsValid(Stop))
			{
				Stop->DestroyWidget();
			}
			LinkStops.RemoveAt(Slot);
			bStopsChanged = true;
		}
	}
	while (LinkStops.Num() > TagIndices.Num())
	{
		if (UDreamWidget* Extra = LinkStops.Pop(EAllowShrinking::No).Get(); IsValid(Extra))
		{
			Extra->DestroyWidget();
		}
		bStopsChanged = true;
	}
	while (LinkStops.Num() < TagIndices.Num())
	{
		// A plain widget with a button on it: focusable, a Tab and D-pad stop, its confirm a click -- SHyperlink's SButton.
		// Nothing to draw and never hit by a pointer, which reaches the link through the text; never placed by a layout
		// the text's widget may have, nor saved.
		UDreamWidget* Stop = NewObject<UDreamWidget>(this, NAME_None, RF_Transient);
		Stop->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
		Stop->SetIgnoreLayout(true);
		UUIButton* Button = Stop->AddComponent<UUIButton>();
		const TWeakObjectPtr<UDreamWidget> WeakStop(Stop);
		if (Button != nullptr)
		{
			Button->GetOnClickEvent().AddWeakLambda(this, [this, WeakStop]()
			{
				// The link this stop stands for now, which a text laid out again may have moved to another tag.
				const int32 Slot = LinkStops.IndexOfByKey(WeakStop.Get());
				UDreamText* ClickedText = GetTextVisual();
				if (ClickedText != nullptr && LinkStopTagIndices.IsValidIndex(Slot))
				{
					ClickedText->ClickHyperlink(LinkStopTagIndices[Slot]);
				}
			});
		}
		Stop->SetParentBeforeRegister(TextWidget);
		// Centred on the text's own centre, which is what the placement below measures from; anchors need the parent.
		Stop->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
		Stop->SetPivot(FVector2D(0.5, 0.5));
		RegisterDreamWidgetHierarchy(Stop);
		LinkStops.Add(Stop);
		bStopsChanged = true;
	}
	if (bStopsChanged || LinkStopTagIndices != TagIndices)
	{
		for (int32 Slot = 0; Slot < LinkStops.Num() && Slot < TagIds.Num(); ++Slot)
		{
			if (UDreamWidget* Stop = LinkStops[Slot].Get())
			{
				Stop->SetDisplayName(FString::Printf(TEXT("Link_%s"), *TagIds[Slot].ToString()));
			}
		}
	}
	LinkStopTagIndices = MoveTemp(TagIndices);

	// Each over its link's glyphs as they are drawn now. A link with nothing drawn -- not painted yet, or wholly cut off --
	// has its stop put away until it is, rather than left where it was or standing at the text's middle.
	const FVector2D TextCentre = TextWidget->GetLocalSpaceCenter();
	for (int32 Slot = 0; Slot < LinkStops.Num() && Slot < LinkStopTagIndices.Num(); ++Slot)
	{
		UDreamWidget* Stop = LinkStops[Slot].Get();
		if (!IsValid(Stop))
		{
			continue;
		}
		FBox2D Bounds;
		const bool bDrawn = Text->GetHyperlinkLocalBounds(LinkStopTagIndices[Slot], Bounds);
		if (bDrawn)
		{
			const FVector2D Position = Bounds.GetCenter() - TextCentre;
			const FVector2D Size = Bounds.GetSize();
			if (!Stop->GetAnchoredPosition().Equals(Position, 0.01)
				|| !FVector2D(Stop->GetWidth(), Stop->GetHeight()).Equals(Size, 0.01))
			{
				Stop->SetAnchoredPositionAndSizeDelta(Position, Size);
			}
		}
		if (Stop->GetWidgetActive() != bDrawn)
		{
			Stop->SetWidgetActive(bDrawn);
		}
	}
}

void UUITextHyperlink::DestroyLinkStops()
{
	TArray<TObjectPtr<UDreamWidget>> Stops = MoveTemp(LinkStops);
	LinkStops.Reset();
	LinkStopTagIndices.Reset();
	for (const TObjectPtr<UDreamWidget>& Stop : Stops)
	{
		if (IsValid(Stop))
		{
			Stop->DestroyWidget();
		}
	}
}

void UUITextHyperlink::OnEnable()
{
	Super::OnEnable();
	UpdateTickEnabled();
}

void UUITextHyperlink::OnUnregister()
{
	// Taken off a widget that stays, the stops go with this behaviour. A widget torn down takes its children with it, the
	// stops among them, and is left to do so.
	const UDreamWidget* Widget = GetWidget();
	bool bStillOnWidget = false;
	if (IsValid(Widget))
	{
		for (const UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (Component == this)
			{
				bStillOnWidget = true;
			}
		}
	}
	if (IsValid(Widget) && !bStillOnWidget)
	{
		DestroyLinkStops();
	}
	else
	{
		LinkStops.Reset();
		LinkStopTagIndices.Reset();
	}
	Super::OnUnregister();
}

void UUITextHyperlink::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// The stops follow the links as they are drawn: placed every frame, which costs a look at a few glyph quads and moves
	// nothing that has not moved -- the glyphs are painted after the layout that counts a change, so the count alone would
	// place them a frame early, over the old text.
	if (WantsLinkStops())
	{
		RefreshLinkStops();
	}
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
	SetCanExecuteTick((bUseHoverColor && HoverPointer.IsValid()) || (bIsEnableCalled && WantsLinkStops()));
}
