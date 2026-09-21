// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWidgetPropertyBinding.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"

// FEnumProperty lives in its own header, not UnrealType.h; the conversion rule has to exclude it by
// name and a unity blob is no place to learn that from a neighbour.
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

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
		//
		// "In" has to allow const-reference: UHT flags a `const FText&` parameter CPF_OutParm as well
		// as CPF_ConstParm, so rejecting OutParm alone throws away every setter that takes its value
		// by const-ref -- which is how all of the FText, FString and struct setters are written, and
		// so exactly the properties anyone would want to bind. A genuine out-parameter is OutParm
		// WITHOUT ConstParm.
		const bool bIsOutParameter = (It->PropertyFlags & CPF_OutParm) && !(It->PropertyFlags & CPF_ConstParm);
		return (!bIsOutParameter && It->SameType(InProperty)) ? Setter : nullptr;
	}
	return nullptr;
}

namespace DreamWidgetBindingConversionLocal
{
	/**
	 * A number and nothing but a number.
	 *
	 * An FByteProperty with a UEnum on it and an FEnumProperty are both numeric by reflection and
	 * neither is numeric by MEANING: their values are names, and only the ones the enum declares
	 * exist. Letting a plain int flow into one would write a state the enum does not have, which is
	 * exactly the hole the text builder closes on the other side (DUI4005).
	 */
	const FNumericProperty* AsPlainNumeric(const FProperty* InProperty)
	{
		if (CastField<FEnumProperty>(InProperty) != nullptr)
		{
			return nullptr;
		}
		const FNumericProperty* Numeric = CastField<FNumericProperty>(InProperty);
		return (Numeric != nullptr && Numeric->GetIntPropertyEnum() == nullptr) ? Numeric : nullptr;
	}
}

bool CanDreamWidgetBoundValueConvert(const FProperty* InReturn, const FProperty* InTarget)
{
	using namespace DreamWidgetBindingConversionLocal;

	if (InReturn == nullptr || InTarget == nullptr)
	{
		return false;
	}
	if (InReturn->SameType(InTarget))
	{
		return true;
	}
	// A bool is numeric to nobody: FBoolProperty is not an FNumericProperty, so this needs no
	// exclusion -- it is written down only because "true is 1" is the first thing a reader wonders.
	return AsPlainNumeric(InReturn) != nullptr && AsPlainNumeric(InTarget) != nullptr;
}

bool CopyDreamWidgetBoundValue(const FProperty* InReturn, const void* InReturnValue,
	const FProperty* InTarget, void* OutTargetValue)
{
	using namespace DreamWidgetBindingConversionLocal;

	if (InReturn == nullptr || InTarget == nullptr || InReturnValue == nullptr || OutTargetValue == nullptr)
	{
		return false;
	}
	if (InReturn->SameType(InTarget))
	{
		// The whole value, constructors and all: an FText or an FString return has to be copied the
		// way its type copies, not memcpy'd.
		InTarget->CopyCompleteValue(OutTargetValue, InReturnValue);
		return true;
	}

	const FNumericProperty* From = AsPlainNumeric(InReturn);
	const FNumericProperty* To = AsPlainNumeric(InTarget);
	if (From == nullptr || To == nullptr)
	{
		return false;
	}
	// Through double when either side is floating point and through int64 otherwise, which is the
	// narrowest pair of channels that loses nothing either side could hold. A raw copy here is what
	// the old code did on the strength of a SameType it had just proved; between two widths it would
	// read past the shorter one, so the conversion is not an improvement to the copy -- it is what
	// makes the widened check safe to have made.
	if (From->IsFloatingPoint() || To->IsFloatingPoint())
	{
		const double Value = From->IsFloatingPoint()
			? From->GetFloatingPointPropertyValue(InReturnValue)
			: static_cast<double>(From->GetSignedIntPropertyValue(InReturnValue));
		if (To->IsFloatingPoint())
		{
			To->SetFloatingPointPropertyValue(OutTargetValue, Value);
		}
		else
		{
			To->SetIntPropertyValue(OutTargetValue, static_cast<int64>(Value));
		}
		return true;
	}
	To->SetIntPropertyValue(OutTargetValue, From->GetSignedIntPropertyValue(InReturnValue));
	return true;
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
