// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexWidgetDetailPropertyExtensionHandler.h"

#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "LexWidgetHierarchyPickerView.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexWidgetSubObjectBehaviour.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "LexWidgetDetailPropertyExtensionHandler"

FLexWidgetDetailPropertyExtensionHandler::FLexWidgetDetailPropertyExtensionHandler(UWorld* InWorld)
{
	World = InWorld;
}

bool FLexWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(const FProperty* InProperty)
{
	auto ObjectProperty = CastField<FObjectPropertyBase>(InProperty);
	if (!ObjectProperty)return false;
	if (CastField<FClassProperty>(ObjectProperty) != nullptr)return false;//skip class property
	auto ObjectClass = ObjectProperty->PropertyClass;
	if (ObjectClass == nullptr)return false;
	if (!ObjectClass->IsChildOf(ULexWidget::StaticClass())
		&& !ObjectClass->IsChildOf(ULexWidgetSubObjectBehaviour::StaticClass())
		&& !ObjectClass->IsChildOf(ULexUIBehaviour::StaticClass())
		)return false;
	//an instanced sub-object is owned by the property, not referenced across the hierarchy
	if (ObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance))return false;
	return true;
}

bool FLexWidgetDetailPropertyExtensionHandler::IsPropertyExtendable(const UClass* ObjectClass, const IPropertyHandle& PropertyHandle) const
{
	return IsWidgetReferenceProperty(PropertyHandle.GetProperty());
}

void FLexWidgetDetailPropertyExtensionHandler::ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass,	TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	InDetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() != 1)return;
	if (!ObjectsBeingCustomized[0].IsValid())return;
	if (!IsWidgetReferenceProperty(InPropertyHandle->GetProperty()))return;
	auto ObjectClass = CastField<FObjectPropertyBase>(InPropertyHandle->GetProperty())->PropertyClass;
	UObject* Object = nullptr;
	if (InPropertyHandle->GetValue(Object) != FPropertyAccess::Success)return;
	auto WeakObject = MakeWeakObjectPtr(Object);
	InPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([&InDetailBuilder]()
	{
		InDetailBuilder.GetPropertyUtilities()->RequestForceRefresh();
	}));

	auto NoneObjectText = LOCTEXT("None", "None");
	auto GetTooltipText = [=, this]()
	{
		if (!WeakObject.IsValid())return NoneObjectText;
		ULexWidget* Widget = nullptr;
		FString PathStr;
		if (auto CastWidget = Cast<ULexWidget>(WeakObject.Get()))
		{
			Widget = CastWidget;
		}
		else
		{
			Widget = WeakObject->GetTypedOuter<ULexWidget>();
			if (!IsValid(Widget))return NoneObjectText;
			if (!Widget->HasRegistered() && !Widget->GetParent() && !Widget->GetRenderCanvas())//could be destroyed in editor
			{
				PathStr = "None." + WeakObject->GetPathName(Widget);
			}
			else
			{
				PathStr = "." + WeakObject->GetPathName(Widget);
			}
		}
		while (Widget && !Widget->IsRootWidgetInHierarchy())
		{
			PathStr = "/" + Widget->GetDisplayName() + PathStr;
			Widget = Widget->GetParent();
		}
		return FText::FromString(PathStr);
	};

	// The picker rides the extension slot rather than replacing ValueContent, so the stock object row
	// -- Browse, Use-selected and clear -- is still there. It is the only widget in the row that can
	// reach a target by hierarchy path, so it has to survive the menu closing itself: the button is
	// held by a box the OnSelectItem lambda shares, not by the handler, which serves every row at once
	// and would otherwise close whichever row happened to be built last.
	TSharedRef<TSharedPtr<SComboButton>> PickerButton = MakeShared<TSharedPtr<SComboButton>>();
	InWidgetRow.ExtensionContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.IsEnabled_Lambda([=]()
		{
			return InPropertyHandle->IsEditable();
		})
		[
			SAssignNew(*PickerButton, SComboButton)
			.HasDownArrow(false)
			.ContentPadding(FMargin(2, 0))
			.ToolTipText_Lambda(GetTooltipText)
			.ButtonContent()
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FSlateIconFinder::FindIconBrushForClass(ULexWidget::StaticClass()))
			]
			.MenuContent()
			[
				SNew(SBox)
				.Padding(4, 4)
				[
					SNew(SLexWidgetHierarchyPickerView, World.Get(), ObjectClass)
					.OnSelectItem_Lambda([=, this](UObject* InItem)
					{
						InPropertyHandle->SetValueFromFormattedString(InItem->GetPathName());
						(*PickerButton)->SetIsOpen(false);
					})
				]
			]
		]
	]
	;
}

#undef LOCTEXT_NAMESPACE
