// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamDropdown.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIDropdown.h"
#include "Interaction/UIToggle.h"

void UDreamDropdown::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// The shape UUIDropdown reads, and nothing else. CreateListItems duplicates the template under
	// the template's PARENT and derives the list's height from that column, so the column carries
	// the vertical box and the template is its only authored child -- inactive, never drawn, purely
	// the thing rows are copied from. Show() takes care of placing and animating ListRoot; here it
	// only has to exist, sized, and asleep.
	Realize(this,
		Image("Face").Out(FaceNode)
			.Stretch()
			.With<UDreamLayoutContainerOverlay>()
			.With<UUIDropdown>()
			.Children(
				DreamUI::Text("Caption").Out(CaptionNode)
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
				DreamUI::Text("Arrow").Out(ArrowNode)
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
				Image("ListRoot").Out(ListNode)
					.Anchors(FVector2D(0.0, 0.0), FVector2D(1.0, 0.0))
					.Children(
						Widget("Column")
							.Stretch()
							.With<UDreamLayoutContainerVerticalBox>()
							.Children(
								Image("ItemTemplate").Out(ItemTemplateNode)
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
										Image("ItemCheck")
											.Slot([](UDreamPanelSlot& InSlot)
											{
												InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
												InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
												InSlot.SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
											})))))
			.Then([this](UDreamWidget& InRoot)
			{
				DropdownBehaviour = InRoot.GetComponent<UUIDropdown>();
				if (DropdownBehaviour == nullptr)
				{
					return;
				}
				DropdownBehaviour->SetTransitionTarget(InRoot.GetVisual());
				DropdownBehaviour->SetListRoot(ListNode);
				DropdownBehaviour->SetCaptionText(CaptionNode != nullptr ? Cast<UDreamText>(CaptionNode->GetVisual()) : nullptr);

				if (ItemTemplateNode != nullptr)
				{
					UUIDropdownItemComponent* Item = ItemTemplateNode->GetComponent<UUIDropdownItemComponent>();
					UUIToggle* ItemToggle = ItemTemplateNode->GetComponent<UUIToggle>();
					UDreamWidget* Check = nullptr;
					for (UDreamWidget* Child : ItemTemplateNode->GetChildren())
					{
						if (Child != nullptr && Child->GetDisplayName() == TEXT("ItemCheck"))
						{
							Check = Child;
						}
						else if (Child != nullptr && Child->GetDisplayName() == TEXT("ItemLabel"))
						{
							if (Item != nullptr)
							{
								Item->SetText(Cast<UDreamText>(Child->GetVisual()));
							}
						}
					}
					if (Item != nullptr)
					{
						Item->SetToggle(ItemToggle);
					}
					if (ItemToggle != nullptr && Check != nullptr)
					{
						// The row's own pair of the library's recurring arrangement: hover tints the
						// row, the selection mark is its own visual.
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
				DropdownBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamDropdown::HandleValueChanged);
			}));

	ApplyStyle();
	PushOptions();
}

void UDreamDropdown::ApplyStyle()
{
	const FDreamDropdownStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::DropdownStyle);

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
	if (ListNode != nullptr)
	{
		// A starting height; the behaviour re-derives the real one from the built column when the
		// list opens, clamped to its max.
		ListNode->SetHeight(Active.MaxListHeight);
		ListNode->SetAnchoredPosition(FVector2D(0.0, -Active.MaxListHeight * 0.5));
	}

	if (ItemTemplateNode != nullptr)
	{
		// Through the brush: the template lives in a vertical box, and a stacked child's height is
		// read off its visual's preferred size, not off SetHeight.
		if (UDreamImage* RowImage = Cast<UDreamImage>(ItemTemplateNode->GetVisual()))
		{
			FDreamUIImageBrush Brush = RowImage->GetBrush();
			Brush.ImageSize = FVector2f(100.0f, Active.ItemHeight);
			RowImage->SetBrush(Brush);
		}
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
				if (UDreamImage* CheckImage = Cast<UDreamImage>(Child->GetVisual()))
				{
					FDreamUIImageBrush Brush = CheckImage->GetBrush();
					Brush.ImageSize = FVector2f(10.0f, 10.0f);
					CheckImage->SetBrush(Brush);
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
		DropdownBehaviour->SetMaxHeight(Active.MaxListHeight);
	}
	SetHeight(Active.Height);
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

void UDreamDropdown::HandleValueChanged(int32 InIndex)
{
	SelectedIndex = InIndex;
	OnSelectionChanged.Broadcast(InIndex);
}
