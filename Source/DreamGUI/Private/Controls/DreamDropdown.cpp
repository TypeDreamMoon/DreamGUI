// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamDropdown.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UIDropdown.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UIToggle.h"

void UDreamDropdown::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Face"), FaceNode);
	OutParts.Emplace(TEXT("Caption"), CaptionNode);
	OutParts.Emplace(TEXT("ListRoot"), ListNode);
	OutParts.Emplace(TEXT("ItemTemplate"), ItemTemplateNode);
	// The glyph is scenery: a template that draws its own arrow, or none, is still a dropdown.
	OutParts.Emplace(TEXT("Arrow"), ArrowNode, /*bRequired*/false);
}

void UDreamDropdown::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The shape UUIDropdown reads, and nothing else. CreateListItems duplicates the template under
	// the template's PARENT and derives the list's height from that column, so the column carries
	// the vertical box and the template is its only authored child -- inactive, never drawn, purely
	// the thing rows are copied from. Show() takes care of placing and animating ListRoot; here it
	// only has to exist, sized, and asleep.
	Realize(this,
		Node<UDreamRectBlock>("Face")
			.Stretch()
			.With<UDreamLayoutContainerOverlay>()
			.Children(
				DreamUI::Text("Caption")
					.Visual([](UDreamText& InText)
					{
						InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
						InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
						InSlot.SetPadding(FMargin(10.0f, 0.0f, 24.0f, 0.0f));
					}),
				DreamUI::Text("Arrow")
					.Visual([](UDreamText& InText)
					{
						InText.SetText(FText::AsCultureInvariant(TEXT("▼")));
						InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
						InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
						InSlot.SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
					}),
				Node<UDreamRectBlock>("ListRoot")
					// POINT anchors, deliberately: the list is inactive between opens, and an
					// inactive widget's stretched axis never re-arranges -- its cached width is
					// stale (zero, after an elevation round-trip), and Elevate pins whatever it
					// finds. Absolute sizes have no cache to go stale; the open handler writes
					// the face's live width in.
					.Anchors(FVector2D(0.5, 0.0), FVector2D(0.5, 0.0))
					// A popup, the way UMG's combo list is: it stays in the tree so Show() can
					// position it against the face, but layout must not see it -- an Auto-sized row
					// otherwise grows by the list's height the moment it opens and shoves the rest
					// of the screen down.
					.Self([](UDreamWidget& InList)
					{
						InList.SetIgnoreLayout(true);
						// The viewport of the scroll below; without the clip, rows past the visible
						// count draw over whatever is under the list.
						InList.SetClipping(EDreamWidgetClipping::ClipToBounds);
					})
					// Vertical only, explicitly: the behaviour ships with BOTH axes on, and a
					// zero-config scroll view drifts horizontally the first time a drag lands.
					.With<UUIScrollView>([](UUIScrollView& InScroll)
					{
						InScroll.SetHorizontal(false);
						InScroll.SetVertical(true);
					})
					.Children(
						Widget("Column")
							// Top-anchored, stretch-X, its HEIGHT authored per open: the column is
							// the scrolled content, so it must be as tall as ALL rows while the
							// list shows only the visible count. Stretch would pin it to the list.
							.Anchors(FVector2D(0.0, 1.0), FVector2D(1.0, 1.0))
							.Self([](UDreamWidget& InColumn)
							{
								InColumn.SetPivot(FVector2D(0.5, 1.0));
								// The DELTA, not the width: SetWidth(0) on a stretched axis
								// computes a delta against the parent's span AT THIS MOMENT --
								// still the default 100 here -- and bakes -100 in forever (the
								// measured symptom: a 300-wide column in a 400-wide list). A
								// zero delta says "exactly the span", whenever it is decided.
								InColumn.SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D::ZeroVector);
							})
							.With<UDreamLayoutContainerVerticalBox>()
							.Children(
								Node<UDreamRectBlock>("ItemTemplate")
									.With<UDreamLayoutContainerOverlay>()
									.With<UUIToggle>()
									.With<UUIDropdownItemComponent>()
									.Children(
										DreamUI::Text("ItemLabel")
											.Visual([](UDreamText& InText)
											{
												InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
												InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
											})
											.Slot([](UDreamPanelSlot& InSlot)
											{
												InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
												InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
												InSlot.SetPadding(FMargin(10.0f, 0.0f, 24.0f, 0.0f));
											}),
										// The per-option picture. Asleep unless OptionIcons names one for
										// this row, which is why it can sit in every row's template
										// and cost a dropdown that has no icons nothing but a node.
										Node<UDreamRectBlock>("ItemIcon")
											.Self([](UDreamWidget& InIcon) { InIcon.SetWidgetActive(false); })
											.Slot([](UDreamPanelSlot& InSlot)
											{
												InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Left);
												InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
												InSlot.SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
											}),
										DreamUI::Text("ItemCheck")
											.Visual([](UDreamText& InText)
											{
												InText.SetText(FText::AsCultureInvariant(TEXT("✓")));
											})
											.Slot([](UDreamPanelSlot& InSlot)
											{
												InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
												InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
												InSlot.SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
											}))))));
}

