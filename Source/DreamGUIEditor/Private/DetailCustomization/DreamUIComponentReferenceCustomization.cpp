// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamUIComponentReferenceCustomization.h"
#include "EdGraphNode_Comment.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "DreamUIComponentReference.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "DreamGUIComponentRefereceHelperCustomization"

namespace DreamUIComponentReferenceCustomizationLocal
{
	/**
	 * A value the row FILLS IN on the author's behalf, not one the author picked.
	 *
	 * It goes in through the handle's own addresses -- resolved here and now, rather than the raw
	 * pointers cached when the header was built, which are only good for as long as nothing
	 * reallocates the container the struct lives in -- and deliberately without the notify pair:
	 * this runs while the row is being DRAWN, and a notified write there pushes an undo entry and
	 * dirties the package for merely looking at the property. A real pick goes through
	 * OnSelectComponent, which does announce itself.
	 */
	template<typename ValueType>
	void FillDerivedValue(const TSharedPtr<IPropertyHandle>& InHandle, const ValueType& InValue)
	{
		if (!InHandle.IsValid())
		{
			return;
		}
		TArray<void*> Addresses;
		InHandle->AccessRawData(Addresses);
		for (void* Address : Addresses)
		{
			if (Address != nullptr)
			{
				*static_cast<ValueType*>(Address) = InValue;
			}
		}
	}
}

TWeakObjectPtr<AActor> FDreamUIComponentReferenceCustomization::CopiedHelperActor;
TWeakObjectPtr<UActorComponent> FDreamUIComponentReferenceCustomization::CopiedTargetComp;
UClass* FDreamUIComponentReferenceCustomization::CopiedHelperClass;

static const FName NAME_AllowedClasses = "AllowedClasses";
static const FName NAME_DisallowedClasses = "DisallowedClasses";

