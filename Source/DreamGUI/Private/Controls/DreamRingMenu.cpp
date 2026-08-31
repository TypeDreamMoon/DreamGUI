// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamRingMenu.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamLayoutSelfAuthoredSurface.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamRingSectorRaycast.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "DreamTweener.h"
#include "Interaction/UIButton.h"

namespace DreamRingMenuLocal
{
	/** Names, in one place: the bind pass finds every part by display name, as the list does. */
	static const TCHAR* ContentName = TEXT("Content");
	static const TCHAR* IconName = TEXT("Icon");
	static const TCHAR* LabelName = TEXT("Label");

	/** Nothing under a wedge but the wedge itself answers the pointer -- see BindWedge. */
	static void MakeDecoration(UDreamVisual* InVisual)
	{
		if (InVisual != nullptr)
		{
			InVisual->SetRaycastTarget(false);
		}
	}

	/**
	 * A rect turned into a ring of the given radii, or a disc when the inner radius is zero.
	 *
	 * The whole of how this library draws a ring, and it is worth stating once: a procedural rect
	 * has exactly one hole in it -- the space a border does not cover -- so a ring is the border
	 * ALONE with the body switched off. A Percentage corner radius of 1 is half the shorter side,
	 * so the silhouette stays a circle whatever the widget is arranged to; a Percentage border
	 * width resolves to Percentage * min(w,h) * 0.5, which against a square of side 2R is exactly
	 * Percentage * R -- so the fraction to write for a band from InInner to InOuter is
	 * (Outer - Inner) / Outer, and 1 is a full disc.
	 *
	 * The border's own colour is forced WHITE because the state colour is written to the VISUAL
	 * (the vertex colour that multiplies everything the rect draws) and the border defaults to
	 * black -- black times anything is a black ring. UDreamProgressBar's radial shape learned this
	 * the same way.
	 */
	static void ShapeRing(UDreamRectBlock* InRect, float InOuterRadius, float InInnerRadius)
	{
		if (InRect == nullptr)
		{
			return;
		}
		const float Outer = FMath::Max(InOuterRadius, UE_KINDA_SMALL_NUMBER);
		const float Inner = FMath::Clamp(InInnerRadius, 0.0f, Outer);
		InRect->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Percentage);
		InRect->SetCornerRadius(FVector4(1.0, 1.0, 1.0, 1.0));
		InRect->SetEnableBody(false);
		InRect->SetEnableBorder(true);
		InRect->SetBorderWidthUnitMode(EDreamRectBlockUnitMode::Percentage);
		InRect->SetBorderWidth((Outer - Inner) / Outer);
		InRect->SetBorderColor(FColor::White);
	}

	/**
	 * The swept mask, from a start and a sweep stated the family's way: degrees clockwise from
	 * twelve o'clock.
	 *
	 * The quarter turn is the whole conversion. The shader keeps the wedge running from
	 * RadialFillRotation to RadialFillRotation + RadialFillAngle, measured with atan2 in UV space --
	 * whose Y runs DOWN -- so its degrees are clockwise from THREE o'clock, and the two conventions
	 * differ by exactly 90 and nothing else. 360 is the shader's "no mask at all" (its gate is
	 * sign(max(0, 360 - angle))), which is why a full ring switches the mask off rather than asking
	 * for a 360-degree wedge and getting a hairline seam.
	 */
	static void SweepRing(UDreamRectBlock* InRect, float InStartAngle, float InSweepAngle)
	{
		if (InRect == nullptr)
		{
			return;
		}
		const float Sweep = FMath::Clamp(InSweepAngle, 0.0f, 360.0f);
		InRect->SetEnableRadialFill(Sweep < 360.0f);
		InRect->SetRadialFillCenterUnitMode(EDreamRectBlockUnitMode::Percentage);
		InRect->SetRadialFillCenter(FVector2D(0.5, 0.5));
		InRect->SetRadialFillRotation(InStartAngle - 90.0f);
		InRect->SetRadialFillAngle(Sweep);
	}

	/** A point-anchored rect on the parent's centre, stated absolutely. */
	static void PlaceCentred(UDreamWidget* InNode, const FVector2D& InOffset, const FVector2D& InSize)
	{
		if (InNode == nullptr)
		{
			return;
		}
		InNode->SetPivot(FVector2D(0.5, 0.5));
		InNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
		InNode->SetAnchoredPositionAndSizeDelta(InOffset, InSize);
	}

	/** Where a radius and a clockwise-from-twelve angle land, in the ring's own Y-up frame. */
	static FVector2D PolarToLocal(float InRadius, float InAngleDegrees)
	{
		const float Radians = FMath::DegreesToRadians(InAngleDegrees);
		return FVector2D(InRadius * FMath::Sin(Radians), InRadius * FMath::Cos(Radians));
	}
}