void UDreamDropdown::WireParts()
{
	DropdownBehaviour = EnsureComponent<UUIDropdown>(FaceNode);
	if (DropdownBehaviour == nullptr || FaceNode == nullptr)
	{
		return;
	}
	DropdownBehaviour->SetTransitionTarget(FaceNode->GetVisual());
	DropdownBehaviour->SetListRoot(ListNode);
	if (UUIScrollView* Scroll = ListNode != nullptr ? ListNode->GetComponent<UUIScrollView>() : nullptr)
	{
		Scroll->SetContent(FindPart(TEXT("Column")));
	}
	DropdownBehaviour->SetCaptionText(CaptionNode != nullptr ? Cast<UDreamText>(CaptionNode->GetVisual()) : nullptr);

	if (ItemTemplateNode != nullptr)
	{
		// The row template's own parts, named as everything else is. FindPart stops at nested
		// instances but descends plain nodes, so these resolve wherever inside the template a
		// tree's author put them.
		UUIDropdownItemComponent* Item = EnsureComponent<UUIDropdownItemComponent>(ItemTemplateNode);
		UUIToggle* ItemToggle = EnsureComponent<UUIToggle>(ItemTemplateNode);
		UDreamWidget* Check = FindPart(TEXT("ItemCheck"));
		if (UDreamWidget* ItemLabel = FindPart(TEXT("ItemLabel")))
		{
			if (Item != nullptr)
			{
				Item->SetText(Cast<UDreamText>(ItemLabel->GetVisual()));
			}
		}
		if (Item != nullptr)
		{
			Item->SetToggle(ItemToggle);
		}
		if (ItemToggle != nullptr && Check != nullptr)
		{
			// The row's own pair of the library's recurring arrangement: hover tints the row, the
			// selection mark is its own visual.
			ItemToggle->SetTransitionTarget(ItemTemplateNode->GetVisual());
			ItemToggle->SetToggleTransitionTarget(Check->GetVisual());
		}
		DropdownBehaviour->SetItemTemplate(Item);
		// The template is the thing rows are copied from, not a row.
		ItemTemplateNode->SetWidgetActive(false);
	}
	// Asleep until Show(); Show() wakes it, positions it and fades it in.
	if (ListNode != nullptr)
	{
		ListNode->SetWidgetActive(false);
	}
	// The behaviour makes the rows (it owns the list's lifetime), and this is the seam it leaves for
	// whoever placed it: one call per created row, with the row widget. Everything this control adds
	// on top of a row -- an authored template inside it, and the consumer's event -- hangs here.
	DropdownBehaviour->SetItemCustomDataFunction(
		[this](int InIndex, UUIDropdownItemComponent*, UDreamWidget* InItem)
		{
			if (!IsValid(InItem))
			{
				return;
			}
			if (ItemTemplateClass != nullptr && GetWorld() != nullptr)
			{
				bool bHasAuthoredContent = false;
				if (UDreamUserWidget* Content = CreateDreamWidget(GetWorld(), ItemTemplateClass, InItem))
				{
					bHasAuthoredContent = true;
					Content->SetDisplayName(TEXT("ItemContent"));
					if (UDreamPanelSlot* ContentSlot = Content->GetPanelSlot())
					{
						ContentSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						ContentSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					}
				}
				// The stock label and the supplied content are two answers to what is on this row --
				// the same rule the content slots follow, and the same one the list's rows follow.
				// Gated on the content EXISTING rather than on having meant to make it: instancing
				// can fail (an abstract class, a class that would be its own template), and a row that
				// answered "a template drew this one" while holding nothing is a blank line in the
				// list. UDreamListViewBase::BindRow decides the same question the same way.
				if (bHasAuthoredContent)
				{
					if (UDreamWidget* ItemLabel = InItem->FindChildByDisplayName(TEXT("ItemLabel"), true))
					{
						ItemLabel->SetWidgetActive(false);
					}
				}
			}
			// The per-option picture, before the consumer's hook: a handler that wants to overrule it
			// should see it already placed rather than have it written over the top afterwards.
			if (UDreamWidget* IconNode = InItem->FindChildByDisplayName(TEXT("ItemIcon"), true))
			{
				UObject* Icon = OptionIcons.IsValidIndex(InIndex) ? OptionIcons[InIndex].Get() : nullptr;
				IconNode->SetWidgetActive(Icon != nullptr);
				if (Icon != nullptr)
				{
					// Through the family's own brush road, so an icon is a sprite or a texture by
					// exactly the rule every other face in this library follows -- and so an icon
					// inherits the item's corner radius rather than needing one of its own.
					FDreamUIFaceBrush IconBrush;
					IconBrush.Image = Icon;
					SkinFace(IconNode, IconBrush);
					const FDreamDropdownStyle& IconStyle = ResolveStyle(Style, &UDreamUIStyleSheet::DropdownStyle);
					SizeFace(IconNode, FVector2D(IconStyle.ItemHeight * 0.6, IconStyle.ItemHeight * 0.6));
				}
			}
			OnItemGenerated.Broadcast(InIndex, InItem);
		});
	DropdownBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamDropdown::HandleValueChanged);
	// The list is a child of the face for positioning and a citizen of the popup layer for
	// everything else: Show anchors it against the face, then the layer lifts it to the screen root
	// with its world position kept -- the UMG menu-stack arrangement -- so an ancestor's clip cannot
	// cut it and an ancestor's layout never counts it. Hide hands it home before the fade.
	DropdownBehaviour->GetOnListVisibilityChangedEvent().AddUObject(this, &UDreamDropdown::HandleListVisibilityChanged);
}

