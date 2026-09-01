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
				if (UDreamUserWidget* Content = CreateDreamWidget(GetWorld(), ItemTemplateClass, InItem))
				{
					Content->SetDisplayName(TEXT("ItemContent"));
					if (UDreamPanelSlot* ContentSlot = Content->GetPanelSlot())
					{
						ContentSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						ContentSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					}
				}
				// The stock label and the supplied content are two answers to what is on this row --
				// the same rule the content slots follow, and the same one the list's rows follow.
				if (UDreamWidget* ItemLabel = InItem->FindChildByDisplayName(TEXT("ItemLabel"), true))
				{
					ItemLabel->SetWidgetActive(false);
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
	// The template only; duplicated rows copy it, and every options push rebuilds them from it.
	SkinFace(ItemTemplateNode, Active.ItemBrush);

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

	if (UDreamVisual* ListVisual = ListNode != nullptr ? ListNode->GetVisual() : nullptr)
	{
		ListVisual->SetColor(Active.ListBackground);
	}
	if (!bListElevated)
	{
		ApplyListRestingGeometry(Active);
	}

	if (ItemTemplateNode != nullptr)
	{
		// A rect block states no size of its own; the authored height feeds the column's desired-size
		// fallback, and the duplicated rows inherit the slot snapshot.
		ItemTemplateNode->SetHeight(Active.ItemHeight);
		for (UDreamWidget* Child : ItemTemplateNode->GetChildren())
		{
			if (Child == nullptr)
			{
				continue;
			}
			if (Child->GetDisplayName() == TEXT("ItemLabel"))
			{
				TintText(Child, Active.TextColor);
			}
			else if (Child->GetDisplayName() == TEXT("ItemCheck"))
			{
				if (UDreamText* CheckText = Cast<UDreamText>(Child->GetVisual()))
				{
					// Colour comes from the toggle's checked transition; only the glyph size is style.
					CheckText->SetFontSize(Active.FontSize);
				}
			}
		}
		if (UUIToggle* ItemToggle = ItemTemplateNode->GetComponent<UUIToggle>())
		{
			ItemToggle->SetNormalColor(Active.ListBackground);
			ItemToggle->SetHoveredColor(Active.ItemHovered);
			ItemToggle->SetPressedColor(Active.FacePressed);
			ItemToggle->SetOnColor(Active.CheckColor);
			ItemToggle->SetOffColor(FColor(Active.CheckColor.R, Active.CheckColor.G, Active.CheckColor.B, 0));
		}
	}

	if (DropdownBehaviour != nullptr)
	{
		DropdownBehaviour->SetNormalColor(Active.FaceNormal);
		DropdownBehaviour->SetHoveredColor(Active.FaceHovered);
		DropdownBehaviour->SetPressedColor(Active.FacePressed);
		DropdownBehaviour->SetMaxHeight(MaxVisibleItems * Active.ItemHeight);
	}
	SizeControlHeight(Active.Height);
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