TSharedRef<IPropertyTypeCustomization> FDreamUIComponentReferenceCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIComponentReferenceCustomization);
}
void FDreamUIComponentReferenceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	PropertyHandle = InPropertyHandle;
	PropertyUtilites = CustomizationUtils.GetPropertyUtilities();
	FCommentNodeSet NodeSet;
	PropertyHandle->GetOuterObjects(NodeSet);
	for (UObject* obj : NodeSet)
	{
		bIsInWorld = obj->GetWorld() != nullptr;
		break;
	}

	// copy all EventDelegate I'm accessing right now
	TArray<void*> StructPtrs;
	PropertyHandle->AccessRawData(StructPtrs);
	check(StructPtrs.Num() != 0);

	ComponentReferenceInstances.AddZeroed(StructPtrs.Num());
	for (auto Iter = StructPtrs.CreateIterator(); Iter; ++Iter)
	{
		check(*Iter);
		auto Item = (FDreamUIComponentReference*)(*Iter);
		ComponentReferenceInstances[Iter.GetIndex()] = Item;
		Item->CheckTargetObject();
	}

	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperActor));
	HelperActorHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamUIComponentReferenceCustomization::OnHelperActorValueChange));

	//ChildBuilder.AddProperty(TargetCompHandle.ToSharedRef());
	//ChildBuilder.AddProperty(HelperActorHandle.ToSharedRef());
	
	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(500)
	[
		SAssignNew(ContentWidgetBox, SBox)
	]
	.CopyAction(FUIAction
	(
		FExecuteAction::CreateSP(this, &FDreamUIComponentReferenceCustomization::OnCopy),
		FCanExecuteAction::CreateLambda([this] {return bIsInWorld; })
	))
	.PasteAction(FUIAction
	(
		FExecuteAction::CreateSP(this, &FDreamUIComponentReferenceCustomization::OnPaste),
		FCanExecuteAction::CreateLambda([this] {return bIsInWorld; })
	))
	.PropertyHandleList({ PropertyHandle })
	.OverrideResetToDefault(FResetToDefaultOverride::Create(
		FSimpleDelegate::CreateSP(this, &FDreamUIComponentReferenceCustomization::OnResetToDefaultClicked)
	))
	;
	BuildClassFilters();
	RegenerateContentWidget();
}
void FDreamUIComponentReferenceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	
}
void FDreamUIComponentReferenceCustomization::OnResetToDefaultClicked()
{
	PropertyHandle->ResetToDefault();
	RegenerateContentWidget();
}
void FDreamUIComponentReferenceCustomization::RegenerateContentWidget()
{
	if (!PropertyHandle.IsValid())return;
	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperClass));
	UClass* HelperClass = nullptr;
	HelperClassHandle->GetValue(*(UObject**)&HelperClass);
	if (!IsValid(HelperClass))
	{
		if (AllowedComponentClassFilters.Num() > 0)
		{
			HelperClass = UActorComponent::StaticClass();
			//a default filled in while the row is built is not an edit; see the note further down
			HelperClassHandle->SetValue((UObject*)HelperClass, EPropertyValueSetFlags::NotTransactable);
		}
	}

	UActorComponent* TargetComp = nullptr;
	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, TargetComp));
	TargetCompHandle->GetValue(*(UObject**)&TargetComp);

	auto HelperComponentNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperComponentName));

	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperActor));
	AActor* HelperActor = nullptr;
	HelperActorHandle->GetValue(*(UObject**)&HelperActor);

	TSharedPtr<SWidget> ContentWidget;
	if (bIsInWorld)
	{
		TArray<UActorComponent*> Components;
		if (HelperActor && HelperClass)
		{
			TArray<UActorComponent*> AllComponents;
			HelperActor->GetComponents(HelperClass, AllComponents);
			if (AllowedComponentClassFilters.Num() == 0 && DisallowedComponentClassFilters.Num() == 0)
			{
				Components = AllComponents;
			}
			else
			{
				for (auto& Comp : AllComponents)
				{
					if (IsAllowedComponentClass(Comp))
					{
						Components.Add(Comp);
					}
				}
			}
		}

		if (!IsValid(HelperClass))
		{
			ContentWidget =
				SNew(SBox)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor(FLinearColor::Red))
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("ComponentCheckTip", "You must set your component class in variable declaration!"))
				];
		}
		else
		{
			if (!IsValid(HelperActor))
			{
				ContentWidget = HelperActorHandle->CreatePropertyValueWidget();
			}
			else
			{
				if (Components.Num() == 0)
				{
					ContentWidget = SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						HelperActorHandle->CreatePropertyValueWidget()
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.ColorAndOpacity(FSlateColor(FLinearColor::Red))
						.AutoWrapText(true)
						.Text(LOCTEXT("ComponentOfTypeNotFound", "No valid component found on target actor!"))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					];
				}
				else if (Components.Num() == 1)
				{
					auto Comp = Components[0];
					// The only candidate, so it is filled in implicitly. Addresses resolved from the
					// handles here and now, not the raw pointers cached back in CustomizeHeader, which
					// are only good for as long as nothing reallocates the container holding the struct.
					//
					// Deliberately silent: this runs while the row is being BUILT, and nobody edited
					// anything, so a notified write would push an undo entry and dirty the package just
					// for looking at the property. OnSelectComponent is where a real pick is announced.
					DreamUIComponentReferenceCustomizationLocal::FillDerivedValue(
						HelperClassHandle, TSubclassOf<UActorComponent>(Comp->GetClass()));
					DreamUIComponentReferenceCustomizationLocal::FillDerivedValue(
						HelperComponentNameHandle, Comp->GetFName());
					ContentWidget = HelperActorHandle->CreatePropertyValueWidget();
				}
				else
				{
					ContentWidget = 
					SNew(SBox)
					.WidthOverride(500)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(0.65f)
						[
							HelperActorHandle->CreatePropertyValueWidget()
						]
						+ SHorizontalBox::Slot()
						.FillWidth(0.35f)
						[
							SNew(SComboButton)
							.ToolTipText(LOCTEXT("TargetActorHaveMultipleComponent_YouMustSelectOne", "Target actor have multiple valid components, you need to select one of them"))
							.OnGetMenuContent(this, &FDreamUIComponentReferenceCustomization::OnGetMenu, TargetCompHandle, HelperComponentNameHandle, Components)
							.ContentPadding(FMargin(0))
							.ButtonContent()
							[
								SNew(STextBlock)
								.Text(this, &FDreamUIComponentReferenceCustomization::GetButtonText, TargetCompHandle, Components)
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]
					]
					;
				}
			}
		}
	}
	else
	{
		ContentWidget = HelperClassHandle->CreatePropertyValueWidget();
	}

	ContentWidgetBox->SetContent(ContentWidget.ToSharedRef());
}
bool FDreamUIComponentReferenceCustomization::IsAllowedComponentClass(UActorComponent* InComp)
{
	auto Class = InComp->GetClass();
	bool bResult = false;
	if (AllowedComponentClassFilters.Num() > 0)
	{
		for (auto& ClassItem : AllowedComponentClassFilters)
		{
			const bool bAllowedClassIsInterface = ClassItem->HasAnyClassFlags(CLASS_Interface);
			if (Class == ClassItem || Class->IsChildOf(ClassItem) || (bAllowedClassIsInterface && Class->ImplementsInterface(ClassItem)))
			{
				bResult = true;
				break;
			}
		}
	}
	else
	{
		bResult = true;
	}
	if (bResult)
	{
		for (auto& ClassItem : DisallowedComponentClassFilters)
		{
			const bool bAllowedClassIsInterface = ClassItem->HasAnyClassFlags(CLASS_Interface);
			if (Class == ClassItem || Class->IsChildOf(ClassItem) || (bAllowedClassIsInterface && Class->ImplementsInterface(ClassItem)))
			{
				bResult = false;
				break;
			}
		}
	}
	return bResult;
}
void FDreamUIComponentReferenceCustomization::BuildClassFilters()
{
	auto AddToClassFilters = [this](const UClass* Class, TArray<const UClass*>& ComponentList)
	{
		if (Class->IsChildOf(UActorComponent::StaticClass()))
		{
			ComponentList.Add(Class);
		}
	};

	auto ParseClassFilters = [this, AddToClassFilters](const FString& MetaDataString, TArray<const UClass*>& ComponentList)
	{
		if (!MetaDataString.IsEmpty())
		{
			TArray<FString> ClassFilterNames;
			MetaDataString.ParseIntoArrayWS(ClassFilterNames, TEXT(","), true);

			for (const FString& ClassName : ClassFilterNames)
			{
				UClass* Class = UClass::TryFindTypeSlow<UClass>(ClassName);
				if (!Class)
				{
					Class = LoadObject<UClass>(nullptr, *ClassName);
				}

				if (Class)
				{
					// If the class is an interface, expand it to be all classes in memory that implement the class.
					if (Class->HasAnyClassFlags(CLASS_Interface))
					{
						for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
						{
							UClass* const ClassWithInterface = (*ClassIt);
							if (ClassWithInterface->ImplementsInterface(Class))
							{
								AddToClassFilters(ClassWithInterface, ComponentList);
							}
						}
					}
					else
					{
						AddToClassFilters(Class, ComponentList);
					}
				}
			}
		}
	};

	// Account for the allowed classes specified in the property metadata
	const FString& AllowedClassesFilterString = PropertyHandle->GetMetaData(NAME_AllowedClasses);
	ParseClassFilters(AllowedClassesFilterString, AllowedComponentClassFilters);

	const FString& DisallowedClassesFilterString = PropertyHandle->GetMetaData(NAME_DisallowedClasses);
	ParseClassFilters(DisallowedClassesFilterString, DisallowedComponentClassFilters);
}
void FDreamUIComponentReferenceCustomization::OnCopy()
{
	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperActor));
	AActor* HelperActor = nullptr;
	HelperActorHandle->GetValue(*(UObject**)&HelperActor);

	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, TargetComp));
	UActorComponent* TargetComp = nullptr;
	TargetCompHandle->GetValue(*(UObject**)&TargetComp);

	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperClass));
	UClass* HelperClass = nullptr;
	HelperClassHandle->GetValue(*(UObject**)&HelperClass);

	CopiedHelperActor = HelperActor;
	CopiedTargetComp = TargetComp;
	CopiedHelperClass = HelperClass;
}
void FDreamUIComponentReferenceCustomization::OnPaste()
{
	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperActor));
	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, TargetComp));
	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperClass));
	HelperActorHandle->SetValue((UObject*)CopiedHelperActor.Get());
	TargetCompHandle->SetValue((UObject*)CopiedTargetComp.Get());
	HelperClassHandle->SetValue((UObject*)CopiedHelperClass);
}
TSharedRef<SWidget> FDreamUIComponentReferenceCustomization::OnGetMenu(TSharedPtr<IPropertyHandle> TargetCompHandle, TSharedPtr<IPropertyHandle> CompNameProperty, TArray<UActorComponent*> Components)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	//MenuBuilder.BeginSection(FName(), LOCTEXT("Components", "Components"));
	{
		MenuBuilder.AddMenuEntry(
			FText::FromName(FName(NAME_None)),
			FText(LOCTEXT("Tip", "Clear component selection, will use first one.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FDreamUIComponentReferenceCustomization::OnSelectComponent, TargetCompHandle, CompNameProperty, (UActorComponent*)nullptr))
		);
		for (auto Comp : Components)
		{
			if (Comp->HasAnyFlags(EObjectFlags::RF_Transient))continue;
			MenuBuilder.AddMenuEntry(
				FText::FromString(Comp->GetName()),
				FText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &FDreamUIComponentReferenceCustomization::OnSelectComponent, TargetCompHandle, CompNameProperty, Comp))
			);
		}
	}
	//MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}
