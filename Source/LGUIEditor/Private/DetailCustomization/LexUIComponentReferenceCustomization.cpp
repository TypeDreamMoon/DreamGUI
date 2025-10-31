// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexUIComponentReferenceCustomization.h"
#include "EdGraphNode_Comment.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "LexUIComponentReference.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "LGUIComponentRefereceHelperCustomization"

TWeakObjectPtr<AActor> FLexUIComponentReferenceCustomization::CopiedHelperActor;
TWeakObjectPtr<UActorComponent> FLexUIComponentReferenceCustomization::CopiedTargetComp;
UClass* FLexUIComponentReferenceCustomization::CopiedHelperClass;

static const FName NAME_AllowedClasses = "AllowedClasses";
static const FName NAME_DisallowedClasses = "DisallowedClasses";

TSharedRef<IPropertyTypeCustomization> FLexUIComponentReferenceCustomization::MakeInstance()
{
	return MakeShareable(new FLexUIComponentReferenceCustomization);
}
void FLexUIComponentReferenceCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
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
		auto Item = (FLexUIComponentReference*)(*Iter);
		ComponentReferenceInstances[Iter.GetIndex()] = Item;
		Item->CheckTargetObject();
	}

	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperActor));
	HelperActorHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexUIComponentReferenceCustomization::OnHelperActorValueChange));

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
		FExecuteAction::CreateSP(this, &FLexUIComponentReferenceCustomization::OnCopy),
		FCanExecuteAction::CreateLambda([this] {return bIsInWorld; })
	))
	.PasteAction(FUIAction
	(
		FExecuteAction::CreateSP(this, &FLexUIComponentReferenceCustomization::OnPaste),
		FCanExecuteAction::CreateLambda([this] {return bIsInWorld; })
	))
	.PropertyHandleList({ PropertyHandle })
	.OverrideResetToDefault(FResetToDefaultOverride::Create(
		FSimpleDelegate::CreateSP(this, &FLexUIComponentReferenceCustomization::OnResetToDefaultClicked)
	))
	;
	BuildClassFilters();
	RegenerateContentWidget();
}
void FLexUIComponentReferenceCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	
}
void FLexUIComponentReferenceCustomization::OnResetToDefaultClicked()
{
	PropertyHandle->ResetToDefault();
	RegenerateContentWidget();
}
void FLexUIComponentReferenceCustomization::RegenerateContentWidget()
{
	if (!PropertyHandle.IsValid())return;
	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperClass));
	UClass* HelperClass = nullptr;
	HelperClassHandle->GetValue(*(UObject**)&HelperClass);
	if (!IsValid(HelperClass))
	{
		if (AllowedComponentClassFilters.Num() > 0)
		{
			HelperClass = UActorComponent::StaticClass();
			HelperClassHandle->SetValue(HelperClass);
		}
	}

	UActorComponent* TargetComp = nullptr;
	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, TargetComp));
	TargetCompHandle->GetValue(*(UObject**)&TargetComp);

	auto HelperComponentNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperComponentName));

	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperActor));
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
					for (auto& Item : ComponentReferenceInstances)
					{
						Item->HelperClass = Comp->GetClass();
						Item->HelperComponentName = Comp != nullptr ? Comp->GetFName() : NAME_None;
					}
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
							.OnGetMenuContent(this, &FLexUIComponentReferenceCustomization::OnGetMenu, TargetCompHandle, HelperComponentNameHandle, Components)
							.ContentPadding(FMargin(0))
							.ButtonContent()
							[
								SNew(STextBlock)
								.Text(this, &FLexUIComponentReferenceCustomization::GetButtonText, TargetCompHandle, Components)
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
bool FLexUIComponentReferenceCustomization::IsAllowedComponentClass(UActorComponent* InComp)
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
void FLexUIComponentReferenceCustomization::BuildClassFilters()
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
void FLexUIComponentReferenceCustomization::OnCopy()
{
	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperActor));
	AActor* HelperActor = nullptr;
	HelperActorHandle->GetValue(*(UObject**)&HelperActor);

	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, TargetComp));
	UActorComponent* TargetComp = nullptr;
	TargetCompHandle->GetValue(*(UObject**)&TargetComp);

	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperClass));
	UClass* HelperClass = nullptr;
	HelperClassHandle->GetValue(*(UObject**)&HelperClass);

	CopiedHelperActor = HelperActor;
	CopiedTargetComp = TargetComp;
	CopiedHelperClass = HelperClass;
}
void FLexUIComponentReferenceCustomization::OnPaste()
{
	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperActor));
	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, TargetComp));
	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperClass));
	HelperActorHandle->SetValue((UObject*)CopiedHelperActor.Get());
	TargetCompHandle->SetValue((UObject*)CopiedTargetComp.Get());
	HelperClassHandle->SetValue((UObject*)CopiedHelperClass);
}
TSharedRef<SWidget> FLexUIComponentReferenceCustomization::OnGetMenu(TSharedPtr<IPropertyHandle> TargetCompHandle, TSharedPtr<IPropertyHandle> CompNameProperty, TArray<UActorComponent*> Components)
{
	FMenuBuilder MenuBuilder(true, nullptr);
	//MenuBuilder.BeginSection(FName(), LOCTEXT("Components", "Components"));
	{
		MenuBuilder.AddMenuEntry(
			FText::FromName(FName(NAME_None)),
			FText(LOCTEXT("Tip", "Clear component selection, will use first one.")),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateRaw(this, &FLexUIComponentReferenceCustomization::OnSelectComponent, TargetCompHandle, CompNameProperty, (UActorComponent*)nullptr))
		);
		for (auto Comp : Components)
		{
			if (Comp->HasAnyFlags(EObjectFlags::RF_Transient))continue;
			MenuBuilder.AddMenuEntry(
				FText::FromString(Comp->GetName()),
				FText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(this, &FLexUIComponentReferenceCustomization::OnSelectComponent, TargetCompHandle, CompNameProperty, Comp))
			);
		}
	}
	//MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}