void UDreamDropdown::OnPartsReady()
{
	// The options and the selection are DATA, and live outside ApplyStyle -- which is why
	// PostEditChangeProperty has to call this alongside the style push rather than relying on it.
	// Either order works against the caption: the style push only tints that text, never writes it.
	PushOptions();
}

void UDreamDropdown::ApplyStyle()
{
	const FDreamDropdownStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::DropdownStyle);
	ShapeFace(FaceNode, Active.CornerRadius);
	ShapeFace(ListNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.FaceBrush);
	SkinFace(ListNode, Active.ListBrush);

	auto TintText = [&Active](UDreamWidget* InNode, const FColor& InColor)
	{
		if (UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr)
		{
			TextVisual->SetColor(InColor);
			TextVisual->SetFontSize(Active.FontSize);
		}
	};
	TintText(CaptionNode, Active.TextColor);
	TintText(ArrowNode, Active.ArrowColor);
	if (ArrowNode != nullptr)
	{
		// UMG's HasDownArrow: a combo box drawn without its glyph, for a face that says "open me"
		// some other way. The node stays in the tree either way, so turning it back on costs nothing
		// and a template that drew its own arrow is unaffected (it has no node of this name).
		ArrowNode->SetWidgetActive(bHasDownArrow);
	}

	if (UDreamVisual* ListVisual = ListNode != nullptr ? ListNode->GetVisual() : nullptr)
	{
		ListVisual->SetColor(Active.ListBackground);
	}
	if (!bListElevated)
	{
		ApplyListRestingGeometry(Active);
	}

	// The template AND every row already built from it. Re-styling only the template was correct
	// exactly once -- at initialize, before any row existed -- because rows are rebuilt from it on
	// every options push; but a RUNTIME restyle does not push options, so an open list (or a closed
	// one holding last open's rows) kept the old colours until the next SetOptions. The rows are
	// duplicates, so the same push has to reach each of them.
	PushItemStyle(ItemTemplateNode, Active);
	for (UDreamWidget* Row : GetItemRows())
	{
		PushItemStyle(Row, Active);
	}

	if (DropdownBehaviour != nullptr)
	{
		PushSelectableState(DropdownBehaviour, Active.FaceNormal, Active.FaceHovered, Active.FacePressed,
			Active.FaceDisabled, Active.FaceFocused, Active.TransitionDuration);
		DropdownBehaviour->SetMaxHeight(MaxVisibleItems * Active.ItemHeight);
	}
	SizeControlHeight(Active.Height);
}