void UDreamRingMenu::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;
	using namespace DreamRingMenuLocal;

	// The control's desired size is its AUTHORED square, and the measure walk stops here.
	//
	// Not optional, and the measured symptom says why: an Auto consumer asked what this control was
	// worth and got ZERO -- so the vertical box above it reserved no room at all, the next control
	// in the column was painted straight across the ring, and the wedges (absolute rects centred on
	// the control) spilled out of a slot with no height. The walk crosses every container-less
	// widget on its way down (UDreamPanelLayoutBase::GetDesiredSize), so it reached the item labels;
	// a negative axis is "no opinion" there, but a UDreamText with no font layout behind it answers
	// ZERO, and zero is a CLAIM. One label saying nothing therefore spoke for the whole ring.
	//
	// A ring is the case where descending is meaningless anyway: its parts are placed by polar
	// geometry, and the smallest box containing them is the circle the style already states.
	CreateNewLayoutSelf<UDreamLayoutSelfAuthoredSurface>();

	// Backdrop, wedges, hub -- in that order, which IS the draw order: the unbroken ring sits under
	// the wedges so the gaps between them show something, and the hub sits over both so a wedge that
	// grows on highlight cannot creep into the middle.
	//
	// Nothing here is arranged. Every node states its own rect from the geometry, the way
	// UDreamListViewBase places its rows: placing wedge N costs nothing, depends on no sibling, and
	// is the same answer with no layout pass at all -- which is what a headless test and the
	// designer's first frame actually see. A ring is the case where that matters most, because the
	// arrangement a container would produce is not one any container knows how to make.
	Realize(this,
		Widget("Ring").Out(RingNode)
			.Stretch()
			.Children(
				Node<UDreamRectBlock>("Backdrop").Out(BackdropNode)
					.Stretch()
					.Visual([](UDreamRectBlock& InRect) { MakeDecoration(&InRect); }),
				Widget("Wedges").Out(WedgeRootNode)
					.Stretch()
					.Children(
						Node<UDreamRectBlock>("WedgeTemplate").Out(WedgeTemplateNode)
							.Self([](UDreamWidget& InTemplate)
							{
								// The thing wedges are copied from, not a wedge: asleep, so it
								// neither draws nor answers a pointer.
								InTemplate.SetWidgetActive(false);
							})
							// A button, so a wedge has the four selectable states, a click, and the
							// hover the highlight rides on. Its hit SHAPE is replaced in
							// CreatePoolWedge; without that every wedge would claim the whole circle.
							.With<UUIButton>()
							.Children(
								Widget(ContentName)
									.Children(
										Node<UDreamRectBlock>(IconName)
											.Visual([](UDreamRectBlock& InRect) { MakeDecoration(&InRect); }),
										DreamUI::Text(LabelName)
											.Visual([](UDreamText& InText)
											{
												InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
												InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
												MakeDecoration(&InText);
											})))),
				Node<UDreamRectBlock>("Hub").Out(HubNode)
					.Visual([](UDreamRectBlock& InRect) { MakeDecoration(&InRect); })
					.Children(
						DreamUI::Text("HubLabel").Out(HubLabelNode)
							.Visual([](UDreamText& InText)
							{
								InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
								InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
								MakeDecoration(&InText);
							}))));

	ApplyStyle();
}

void UDreamRingMenu::ApplyStyle()
{
	using namespace DreamRingMenuLocal;

	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	const float Outer = FMath::Max(Active.OuterRadius, 1.0f);
	const float Inner = FMath::Clamp(Active.InnerRadius, 0.0f, Outer);

	// A ring is square and states BOTH axes -- there is no "length comes from whoever placed it" for
	// a circle. The wedges may grow past this on highlight; the control's footprint does not follow
	// them, because a menu that resized its own slot every time the pointer moved would push
	// whatever is beside it around.
	//
	// SizeControl, not SizeFace: this is the control's own rect, so the slot's snapshot has to be
	// SYNCED rather than re-captured -- and that snapshot is exactly what the authored-surface
	// layout-self above answers an Auto consumer with.
	SizeControl(FVector2D(Outer * 2.0, Outer * 2.0));

	if (BackdropNode != nullptr)
	{
		// The same annulus as the wedges, over the whole authored sweep: what shows through the gaps.
		if (UDreamRectBlock* Rect = Cast<UDreamRectBlock>(BackdropNode->GetVisual()))
		{
			ShapeRing(Rect, Outer, Inner);
			SweepRing(Rect, Active.StartAngle, Active.SweepAngle);
		}
		if (UDreamVisual* BackdropVisual = BackdropNode->GetVisual())
		{
			BackdropVisual->SetColor(Active.BackdropColor);
		}
		PlaceCentred(BackdropNode, FVector2D::ZeroVector, FVector2D(Outer * 2.0, Outer * 2.0));
	}

	if (HubNode != nullptr)
	{
		if (UDreamRectBlock* Rect = Cast<UDreamRectBlock>(HubNode->GetVisual()))
		{
			// A disc, not a ring: zero inner radius is a border thick enough to close the middle.
			ShapeRing(Rect, Inner, 0.0f);
			SweepRing(Rect, 0.0f, 360.0f);
		}
		if (UDreamVisual* HubVisual = HubNode->GetVisual())
		{
			HubVisual->SetColor(Active.HubColor);
		}
		PlaceCentred(HubNode, FVector2D::ZeroVector, FVector2D(Inner * 2.0, Inner * 2.0));
		// An inner radius of zero is a menu with no hub at all, and a zero-sized disc would still
		// answer nothing but would keep a text child alive in the middle of the ring.
		HubNode->SetWidgetActive(Inner > UE_KINDA_SMALL_NUMBER);
	}
	if (HubLabelNode != nullptr)
	{
		if (UDreamText* Text = Cast<UDreamText>(HubLabelNode->GetVisual()))
		{
			Text->SetColor(Active.HubTextColor);
			Text->SetFontSize(Active.HubFontSize);
		}
		// Inside the hub's own square, inset so a caption does not run out over the ring.
		const float Span = FMath::Max(0.0f, Inner * 2.0f - Active.HubFontSize);
		PlaceCentred(HubLabelNode, FVector2D::ZeroVector, FVector2D(Span, Span));
	}

	RebuildItems();
}