void FLexUIComponentReferenceCustomization::OnSelectComponent(TSharedPtr<IPropertyHandle> TargetCompHandle, TSharedPtr<IPropertyHandle> CompNameProperty, UActorComponent* Comp)
{
	for (auto& Item : ComponentReferenceInstances)
	{
		Item->TargetComp = Comp;
		Item->HelperComponentName = Comp != nullptr ? Comp->GetFName() : NAME_None;
	}
}

FText FLexUIComponentReferenceCustomization::GetButtonText(TSharedPtr<IPropertyHandle> TargetCompHandle, TArray<UActorComponent*> Components)const
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
void FLexUIComponentReferenceCustomization::OnHelperActorValueChange()
{
	auto HelperActorHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperActor));
	AActor* HelperActor = nullptr;
	HelperActorHandle->GetValue(*(UObject**)&HelperActor);

	auto TargetCompHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, TargetComp));
	UActorComponent* TargetComp = nullptr;
	TargetCompHandle->GetValue(*(UObject**)&TargetComp);

	auto HelperClassHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperClass));
	UClass* HelperClass = nullptr;
	HelperClassHandle->GetValue(*(UObject**)&HelperClass);

	auto HelperComponentNameHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIComponentReference, HelperClass));
	
	if (HelperActor)
	{
		if (HelperClass)
		{
			TArray<UActorComponent*> Components;
			HelperActor->GetComponents(HelperClass, Components);
			if (Components.Num() == 1)
			{
				TargetCompHandle->SetValue((UObject*)Components[0]);
				HelperComponentNameHandle->SetValue(Components[0]->GetFName());
			}
			else if (Components.Num() == 0)
			{
				TargetCompHandle->ResetToDefault();
				HelperActorHandle->ResetToDefault();
				HelperComponentNameHandle->ResetToDefault();
			}
		}
	}
	else
	{
		TargetCompHandle->ResetToDefault();
		HelperComponentNameHandle->ResetToDefault();
	}

	RegenerateContentWidget();
}
#undef LOCTEXT_NAMESPACE