TArray<UDreamWidget*> UDreamDropdown::GetItemRows() const
{
	TArray<UDreamWidget*> Rows;
	if (ListNode == nullptr)
	{
		return Rows;
	}
	// The rows the behaviour duplicated, which live beside the template in the scrolled column. Found
	// by walking rather than asked of UUIDropdown: the behaviour keeps its copies as item COMPONENTS
	// and the control's business here is with the widgets.
	for (UDreamWidget* Child : ListNode->GetChildren())
	{
		if (Child == nullptr || Child->GetDisplayName() != TEXT("Column"))
		{
			continue;
		}
		for (UDreamWidget* Row : Child->GetChildren())
		{
			if (IsValid(Row) && Row != ItemTemplateNode)
			{
				Rows.Add(Row);
			}
		}
	}
	return Rows;
}

void UDreamDropdown::PushItemStyle(UDreamWidget* InItem, const FDreamDropdownStyle& InActive)
{
	if (!IsValid(InItem))
	{
		return;
	}
	SkinFace(InItem, InActive.ItemBrush);
	// A rect block states no size of its own; the authored height feeds the column's desired-size
	// fallback, and the duplicated rows inherit the slot snapshot.
	InItem->SetHeight(InActive.ItemHeight);
	for (UDreamWidget* Child : InItem->GetChildren())
	{
		if (Child == nullptr)
		{
			continue;
		}
		if (Child->GetDisplayName() == TEXT("ItemLabel"))
		{
			if (UDreamText* LabelText = Cast<UDreamText>(Child->GetVisual()))
			{
				LabelText->SetColor(InActive.TextColor);
				LabelText->SetFontSize(InActive.FontSize);
			}
		}
		else if (Child->GetDisplayName() == TEXT("ItemCheck"))
		{
			if (UDreamText* CheckText = Cast<UDreamText>(Child->GetVisual()))
			{
				// Colour comes from the toggle's checked transition; only the glyph size is style.
				CheckText->SetFontSize(InActive.FontSize);
			}
		}
	}
	if (UUIToggle* ItemToggle = InItem->GetComponent<UUIToggle>())
	{
		// All five states and the speed, rather than the three pointer colours the rows used to get:
		// the two left out were a flat grey belonging to no theme and focus visuals that ship OFF, so
		// navigating an open list with a pad or the arrow keys lit nothing up at all. The check mark's
		// own pair (On/Off) is a different transition and stays beside it.
		PushSelectableState(ItemToggle, InActive.ListBackground, InActive.ItemHovered, InActive.FacePressed,
			InActive.ItemDisabled, InActive.ItemFocused, InActive.TransitionDuration);
		ItemToggle->SetOnColor(InActive.CheckColor);
		ItemToggle->SetOffColor(FColor(InActive.CheckColor.R, InActive.CheckColor.G, InActive.CheckColor.B, 0));
	}
}