float UDreamRingMenu::GetTotalWeight() const
{
	float Total = 0.0f;
	for (const FDreamRingMenuItem& Item : Items)
	{
		Total += FMath::Max(Item.Weight, 0.0f) > 0.0f ? Item.Weight : 1.0f;
	}
	// A set of weights that adds to nothing spreads evenly. Not an error: an even ring is the
	// honest reading of "no opinion", and it is what an author who never touched Weight gets.
	return Total > UE_KINDA_SMALL_NUMBER ? Total : static_cast<float>(FMath::Max(1, Items.Num()));
}

float UDreamRingMenu::GetItemStartAngle(int32 InIndex) const
{
	if (!Items.IsValidIndex(InIndex))
	{
		return 0.0f;
	}
	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	const float Sweep = FMath::Clamp(Active.SweepAngle, 0.0f, 360.0f);
	const float Total = GetTotalWeight();
	float Accumulated = 0.0f;
	for (int32 Index = 0; Index < InIndex; ++Index)
	{
		Accumulated += FMath::Max(Items[Index].Weight, 0.0f) > 0.0f ? Items[Index].Weight : 1.0f;
	}
	return Active.StartAngle + Sweep * (Accumulated / Total);
}

float UDreamRingMenu::GetItemSweepAngle(int32 InIndex) const
{
	if (!Items.IsValidIndex(InIndex))
	{
		return 0.0f;
	}
	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	const float Sweep = FMath::Clamp(Active.SweepAngle, 0.0f, 360.0f);
	const float Weight = FMath::Max(Items[InIndex].Weight, 0.0f) > 0.0f ? Items[InIndex].Weight : 1.0f;
	return Sweep * (Weight / GetTotalWeight());
}

float UDreamRingMenu::GetItemMidAngle(int32 InIndex) const
{
	return GetItemStartAngle(InIndex) + GetItemSweepAngle(InIndex) * 0.5f;
}

int32 UDreamRingMenu::IndexAtAngle(float InAngleDegrees) const
{
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		if (UDreamRingSectorRaycast::IsAngleInSweep(
			FMath::Fmod(InAngleDegrees + 720.0f, 360.0f), GetItemStartAngle(Index), GetItemSweepAngle(Index)))
		{
			return Index;
		}
	}
	// A partial ring genuinely does not cover every direction, and saying so is the point: a stick
	// pushed into the empty half of a half-ring should pick nothing, not the nearest edge.
	return INDEX_NONE;
}

UDreamWidget* UDreamRingMenu::GetWedgeWidget(int32 InIndex) const
{
	return WedgeNodes.IsValidIndex(InIndex) ? WedgeNodes[InIndex].Get() : nullptr;
}

void UDreamRingMenu::SetItems(const TArray<FDreamRingMenuItem>& InItems)
{
	Items = InItems;
	// The highlight names an item, and the items just changed underneath it.
	HighlightedIndex = INDEX_NONE;
	RebuildItems();
}

void UDreamRingMenu::RebuildItems()
{
	if (!IsValid(WedgeRootNode) || !IsValid(WedgeTemplateNode))
	{
		return;
	}
	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);

	// Both cursors, against items that may have just moved underneath them. Here rather than in
	// SetItems because a details-panel edit of the array reaches this through ApplyStyle without
	// passing a setter, and an index pointing past the end is the kind of thing that survives until
	// something dereferences it. Quietly: an item disappearing is not the user choosing.
	if (!Items.IsValidIndex(SelectedIndex))
	{
		SelectedIndex = INDEX_NONE;
	}
	if (!Items.IsValidIndex(HighlightedIndex))
	{
		HighlightedIndex = INDEX_NONE;
	}

	// The pool is kept whenever it can be, and for UDreamListViewBase::RebuildRows' reason rather
	// than for the frame rate: creating or destroying a widget marks the UI outliner dirty, the
	// designer answers that by force-refreshing the details view, and ApplyStyle runs on every
	// PostEditChangeProperty -- so a control that tore its pool down each time would cost tens of
	// milliseconds per click in the details panel. A rebind carries everything except a change in
	// the NUMBER of wedges, so that is the only thing that resizes the pool.
	ResizePool(Items.Num());

	for (int32 Index = 0; Index < WedgeNodes.Num(); ++Index)
	{
		BindWedge(Index, Active);
	}
	// Once, after every wedge is bound, rather than once per wedge: the colour pass reads the whole
	// pool, so calling it from inside the bind loop would be the same work N times over.
	RefreshWedgeColors();
	RefreshHubText();
	// Last of all, so a decorator hooked here sees a finished wedge -- geometry, content and colour.
	for (int32 Index = 0; Index < WedgeNodes.Num(); ++Index)
	{
		OnWedgeGenerated.Broadcast(Index, WedgeNodes[Index].Get());
	}
}