void FDreamUIComponentReferenceCustomization::OnSelectComponent(TSharedPtr<IPropertyHandle> TargetCompHandle, TSharedPtr<IPropertyHandle> CompNameProperty, UActorComponent* Comp)
{
	// Through the handles, not the raw struct pointers cached in CustomizeHeader. Writing the fields
	// directly skipped the transaction, the PostEditChange and the package dirty flag -- so the pick
	// could not be undone and the owning asset never asked to be saved -- and the cached addresses are
	// only valid for as long as nothing reallocates the container holding the struct.
	const FName CompName = Comp != nullptr ? Comp->GetFName() : FName(NAME_None);
	FScopedTransaction Transaction(LOCTEXT("SelectComponent_Transaction", "Select Component Reference"));
	if (TargetCompHandle.IsValid())
	{
		TargetCompHandle->SetValue((UObject*)Comp);
	}
	// HelperComponentName is VisibleAnywhere, so a handle SetValue on it is refused as edit-const --
	// and it is the half of the pick that survives a save, so it cannot simply be dropped. Written
	// through the handle's OWN addresses instead, freshly resolved and bracketed by the notify pair,
	// which is what supplies the Modify, the dirty flag and the PostEditChange that SetValue would have.
	if (CompNameProperty.IsValid())
	{
		CompNameProperty->NotifyPreChange();
		TArray<void*> NameAddresses;
		CompNameProperty->AccessRawData(NameAddresses);
		for (void* NameAddress : NameAddresses)
		{
			if (NameAddress != nullptr)
			{
				*static_cast<FName*>(NameAddress) = CompName;
			}
		}
		CompNameProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	}
}

