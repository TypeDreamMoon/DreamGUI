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
#include "Widget/SDreamUISwatches.h"
#include "ScopedTransaction.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDetailPropertyExtensionHandler"

namespace DreamWidgetDetailPropertyExtensionLocal
{
	/** The row's colour in linear light, an FColor read as sRGB. */
	bool ReadColor(const TSharedPtr<IPropertyHandle>& InHandle, FLinearColor& OutColor)
	{
		void* Data = nullptr;
		if (!InHandle.IsValid() || InHandle->GetValueData(Data) != FPropertyAccess::Success || Data == nullptr)
		{
			return false;
		}
		const FStructProperty* AsStruct = CastField<FStructProperty>(InHandle->GetProperty());
		OutColor = AsStruct->Struct == TBaseStructure<FColor>::Get()
			? FLinearColor::FromSRGBColor(*static_cast<const FColor*>(Data))
			: *static_cast<const FLinearColor*>(Data);
		return true;
	}

	/** The eyedropper and the project's palette beside a colour row, writing through the row's handle. */
	TSharedRef<SWidget> MakeColorQuickPick(const TSharedPtr<IPropertyHandle>& InHandle)
	{
		return SNew(SDreamUIColorQuickPick)
			.Color_Lambda([InHandle]()
			{
				FLinearColor Color = FLinearColor::White;
				ReadColor(InHandle, Color);
				return Color;
			})
			.OnColorCommitted_Lambda([InHandle](const FLinearColor& InColor)
			{
				FDreamWidgetDetailPropertyExtensionHandler::WritePickedColor(InHandle, InColor);
			});
	}
}

bool FDreamWidgetDetailPropertyExtensionHandler::IsColorProperty(const FProperty* InProperty)
{
	const FStructProperty* AsStruct = CastField<FStructProperty>(InProperty);
	return AsStruct != nullptr && InProperty->ArrayDim == 1
		&& (AsStruct->Struct == TBaseStructure<FLinearColor>::Get() || AsStruct->Struct == TBaseStructure<FColor>::Get());
}

bool FDreamWidgetDetailPropertyExtensionHandler::WritePickedColor(const TSharedPtr<IPropertyHandle>& InHandle, const FLinearColor& InColor)
{
	FLinearColor Current = FLinearColor::White;
	if (!InHandle.IsValid() || !IsColorProperty(InHandle->GetProperty())
		|| !DreamWidgetDetailPropertyExtensionLocal::ReadColor(InHandle, Current))
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("PickColour", "Pick Colour"));
	const FStructProperty* AsStruct = CastField<FStructProperty>(InHandle->GetProperty());
	FPropertyAccess::Result Result = FPropertyAccess::Fail;
	if (AsStruct->Struct == TBaseStructure<FColor>::Get())
	{
		FColor Picked = InColor.ToFColorSRGB();
		Picked.A = Current.ToFColorSRGB().A;
		Result = InHandle->SetValueFromFormattedString(FString::Printf(TEXT("(B=%d,G=%d,R=%d,A=%d)"), Picked.B, Picked.G, Picked.R, Picked.A));
	}
	else
	{
		FLinearColor Picked = InColor;
		Picked.A = Current.A;
		Result = InHandle->SetValueFromFormattedString(Picked.ToString());
	}
	return Result == FPropertyAccess::Success;
}

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
	if (IsWidgetReferenceProperty(PropertyHandle.GetProperty())
		|| IsColorProperty(PropertyHandle.GetProperty()))
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
		TSharedPtr<SWidget> BindingWidget;
		if (auto Designer = FDreamWidgetBlueprintEditor::GetEditorByWorld(World.Get()).Pin())
		{
			BindingWidget = DreamWidgetPropertyBindingExtension::MakeBindingWidget(Designer.Get(), Owner, InPropertyHandle);
		}
		// A colour row shares the slot: the eyedropper and the palette first, the Bind control after them.
		if (IsColorProperty(InPropertyHandle->GetProperty()))
		{
			TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				DreamWidgetDetailPropertyExtensionLocal::MakeColorQuickPick(InPropertyHandle)
			];
			if (BindingWidget.IsValid())
			{
				Box->AddSlot().AutoWidth().VAlign(VAlign_Center)
				[
					BindingWidget.ToSharedRef()
				];
			}
			InWidgetRow.ExtensionContent()
			[
				Box
			];
		}
		else if (BindingWidget.IsValid())
		{
			InWidgetRow.ExtensionContent()
			[
				BindingWidget.ToSharedRef()
			];
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
