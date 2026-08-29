// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetPropertyBinding.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"

FName MakeDreamWidgetSetterName(const FProperty* InProperty)
{
	if (InProperty == nullptr)
	{
		return NAME_None;
	}
	// The bool prefix is not part of the name the setter is spelled with, and every setter in the
	// library follows this: bUseKerning is set by SetUseKerning.
	FString Name = InProperty->GetName();
	if (InProperty->IsA<FBoolProperty>() && Name.Len() > 1 && Name[0] == TEXT('b') && FChar::IsUpper(Name[1]))
	{
		Name.RightChopInline(1);
	}
	return FName(*(TEXT("Set") + Name));
}

UFunction* FindDreamWidgetSetterFor(const UClass* InClass, const FProperty* InProperty)
{
	if (InClass == nullptr || InProperty == nullptr)
	{
		return nullptr;
	}
	UFunction* Setter = InClass->FindFunctionByName(MakeDreamWidgetSetterName(InProperty));
	if (Setter == nullptr || Setter->NumParms != 1 || Setter->GetReturnProperty() != nullptr)
	{
		return nullptr;
	}
	for (TFieldIterator<FProperty> It(Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		// One parameter, in, of the property's own type. Anything else is a different function that
		// happens to be spelled the same way.
		return (!(It->PropertyFlags & CPF_OutParm) && It->SameType(InProperty)) ? Setter : nullptr;
	}
	return nullptr;
}

UObject* ResolveDreamWidgetBindingTarget(const UDreamWidget* InWidget, EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex)
{
	if (!IsValid(InWidget))
	{
		return nullptr;
	}
	switch (InTarget)
	{
	case EDreamWidgetBindingTarget::Widget:
		return const_cast<UDreamWidget*>(InWidget);
	case EDreamWidgetBindingTarget::Visual:
		return InWidget->GetVisual();
	case EDreamWidgetBindingTarget::Behaviour:
	{
		const TArray<UDreamUIBehaviour*>& Components = InWidget->GetAllComponents();
		return Components.IsValidIndex(InBehaviourIndex) ? Components[InBehaviourIndex] : nullptr;
	}
	}
	return nullptr;
}