void UDreamRingMenu::ResizePool(int32 InPoolSize)
{
	InPoolSize = FMath::Max(0, InPoolSize);
	for (int32 Index = WedgeNodes.Num() - 1; Index >= InPoolSize; --Index)
	{
		if (IsValid(WedgeNodes[Index]))
		{
			WedgeNodes[Index]->DestroyWidget();
		}
		WedgeNodes.RemoveAt(Index);
	}
	while (WedgeNodes.Num() < InPoolSize)
	{
		UDreamWidget* Wedge = CreatePoolWedge(WedgeNodes.Num());
		if (Wedge == nullptr)
		{
			// Duplication failed, and going round again would spin: stop with the pool short rather
			// than never returning.
			break;
		}
		WedgeNodes.Add(Wedge);
	}
}

UDreamWidget* UDreamRingMenu::CreatePoolWedge(int32 InIndex)
{
	// Outered to the wedge root's outer -- the widget tree -- which is where every widget in this
	// hierarchy lives. Duplication brings the subtree, its visual, its slot and its behaviours, then
	// registers the copy under its new parent; no world is required for any of it.
	UDreamWidget* Wedge = DuplicateDreamWidgetHierarchy(
		WedgeRootNode->GetOuter(), WedgeTemplateNode, WedgeRootNode);
	if (!IsValid(Wedge))
	{
		return nullptr;
	}
	Wedge->SetDisplayName(FString::Printf(TEXT("Wedge_%d"), InIndex));
	Wedge->SetWidgetActive(true);

	if (UUIButton* Button = Wedge->GetComponent<UUIButton>())
	{
		// Re-aimed, every time. TransitionTarget is a weak pointer copied BY VALUE, so a fresh copy
		// starts out pointing at the TEMPLATE's visual -- left alone, every hover anywhere on the
		// ring would repaint the one widget nobody can see. Registration fills the target in only
		// when it is EMPTY, and a copied one is not empty, it is wrong.
		Button->SetTransitionTarget(Wedge->GetVisual());
		// The index is captured rather than carried on a component, because a dynamic delegate can
		// only reach a UFUNCTION with no arguments -- which could not say WHICH wedge spoke.
		Button->GetOnClickEvent().AddWeakLambda(this, [this, InIndex]()
		{
			HandleWedgeClicked(InIndex);
		});
		Button->GetOnHoveredEvent().AddWeakLambda(this, [this, InIndex]()
		{
			SetHighlightedIndex(InIndex);
		});
		Button->GetOnUnhoveredEvent().AddWeakLambda(this, [this, InIndex]()
		{
			// Only if it is still THIS wedge: the pointer crossing from one slice straight into the
			// next fires the new wedge's enter before the old one's exit, and an unconditional clear
			// here would throw away the highlight that just arrived.
			if (HighlightedIndex == InIndex)
			{
				SetHighlightedIndex(INDEX_NONE);
			}
		});
	}

	// The hit shape. Made once per wedge and re-aimed the same way the transition target is: an
	// Instanced subobject copied out of the template would be shared or stale, and either way the
	// answer would be some other wedge's slice.
	if (UDreamVisual* WedgeVisual = Wedge->GetVisual())
	{
		UDreamRingSectorRaycast* Sector = NewObject<UDreamRingSectorRaycast>(WedgeVisual);
		WedgeVisual->SetCustomRaycastObject(Sector);
		WedgeVisual->SetRaycastType(EDreamVisualRaycastType::Custom);
	}
	return Wedge;
}

float UDreamRingMenu::ResolveHitOuterRadius(const FDreamRingMenuStyle& InStyle, float InDrawnReach) const
{
	if (HitArea != EDreamRingHitArea::Slice)
	{
		// Ring claims exactly what it draws, growth included -- see the note in SetHighlightedIndex
		// about why the hit edge has to follow the drawn one outward.
		return InDrawnReach;
	}
	// Against the AUTHORED radius, not the grown one: the slice's far edge has nothing to do with
	// which item the pointer is on, and a boundary that moved when the highlight did would be one
	// more thing to oscillate.
	const float Outer = FMath::Max(InStyle.OuterRadius, 1.0f);
	// Zero is the raycast's "no limit", and reaching it takes an explicit zero here.
	return SliceHitRadiusScale > 0.0f ? Outer * SliceHitRadiusScale : 0.0f;
}

