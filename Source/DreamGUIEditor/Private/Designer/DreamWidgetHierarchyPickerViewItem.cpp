// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamWidgetHierarchyPickerViewItem.h"
#include "Styling/CoreStyle.h"
#include "DreamWidgetBlueprintEditor.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "DreamGUIEditorModule.h"
#include "DreamGUIEditorStyle.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "DreamWidgetHierarchyPickerViewItem"

class SHoverBox : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SHoverBox) {}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_EVENT(FSimpleDelegate, OnMouseEnter)
	SLATE_EVENT(FSimpleDelegate, OnMouseLeave)
SLATE_END_ARGS()

void Construct(const FArguments& InArgs)
	{
		OnMouseEnterDelegate = InArgs._OnMouseEnter;
		OnMouseLeaveDelegate = InArgs._OnMouseLeave;

		ChildSlot
		[
			InArgs._Content.Widget
		];
	}

	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (OnMouseEnterDelegate.IsBound())
			OnMouseEnterDelegate.Execute();
	}

	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override
	{
		if (OnMouseLeaveDelegate.IsBound())
			OnMouseLeaveDelegate.Execute();
	}

private:
	FSimpleDelegate OnMouseEnterDelegate;
	FSimpleDelegate OnMouseLeaveDelegate;
};

void SDreamWidgetHierarchyPickerViewItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SDreamWidgetHierarchyPickerView::DataType InModel
	, UClass* InObjectClass)
{
	Model = InModel;

	MenuBuilder = new FMenuBuilder(true, NULL, TSharedPtr<FExtender>(), false, &FCoreStyle::Get(), false);
	// A weak pointer, and the tree it came from is rebuilt on a timer -- so a row can be generated
	// for an item whose widget has already gone. Every dereference below has to survive that.
	const TWeakObjectPtr<UDreamWidget> Widget = Model.IsValid() ? Model->Widget : TWeakObjectPtr<UDreamWidget>();
	MenuBuilder->BeginSection("WidgetSection", LOCTEXT("WidgetMenu", "Widget"));
	if (Widget.IsValid())
	{
		if (Widget->IsA(InObjectClass))
		{
			MenuBuilder->AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Widget->GetDisplayName(), *Widget->GetClass()->GetName())), FText::GetEmpty(), FSlateIconFinder::FindIconForClass(Widget->GetClass())
				, FUIAction(FExecuteAction::CreateLambda([=]()
				{
					InArgs._OnSelectObject.ExecuteIfBound(Widget.Get());
				})));
		}
		TArray<UObject*> SubObjects;
		ForEachObjectWithOuter(Widget.Get(), [&](UObject* SubObject)
		{
			if (SubObject->IsA(InObjectClass)
				&& !SubObject->IsA<UDreamUIBehaviour>()//Component is handled below
				)
			{
				SubObjects.Add(SubObject);
			}
		}, false);
		if (SubObjects.Num() > 0)
		{
			MenuBuilder->AddSubMenu(
				FText::FromString(FString::Printf(TEXT("%s (%s)"), *Widget->GetDisplayName(), *Widget->GetClass()->GetName())),
				FText::GetEmpty(), FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
				{
					for (UObject* Object : SubObjects)
					{
						SubMenuBuilder.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Object->GetName(), *Object->GetClass()->GetName())), FText::GetEmpty(), FSlateIconFinder::FindIconForClass(Object->GetClass())
						, FUIAction(FExecuteAction::CreateLambda([=]()
						{
							InArgs._OnSelectObject.ExecuteIfBound(Object);
						})));
					}
				}), false, FSlateIconFinder::FindIconForClass(Widget->GetClass()));
		}
	}
	MenuBuilder->EndSection();
	MenuBuilder->BeginSection("ComponentsSection", LOCTEXT("ComponentsMenu", "Components"));
	const TArray<UDreamUIBehaviour*> Components = Widget.IsValid() ? Widget->GetAllComponents() : TArray<UDreamUIBehaviour*>();
	for (UDreamUIBehaviour* Component : Components)
	{
		if (!IsValid(Component))
		{
			continue;
		}
		if (Component->IsA(InObjectClass))
		{
			// A weak pointer in the action, not the raw one: the menu is built here and executed
			// when the author picks a line, and a preview rebuild in between frees the component.
			const TWeakObjectPtr<UDreamUIBehaviour> WeakComponent = Component;
			MenuBuilder->AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Component->GetName(), *Component->GetClass()->GetName())), FText::GetEmpty(), FSlateIconFinder::FindIconForClass(Component->GetClass())
				, FUIAction(FExecuteAction::CreateLambda([=]()
				{
					if (WeakComponent.IsValid())
					{
						InArgs._OnSelectObject.ExecuteIfBound(WeakComponent.Get());
					}
				})));
		}
		TArray<UObject*> SubObjects;
		ForEachObjectWithOuter(Component, [&](UObject* SubObject)
		{
			if (SubObject->IsA(InObjectClass))
			{
				SubObjects.Add(SubObject);
			}
		}, false);
		if (SubObjects.Num() > 0)
		{
			MenuBuilder->AddSubMenu(
				FText::FromString(FString::Printf(TEXT("%s (%s)"), *Component->GetName(), *Component->GetClass()->GetName())),
				FText::GetEmpty(), FNewMenuDelegate::CreateLambda([=](FMenuBuilder& SubMenuBuilder)
				{
					for (UObject* Object : SubObjects)
					{
						SubMenuBuilder.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Object->GetName(), *Object->GetClass()->GetName())), FText::GetEmpty(), FSlateIconFinder::FindIconForClass(Object->GetClass())
						, FUIAction(FExecuteAction::CreateLambda([=]()
						{
							InArgs._OnSelectObject.ExecuteIfBound(Object);
						})));
					}
				}), false, FSlateIconFinder::FindIconForClass(Component->GetClass()));
		}
	}
	MenuBuilder->EndSection();

	STableRow<SDreamWidgetHierarchyPickerView::DataType>::Construct(
		STableRow<SDreamWidgetHierarchyPickerView::DataType>::FArguments()
		.Padding(0.0f)
		.Content()
		[
			SAssignNew(MenuAnchor, SMenuAnchor)
			.Placement(MenuPlacement_MenuRight)
			[
				SNew(SHoverBox)
				.OnMouseEnter_Lambda([=, this]()
				{
					if (!MenuAnchor->IsOpen())
					{
						MenuAnchor->SetIsOpen(true, false);
					}
				})
				.OnMouseLeave_Lambda([=, this]()
				{
					
				})
				[
					SNew(SHorizontalBox)
					// Widget icon
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0)
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.DesiredSizeOverride(FVector2D(16, 16))
						.Image_Lambda([=, this]()
						{
							return FDreamGUIEditorModule::Get().GetWidgetIconBrush(Widget.Get());
						})
					]

					// Canvas
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(2, 0)
					[
						SNew(SBox)
						.Visibility_Lambda([=, this]()
						{
							if (Widget.IsValid() && Widget->IsCanvasWidget())
							{
								return EVisibility::Visible;
							}
							return EVisibility::Collapsed;
						})
						[
							SNew(SBox)
							.WidthOverride(16)
							.HeightOverride(16)
							.Padding(FMargin(0))
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								SNew(SImage)
								.Image(FDreamGUIEditorStyle::Get().GetBrush("CanvasMark"))
								.Visibility_Lambda([=, this]()
								{
									if (Widget.IsValid() && Widget->IsCanvasWidget())
									{
										return EVisibility::Visible;
									}
									return EVisibility::Hidden;
								})
								.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.4f))
							]
						]
					]			

					// Name of the widget
					+SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(2, 0, 0, 0)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text_Lambda([=]()
						{
							return FText::FromString(InModel->DisplayText);
						})
					]

					// Arrow
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew( SBox )
						.Visibility_Lambda([=, this]()
						{
							return Model->ValidObjectArray.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed; 
						})
						.Padding(FMargin(7,0,0,0))
						[
							SNew( SImage )
							.Image( FAppStyle::Get().GetBrush( "Menu.SubMenuIndicator" ) )
						]
					]
				]
			]
			.OnGetMenuContent_Lambda([this]()
			{
				return MenuBuilder->MakeWidget();
			})
		],
		InOwnerTableView);
}

SDreamWidgetHierarchyPickerViewItem::~SDreamWidgetHierarchyPickerViewItem()
{
	delete MenuBuilder;
}

#undef LOCTEXT_NAMESPACE