FText FDreamUIComponentReferenceCustomization::GetButtonText(TSharedPtr<IPropertyHandle> TargetCompHandle, TArray<UActorComponent*> Components)const
{
	UActorComponent* TargetComp = nullptr;
	TargetCompHandle->GetValue(*(UObject**)&TargetComp);

	if (IsValid(TargetComp))
	{
		return FText::FromName(TargetComp->GetFName());
	}
	else
	{
		return LOCTEXT("ComponentButtonNone", "None");
	}
}
void FDreamUIComponentReferenceCustomization::OnHelperActorValueChange()
{
	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperActor));
	AActor* HelperActor = nullptr;
	HelperActorHandle->GetValue(*(UObject**)&HelperActor);

	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, TargetComp));
	UActorComponent* TargetComp = nullptr;
	TargetCompHandle->GetValue(*(UObject**)&TargetComp);

	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperClass));
	UClass* HelperClass = nullptr;
	HelperClassHandle->GetValue(*(UObject**)&HelperClass);

	//HelperComponentName, not HelperClass: the three writes below aimed at the name were resetting and
	//overwriting the CLASS instead, which is what the picker then filters the actor's components by
	auto HelperComponentNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIComponentReference, HelperComponentName));

	if (HelperActor)
	{
		if (HelperClass)
		{
			TArray<UActorComponent*> Components;
			HelperActor->GetComponents(HelperClass, Components);
			if (Components.Num() == 1)
			{
				TargetCompHandle->SetValue((UObject*)Components[0]);
				//edit-const, so SetValue/ResetToDefault are refused on it; see FillDerivedValue
				DreamUIComponentReferenceCustomizationLocal::FillDerivedValue(HelperComponentNameHandle, Components[0]->GetFName());
			}
			else if (Components.Num() == 0)
			{
				TargetCompHandle->ResetToDefault();
				HelperActorHandle->ResetToDefault();
				DreamUIComponentReferenceCustomizationLocal::FillDerivedValue(HelperComponentNameHandle, FName(NAME_None));
			}
		}
	}
	else
	{
		TargetCompHandle->ResetToDefault();
		DreamUIComponentReferenceCustomizationLocal::FillDerivedValue(HelperComponentNameHandle, FName(NAME_None));
	}

	RegenerateContentWidget();
}
#undef LOCTEXT_NAMESPACE