int32 UDreamDropdown::GetSelectedIndex() const
{
	return DropdownBehaviour != nullptr ? DropdownBehaviour->GetValue() : SelectedIndex;
}

void UDreamDropdown::SetSelectedIndex(int32 InIndex)
{
	SelectedIndex = InIndex;
	if (DropdownBehaviour != nullptr)
	{
		DropdownBehaviour->SetValue(InIndex);
	}
}

void UDreamDropdown::SetOptions(const TArray<FText>& InOptions)
{
	Options = InOptions;
	PushOptions();
}

void UDreamDropdown::SetHasDownArrow(bool bInHasDownArrow)
{
	if (bHasDownArrow == bInHasDownArrow)
	{
		return;
	}
	bHasDownArrow = bInHasDownArrow;
	if (ArrowNode != nullptr)
	{
		// The one thing it decides, and nothing else: whether that node is awake.
		ArrowNode->SetWidgetActive(bHasDownArrow);
	}
}

void UDreamDropdown::SetOptionIcons(const TArray<UObject*>& InIcons)
{
	OptionIcons.Reset(InIcons.Num());
	for (UObject* Icon : InIcons)
	{
		OptionIcons.Add(Icon);
	}
	// The rows are built from the options and decorated from these together, so a new picture list is
	// a re-push of the list rather than a restyle.
	PushOptions();
}

void UDreamDropdown::SetMaxVisibleItems(int32 InMaxVisibleItems)
{
	const int32 Clamped = FMath::Max(1, InMaxVisibleItems);
	if (MaxVisibleItems == Clamped)
	{
		return;
	}
	MaxVisibleItems = Clamped;
	// The cap is spent in the style push, where a row COUNT becomes the open list's pixel height --
	// so writing the number without re-pushing left a list that still opened at the old height.
	ApplyStyle();
}

void UDreamDropdown::PushOptions()
{
	if (DropdownBehaviour == nullptr)
	{
		return;
	}
	TArray<FUIDropdownOptionData> Data;
	Data.Reserve(Options.Num());
	for (const FText& Option : Options)
	{
		FUIDropdownOptionData& Entry = Data.AddDefaulted_GetRef();
		Entry.Text = Option;
	}
	DropdownBehaviour->SetOptions(Data);
	DropdownBehaviour->SetValueWithoutNotify(SelectedIndex);
}

#if WITH_EDITOR
void UDreamDropdown::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// The base re-applies the style; the options and the selection live OUTSIDE ApplyStyle, so
	// without this re-push a details-panel edit of either is silently nothing until the next
	// initialize. Re-pushing unconditionally is fine: rows rebuild from the template either way.
	Super::PostEditChangeProperty(PropertyChangedEvent);
	PushOptions();
}
#endif