void UDreamRingMenu::BindWedge(int32 InIndex, const FDreamRingMenuStyle& InStyle)
{
	using namespace DreamRingMenuLocal;

	UDreamWidget* Wedge = GetWedgeWidget(InIndex);
	if (!IsValid(Wedge) || !Items.IsValidIndex(InIndex))
	{
		return;
	}
	const FDreamRingMenuItem& Item = Items[InIndex];

	const float Outer = FMath::Max(InStyle.OuterRadius, 1.0f);
	const float Inner = FMath::Clamp(InStyle.InnerRadius, 0.0f, Outer);
	const bool bHighlighted = (InIndex == HighlightedIndex);
	// The highlighted wedge reaches further out. Its inner edge does not move, which is why the
	// border fraction is recomputed against the grown radius rather than reused.
	const float Reach = Outer + (bHighlighted ? FMath::Max(InStyle.HighlightGrowth, 0.0f) : 0.0f);

	const float SliceStart = GetItemStartAngle(InIndex);
	const float SliceSweep = GetItemSweepAngle(InIndex);
	// The gap is taken out of the DRAWN wedge, half at each end. The hit sector below keeps the full
	// slice; see the class header for why the two differ on purpose.
	const float Gap = FMath::Clamp(InStyle.ItemGapAngle, 0.0f, FMath::Max(SliceSweep - 0.5f, 0.0f));

	PlaceCentred(Wedge, FVector2D::ZeroVector, FVector2D(Reach * 2.0, Reach * 2.0));
	if (UDreamRectBlock* Rect = Cast<UDreamRectBlock>(Wedge->GetVisual()))
	{
		ShapeRing(Rect, Reach, Inner);
		SweepRing(Rect, SliceStart + Gap * 0.5f, SliceSweep - Gap);
	}

	// The hit sector. In Ring the wedge claims exactly what it draws; in Slice it claims its whole
	// direction outward with no far edge, which is the weapon-wheel feel.
	if (UDreamVisual* WedgeVisual = Wedge->GetVisual())
	{
		if (UDreamRingSectorRaycast* Sector = Cast<UDreamRingSectorRaycast>(WedgeVisual->GetCustomRaycastObject()))
		{
			Sector->InnerRadius = DeadZoneRadius > 0.0f ? DeadZoneRadius : Inner;
			Sector->OuterRadius = ResolveHitOuterRadius(InStyle, Reach);
			Sector->StartAngle = SliceStart;
			Sector->SweepAngle = SliceSweep;
		}
	}

	// Disabled at the WIDGET, which is what UUISelectable::IsInteractable actually consults -- so the
	// wedge goes to its Disabled state and stays there rather than lighting up under the pointer.
	// The handlers check bEnabled too: the button still broadcasts its hover, because the selectable
	// answers a pointer it cannot act on.
	Wedge->SetInteractable(Item.bEnabled
		? EDreamWidgetInteractableType::Enabled
		: EDreamWidgetInteractableType::Disabled);

	if (UUIButton* Button = Wedge->GetComponent<UUIButton>())
	{
		Button->SetHoveredColor(InStyle.WedgeHovered);
		Button->SetPressedColor(InStyle.WedgePressed);
		Button->SetDisabledColor(InStyle.WedgeDisabled);
	}

	// The icon and the label ride together, in a box the wedge places and turns. Both are anchored
	// rather than stacked by a container: a container would need the box's height to be the sum of
	// what it holds -- an equation solved in two places -- and would still top-align the pair inside
	// a box the ring's geometry decides the size of.
	UDreamWidget* Content = Wedge->FindChildByDisplayName(ContentName);
	UDreamWidget* Icon = Content != nullptr ? Content->FindChildByDisplayName(IconName) : nullptr;
	UDreamWidget* Label = Content != nullptr ? Content->FindChildByDisplayName(LabelName) : nullptr;

	const bool bHasIcon = Item.Icon.Image != nullptr;
	const bool bHasLabel = bShowLabels && !Item.Label.IsEmpty();
	const FVector2D IconSize = BrushSizeOr(Item.Icon, InStyle.IconSize);
	const float LabelHeight = InStyle.LabelHeight > 0.0f ? InStyle.LabelHeight : InStyle.FontSize * 2.6f;
	const float IconSpan = bHasIcon ? static_cast<float>(IconSize.Y) : 0.0f;
	const float LabelSpan = bHasLabel ? LabelHeight : 0.0f;
	const float Spacing = (bHasIcon && bHasLabel) ? FMath::Max(InStyle.IconSpacing, 0.0f) : 0.0f;
	const float StackHeight = IconSpan + Spacing + LabelSpan;

	if (Content != nullptr)
	{
		const float ContentRadius = InStyle.ContentRadius > 0.0f
			? InStyle.ContentRadius
			// Midway between the two edges, which stays right when the radii change. Against the
			// AUTHORED outer radius, not the grown one: content that slid outward every time the
			// pointer arrived would read as a wobble, not as a highlight.
			: (Inner + Outer) * 0.5f;
		PlaceCentred(Content,
			PolarToLocal(ContentRadius, GetItemMidAngle(InIndex)),
			FVector2D(InStyle.ContentWidth, StackHeight));

		// Render-only, and a ROLL, which is the one rotation that stays on the batched 2D path.
		// UE's roll about +X maps a local "up" of (0,0,1) to (0, sin R, cos R) -- toward the RIGHT as
		// R grows -- so a positive roll turns clockwise on screen, which is already this family's
		// convention. Tangential is therefore the item's own angle (the box turns with the ring, so
		// text runs along the tangent with its top facing outward) and Radial is a further quarter
		// back, which puts the baseline along the radius pointing outward.
		float Facing = 0.0f;
		if (LabelFacing == EDreamRingLabelFacing::Tangential)
		{
			Facing = GetItemMidAngle(InIndex);
		}
		else if (LabelFacing == EDreamRingLabelFacing::Radial)
		{
			Facing = GetItemMidAngle(InIndex) - 90.0f;
		}
		Content->SetRenderRotation(FRotator(0.0, 0.0, Facing));
	}

	// Stacked from the top of the box downward, both centred across it.
	const float Top = StackHeight * 0.5f;
	if (Icon != nullptr)
	{
		Icon->SetWidgetActive(bHasIcon);
		if (bHasIcon)
		{
			SkinFace(Icon, Item.Icon);
			ShapeFace(Icon, 0.0f);
			PlaceCentred(Icon, FVector2D(0.0, Top - IconSpan * 0.5), IconSize);
		}
	}
	if (Label != nullptr)
	{
		Label->SetWidgetActive(bHasLabel);
		if (bHasLabel)
		{
			if (UDreamText* Text = Cast<UDreamText>(Label->GetVisual()))
			{
				Text->SetText(Item.Label);
				Text->SetFontSize(InStyle.FontSize);
			}
			PlaceCentred(Label,
				FVector2D(0.0, Top - IconSpan - Spacing - LabelSpan * 0.5),
				FVector2D(InStyle.ContentWidth, LabelSpan));
		}
	}

}

