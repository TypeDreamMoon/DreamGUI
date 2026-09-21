// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamWidgetDetailPropertyExtensionHandler.h"

#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "DreamWidgetHierarchyPickerView.h"
#include "DreamWidgetPropertyBindingExtension.h"
#include "DreamWidgetBlueprintEditor.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetSubObjectBehaviour.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDetailPropertyExtensionHandler"

FDreamWidgetDetailPropertyExtensionHandler::FDreamWidgetDetailPropertyExtensionHandler(UWorld* InWorld)
{
	World = InWorld;
}

bool FDreamWidgetDetailPropertyExtensionHandler::IsWidgetReferenceProperty(const FProperty* InProperty)
{
	auto ObjectProperty = CastField<FObjectPropertyBase>(InProperty);
	if (!ObjectProperty)return false;
	if (CastField<FClassProperty>(ObjectProperty) != nullptr)return false;//skip class property
	auto ObjectClass = ObjectProperty->PropertyClass;
	if (ObjectClass == nullptr)return false;
	if (!ObjectClass->IsChildOf(UDreamWidget::StaticClass())
		&& !ObjectClass->IsChildOf(UDreamWidgetSubObjectBehaviour::StaticClass())
		&& !ObjectClass->IsChildOf(UDreamUIBehaviour::StaticClass())
		)return false;
	//an instanced sub-object is owned by the property, not referenced across the hierarchy
	if (ObjectProperty->HasAnyPropertyFlags(CPF_PersistentInstance))return false;
	return true;
}

bool FDreamWidgetDetailPropertyExtensionHandler::IsPropertyExtendable(const UClass* ObjectClass, const IPropertyHandle& PropertyHandle) const
{
	if (IsWidgetReferenceProperty(PropertyHandle.GetProperty()))
	{
		return true;
	}
	// The other tenant of this slot: a property a binding could drive. Asked about every row in the
	// panel, so it answers with the same rule the row builder does -- a bare true makes the property
	// editor allocate an extension slot for rows that will never use one.
	TArray<UObject*> Outers;
	PropertyHandle.GetOuterObjects(Outers);
	return Outers.Num() == 1
		&& DreamWidgetPropertyBindingExtension::IsBindable(Outers[0], PropertyHandle.GetProperty());
}

void FDreamWidgetDetailPropertyExtensionHandler::ExtendWidgetRow(FDetailWidgetRow& InWidgetRow, const IDetailLayoutBuilder& InDetailBuilder, const UClass* InObjectClass,	TSharedPtr<IPropertyHandle> InPropertyHandle)
{
	if (!InPropertyHandle.IsValid())return;
	// The row's OWN object, which is not the panel's. Panel / Self Layout / Visual come in through
	// AddExternalObjects and a panel slot through AddExternalObjectProperty, so for every one of those
	// rows GetObjectsBeingCustomized answers "the widget" while the property lives on the layout, the
	// visual or the slot -- and asking the widget for a setter it does not have took the Bind control
	// off exactly the rows that most wanted one. IsPropertyExtendable already answers from the
	// handle's outers; this is the same question asked the same way, so the two cannot disagree.
	TArray<UObject*> Outers;
	InPropertyHandle->GetOuterObjects(Outers);
	if (Outers.Num() != 1)return;
	UObject* Owner = Outers[0];
	if (!IsValid(Owner))return;

	if (!IsWidgetReferenceProperty(InPropertyHandle->GetProperty()))
	{
		// The binding row draws its own control; the picker below is only for reference properties.
		if (auto Designer = FDreamWidgetBlueprintEditor::GetEditorByWorld(World.Get()).Pin())
		{
			if (TSharedPtr<SWidget> BindingWidget = DreamWidgetPropertyBindingExtension::MakeBindingWidget(
				Designer.Get(), Owner, InPropertyHandle))
			{
				InWidgetRow.ExtensionContent()
				[
					BindingWidget.ToSharedRef()
				];
			}
		}
		return;
	}
	auto ObjectClass = CastField<FObjectPropertyBase>(InPropertyHandle->GetProperty())->PropertyClass;
	UObject* Object = nullptr;
	if (InPropertyHandle->GetValue(Object) != FPropertyAccess::Success)return;
	auto WeakObject = MakeWeakObjectPtr(Object);
	// The UTILITIES, weakly -- not the layout builder by reference. This delegate is held by the
	// property node, and a forced refresh is precisely what destroys the layout builder that installed
	// it, so the capture would be dangling by the second time anyone changed the value. The utilities
	// are shared and are what actually services the request.
	TWeakPtr<IPropertyUtilities> WeakUtilities = InDetailBuilder.GetPropertyUtilities();
	InPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakUtilities]()
	{
		if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
		{
			Utilities->RequestForceRefresh();
		}
	}));

	auto NoneObjectText = LOCTEXT("None", "None");
	auto GetTooltipText = [=, this]()
	{
		if (!WeakObject.IsValid())return NoneObjectText;
		UDreamWidget* Widget = nullptr;
		FString PathStr;
		if (auto CastWidget = Cast<UDreamWidget>(WeakObject.Get()))
		{
			Widget = CastWidget;
		}
		else
		{
			Widget = WeakObject->GetTypedOuter<UDreamWidget>();
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
	//
	// Nothing sets the enabled state here: SDetailSingleItemRow assigns the row's own enabled
	// attribute onto whatever this returns, so any attribute set on it is overwritten before it is
	// ever read.
	TSharedRef<TSharedPtr<SComboButton>> PickerButton = MakeShared<TSharedPtr<SComboButton>>();
	InWidgetRow.ExtensionContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SAssignNew(*PickerButton, SComboButton)
			.HasDownArrow(false)
			.ContentPadding(FMargin(2, 0))
			.ToolTipText_Lambda(GetTooltipText)
			.ButtonContent()
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image(FSlateIconFinder::FindIconBrushForClass(UDreamWidget::StaticClass()))
			]
			.MenuContent()
			[
				SNew(SBox)
				.Padding(4, 4)
				[
					SNew(SDreamWidgetHierarchyPickerView, World.Get(), ObjectClass)
					.OnSelectItem_Lambda([=, this](UObject* InItem)
					{
						// The menu entries hold weak references and are built once, so a widget destroyed
						// between opening the menu and clicking it answers null here. Closing the menu
						// without writing is the whole of what that click can honestly mean.
						if (InItem != nullptr)
						{
							InPropertyHandle->SetValueFromFormattedString(InItem->GetPathName());
						}
						(*PickerButton)->SetIsOpen(false);
					})
				]
			]
		]
	]
	;
}

#undef LOCTEXT_NAMESPACE