void UDreamDropdown::HandleListVisibilityChanged(bool bInVisible)
{
	// Broadcast FIRST and unconditionally -- before the popup-layer work below, and whether or not
	// there is a popup layer to do it in. This seam fires from the behaviour's Show and Hide, which
	// is the moment the list opens and closes; a consumer refreshing its options from OnOpening (the
	// reason UMG's combo box has the event) must be heard before the rows are placed, and a headless
	// test has no popup layer at all.
	if (bInVisible)
	{
		OnOpening.Broadcast();
	}
	else
	{
		OnClosed.Broadcast();
	}

	UDreamUIPopupLayer* Popup = UDreamUIPopupLayer::Get(this);
	if (Popup == nullptr || ListNode == nullptr)
	{
		return;
	}
	if (bInVisible)
	{
		bListElevated = true;
		// The control owns every height in the open list, because nothing else can. The list is
		// exactly visible-rows tall (past MaxVisibleItems the rest scroll -- the scroll view only
		// engages when the column outgrows it); the column is all-rows tall, the scrolled content.
		// The rows go through their SLOTS, not through authored heights: a row is an overlay whose
		// Auto measure is its TEXT's line height -- 19.7 for the default font, the measured symptom,
		// and no authored number ever wins against a content measure. Fill does: the column is
		// exactly rows*ItemHeight tall, so equal fill weights hand every row exactly ItemHeight.
		// All before the lift, which pins the height it finds.
		const FDreamDropdownStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::DropdownStyle);
		const int32 RowCount = FMath::Max(1, Options.Num());
		const int32 VisibleRows = FMath::Min(RowCount, FMath::Max(1, MaxVisibleItems));
		// The SCHEME first: Show()'s automatic placement thinks in the preset Blueprint's terms and
		// rewrites the pivot (measured: top-pivot 0.5,1) -- under which our centre-pivot position
		// maths hangs the list a full height below the face. Re-assert anchors and pivot, then
		// write this open's numbers over the resting ones.
		ApplyListRestingGeometry(Active);
		// Width explicitly, each open: the face is the one measurement that is always live.
		const float OpenHeight = VisibleRows * Active.ItemHeight;
		const float OpenWidth = FaceNode != nullptr ? static_cast<float>(FaceNode->GetWidth()) : static_cast<float>(ListNode->GetWidth());
		ListNode->SetAnchoredPositionAndSizeDelta(
			FVector2D(0.0, -OpenHeight * 0.5), FVector2D(OpenWidth, OpenHeight));
		for (UDreamWidget* Child : ListNode->GetChildren())
		{
			if (Child == nullptr || Child->GetDisplayName() != TEXT("Column"))
			{
				continue;
			}
			Child->SetHeight(RowCount * Active.ItemHeight);
			for (UDreamWidget* Row : Child->GetChildren())
			{
				if (Row == nullptr || Row == ItemTemplateNode)
				{
					continue;
				}
				if (UDreamPanelSlot* RowSlot = Row->GetPanelSlot())
				{
					RowSlot->SetSizeRule(EDreamPanelSizeRule::Fill);
					RowSlot->SetFillWeight(1.0f);
				}
			}
		}
		Popup->Elevate(ListNode);
	}
	else
	{
		Popup->Restore(ListNode);
		bListElevated = false;
		// The whole resting scheme, not just the numbers: Elevate re-anchored the list to a POINT
		// for the screen root and Restore reparents plainly, so without this the next open (and any
		// ApplyStyle in between) works against point anchors -- where a zero width delta is a zero
		// WIDTH. The measured symptom: a 0-wide list on the second open.
		ApplyListRestingGeometry(ResolveStyle(Style, &UDreamUIStyleSheet::DropdownStyle));
	}
}

void UDreamDropdown::ApplyListRestingGeometry(const FDreamDropdownStyle& InActive)
{
	if (ListNode == nullptr)
	{
		return;
	}
	// Hanging centred under the face's bottom edge, POINT-anchored on both axes: absolute numbers
	// only, because the list is inactive between opens and a stretched axis's cached width goes
	// stale there (see the tree comment). Width is nominal at rest -- the open handler writes the
	// face's live width; height is a sane resting value the behaviour reads as MaxHeight.
	const float RestingHeight = MaxVisibleItems * InActive.ItemHeight;
	ListNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.0), FVector2D(0.5, 0.0), false, false);
	ListNode->SetPivot(FVector2D(0.5, 0.5));
	ListNode->SetAnchoredPositionAndSizeDelta(
		FVector2D(0.0, -RestingHeight * 0.5), FVector2D(0.0, RestingHeight));
}

void UDreamDropdown::HandleValueChanged(int32 InIndex)
{
	SelectedIndex = InIndex;
	OnSelectionChanged.Broadcast(InIndex);
	OnValueChangedBP.Broadcast(InIndex);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Dropdown", UDreamDropdown)