void UDreamRingMenu::RefreshWedgeColors()
{
	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	for (int32 Index = 0; Index < WedgeNodes.Num(); ++Index)
	{
		UDreamWidget* Wedge = WedgeNodes[Index].Get();
		if (!IsValid(Wedge) || !Items.IsValidIndex(Index))
		{
			continue;
		}
		const FDreamRingMenuItem& Item = Items[Index];
		// Selection is not a pointer state -- it has to survive the pointer leaving -- so it rides
		// the selectable's NORMAL colour rather than a fifth transition it does not have.
		// FDreamListStyle and FDreamTabViewStyle make the same call in the same words.
		FColor Resting = Item.bOverrideColor ? Item.Color : Active.WedgeNormal;
		if (!Item.bEnabled)
		{
			Resting = Active.WedgeDisabled;
		}
		else if (Index == SelectedIndex)
		{
			Resting = Active.WedgeSelected;
		}

		if (UUIButton* Button = Wedge->GetComponent<UUIButton>())
		{
			Button->SetNormalColor(Resting);
		}
		// And onto the visual directly. SetNormalColor repaints only while the wedge is in its
		// Normal state AND a transition can actually run -- the tween manager needs a world -- so a
		// wedge's resting colour is data this control owns, not something to hope a transition will
		// deliver. The one exception is the wedge the pointer is ON: writing over the hover tint
		// there would undo the transition that just ran.
		if (UDreamVisual* WedgeVisual = Wedge->GetVisual())
		{
			if (Index != HighlightedIndex || !Item.bEnabled)
			{
				WedgeVisual->SetColor(Resting);
			}
		}

		if (UDreamWidget* Content = Wedge->FindChildByDisplayName(DreamRingMenuLocal::ContentName))
		{
			if (UDreamWidget* Label = Content->FindChildByDisplayName(DreamRingMenuLocal::LabelName))
			{
				if (UDreamText* Text = Cast<UDreamText>(Label->GetVisual()))
				{
					Text->SetColor(!Item.bEnabled
						? Active.LabelDisabledColor
						: (Index == SelectedIndex ? Active.LabelSelectedColor : Active.LabelColor));
				}
			}
		}
	}
}

void UDreamRingMenu::RefreshHubText()
{
	UDreamText* Text = HubLabelNode != nullptr ? Cast<UDreamText>(HubLabelNode->GetVisual()) : nullptr;
	if (Text == nullptr)
	{
		return;
	}
	FText Caption = HubText;
	if (bHubFollowsHighlight)
	{
		// The highlight first, the selection second, the authored caption last -- what the pointer is
		// on says more than what was chosen a moment ago, and both say more than a static title.
		if (Items.IsValidIndex(HighlightedIndex))
		{
			Caption = Items[HighlightedIndex].Label;
		}
		else if (Items.IsValidIndex(SelectedIndex))
		{
			Caption = Items[SelectedIndex].Label;
		}
	}
	Text->SetText(Caption);
}

void UDreamRingMenu::SetHighlightedIndex(int32 InIndex)
{
	// A disabled item is not a place the highlight can be, whichever route asked for it.
	if (!Items.IsValidIndex(InIndex) || !Items[InIndex].bEnabled)
	{
		InIndex = INDEX_NONE;
	}
	if (HighlightedIndex == InIndex)
	{
		return;
	}
	const int32 Previous = HighlightedIndex;
	HighlightedIndex = InIndex;

	// Only the two wedges that changed, and only their GEOMETRY -- the colours are one pass over
	// everything below, which is cheap, and re-binding the whole ring on every pointer move would
	// re-broadcast OnWedgeGenerated for wedges nothing happened to.
	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	const float Outer = FMath::Max(Active.OuterRadius, 1.0f);
	const float Inner = FMath::Clamp(Active.InnerRadius, 0.0f, Outer);
	auto ReachOf = [&Active, Outer](bool bHighlighted)
	{
		return Outer + (bHighlighted ? FMath::Max(Active.HighlightGrowth, 0.0f) : 0.0f);
	};
	auto Resize = [this, &Active, Inner, &ReachOf](int32 InWedgeIndex, bool bHighlighted)
	{
		UDreamWidget* Wedge = GetWedgeWidget(InWedgeIndex);
		if (!IsValid(Wedge))
		{
			return;
		}
		const float Reach = ReachOf(bHighlighted);
		DreamRingMenuLocal::PlaceCentred(Wedge, FVector2D::ZeroVector, FVector2D(Reach * 2.0, Reach * 2.0));
		if (UDreamRectBlock* Rect = Cast<UDreamRectBlock>(Wedge->GetVisual()))
		{
			// The mask is unchanged; only the two radii are, and the border fraction is stated
			// against the new outer one so the inner edge stays put.
			DreamRingMenuLocal::ShapeRing(Rect, Reach, Inner);
		}
		if (UDreamVisual* WedgeVisual = Wedge->GetVisual())
		{
			if (UDreamRingSectorRaycast* Sector = Cast<UDreamRingSectorRaycast>(WedgeVisual->GetCustomRaycastObject()))
			{
				// The Ring hit area follows the drawn edge outward, so the wedge does not slip out
				// from under the pointer that grew it -- which would exit, shrink, re-enter, and
				// oscillate for as long as the pointer sat on the old boundary.
				Sector->OuterRadius = ResolveHitOuterRadius(Active, Reach);
			}
		}
	};
	Resize(Previous, false);
	Resize(HighlightedIndex, true);

	RefreshWedgeColors();
	RefreshHubText();
	OnHighlightChanged.Broadcast(HighlightedIndex);

	if (bSelectOnHighlight && HighlightedIndex != INDEX_NONE)
	{
		SetSelectedIndex(HighlightedIndex);
	}
}

void UDreamRingMenu::HighlightByAngle(float InAngleDegrees)
{
	SetHighlightedIndex(IndexAtAngle(InAngleDegrees));
}

void UDreamRingMenu::HighlightByDirection(FVector2D InDirection)
{
	const float Length = static_cast<float>(InDirection.Size());
	// A stick at rest is a vector of nearly nothing pointing nowhere in particular, and honouring its
	// MAGNITUDE is what stops that from picking an item every frame. Against StickDeadZone and not
	// DeadZoneRadius: an axis pair is normalized and a radius is in ring units, so one number serving
	// both would have compared a magnitude of 1 against a radius of 80 and never picked anything.
	if (Length <= UE_KINDA_SMALL_NUMBER || Length < StickDeadZone)
	{
		SetHighlightedIndex(INDEX_NONE);
		return;
	}
	HighlightByAngle(UDreamRingSectorRaycast::AngleOfLocalPoint(InDirection));
}

void UDreamRingMenu::StepHighlight(int32 InDelta)
{
	if (Items.Num() == 0 || InDelta == 0)
	{
		return;
	}
	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	// A full wheel has no ends; a partial one does, and pretending otherwise is how a keyboard user
	// steps off the visible arc into the empty part of the ring.
	const bool bWrap = Active.SweepAngle >= 360.0f;
	const int32 Step = InDelta > 0 ? 1 : -1;

	int32 Candidate = HighlightedIndex;
	if (Candidate == INDEX_NONE)
	{
		// From nowhere, the first step lands on an end rather than beside an item that is not there.
		Candidate = Step > 0 ? -1 : Items.Num();
	}
	for (int32 Taken = 0; Taken < FMath::Abs(InDelta); ++Taken)
	{
		// One move, then keep going while it lands on something disabled -- bounded by the item
		// count so a ring of nothing but disabled items terminates.
		int32 Next = Candidate;
		for (int32 Guard = 0; Guard < Items.Num(); ++Guard)
		{
			Next += Step;
			if (Next < 0 || Next >= Items.Num())
			{
				if (!bWrap)
				{
					Next = INDEX_NONE;
					break;
				}
				Next = (Next + Items.Num()) % Items.Num();
			}
			if (Items[Next].bEnabled)
			{
				break;
			}
			if (Guard == Items.Num() - 1)
			{
				Next = INDEX_NONE;
			}
		}
		if (Next == INDEX_NONE)
		{
			// Nothing left in that direction: keep what is highlighted rather than clearing it,
			// which is what every list in the engine does at the end of a keyboard walk.
			return;
		}
		Candidate = Next;
	}
	SetHighlightedIndex(Candidate);
}

void UDreamRingMenu::SetSelectedIndex(int32 InIndex)
{
	const int32 Resolved = Items.IsValidIndex(InIndex) ? InIndex : INDEX_NONE;
	if (SelectedIndex == Resolved)
	{
		return;
	}
	SetSelectedIndexWithoutNotify(Resolved);
	OnSelectionChanged.Broadcast(SelectedIndex);
	OnValueChangedBP.Broadcast(SelectedIndex);
}

void UDreamRingMenu::SetSelectedIndexWithoutNotify(int32 InIndex)
{
	SelectedIndex = Items.IsValidIndex(InIndex) ? InIndex : INDEX_NONE;
	RefreshWedgeColors();
	RefreshHubText();
}

void UDreamRingMenu::HandleWedgeClicked(int32 InIndex)
{
	if (!Items.IsValidIndex(InIndex) || !Items[InIndex].bEnabled)
	{
		return;
	}
	ActivateItem(InIndex);
}

void UDreamRingMenu::ActivateHighlighted()
{
	if (Items.IsValidIndex(HighlightedIndex) && Items[HighlightedIndex].bEnabled)
	{
		ActivateItem(HighlightedIndex);
	}
}

void UDreamRingMenu::ActivateItem(int32 InIndex)
{
	if (!Items.IsValidIndex(InIndex))
	{
		return;
	}
	if (bAllowDeselect && SelectedIndex == InIndex)
	{
		SetSelectedIndex(INDEX_NONE);
	}
	else
	{
		SetSelectedIndex(InIndex);
	}
	// AFTER the selection moved, and unconditionally: a menu entry is a command, and choosing the
	// same one twice has to be sayable even though the selection did not change.
	OnItemActivated.Broadcast(InIndex, Items[InIndex].Tag);
}

void UDreamRingMenu::Open()
{
	if (!IsValid(RingNode))
	{
		return;
	}
	bOpen = true;
	RingNode->SetWidgetActive(true);

	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	const float Scale = FMath::Max(Active.OpenScaleFrom, 0.01f);
	bool bTweened = false;
	if (Active.OpenDuration > 0.0f)
	{
		RingNode->SetRenderOpacity(0.0f);
		// X is DEPTH in this framework's local space; a UI scale is the other two.
		RingNode->SetRelativeScale(FVector(1.0, Scale, Scale));
		UDreamTweener* Fade = RingNode->RenderOpacityTo(1.0f, Active.OpenDuration, 0.0f, EDreamTweenEase::OutCubic);
		UDreamTweener* Grow = RingNode->LocalScaleTo(FVector::OneVector, Active.OpenDuration, 0.0f, EDreamTweenEase::OutCubic);
		bTweened = (Fade != nullptr) && (Grow != nullptr);
	}
	if (!bTweened)
	{
		// The tween manager is a world subsystem and hands back null without one, which is every
		// headless test and every designer preview. Snapping to the END state is the only correct
		// fallback: leaving the start values written would be a ring that is open and invisible.
		RingNode->SetRenderOpacity(1.0f);
		RingNode->SetRelativeScale(FVector::OneVector);
	}
	OnOpened.Broadcast(SelectedIndex);
}

void UDreamRingMenu::Close()
{
	if (!IsValid(RingNode))
	{
		return;
	}
	bOpen = false;
	// Before the fade, not after: a menu on its way out must stop answering the pointer at once, or
	// the last frames of the animation are still clickable.
	SetHighlightedIndex(INDEX_NONE);

	const FDreamRingMenuStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::RingMenuStyle);
	const float Scale = FMath::Max(Active.OpenScaleFrom, 0.01f);
	bool bTweened = false;
	if (Active.OpenDuration > 0.0f)
	{
		UDreamTweener* Fade = RingNode->RenderOpacityTo(0.0f, Active.OpenDuration, 0.0f, EDreamTweenEase::OutCubic);
		UDreamTweener* Shrink = RingNode->LocalScaleTo(FVector(1.0, Scale, Scale), Active.OpenDuration, 0.0f, EDreamTweenEase::OutCubic);
		if (Fade != nullptr && Shrink != nullptr)
		{
			bTweened = true;
			// Weak, because the tween outlives nothing else here: a control destroyed mid-close
			// would otherwise be written to by a callback holding a raw pointer to it.
			TWeakObjectPtr<UDreamRingMenu> WeakThis(this);
			Fade->OnComplete(TFunction<void()>([WeakThis]()
			{
				UDreamRingMenu* Menu = WeakThis.Get();
				if (Menu != nullptr && !Menu->bOpen && IsValid(Menu->RingNode))
				{
					// Re-checked, because Open may have run while the fade was still going.
					Menu->RingNode->SetWidgetActive(false);
				}
			}));
		}
	}
	if (!bTweened)
	{
		RingNode->SetWidgetActive(false);
	}
	OnClosed.Broadcast(SelectedIndex);
}

void UDreamRingMenu::ToggleOpen()
{
	if (bOpen)
	{
		Close();
	}
	else
	{
		Open();
	}
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "RingMenu", UDreamRingMenu)
