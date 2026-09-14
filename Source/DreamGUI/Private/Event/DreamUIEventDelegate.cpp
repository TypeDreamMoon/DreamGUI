// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamUIEventDelegate.h"
#include "DreamGUI.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Serialization/MemoryReader.h"
// A `Struct` parameter is stored as exported text and imported into a real instance before it is
// fired; UScriptStruct::ImportText reports what it could not read through one of these.
#include "Misc/StringOutputDevice.h"
#if WITH_EDITOR
#include "Utils/DreamUIUtils.h"
#endif



#define LOCTEXT_NAMESPACE "DreamGUIEventDelegate"

bool UDreamUIEventDelegateParameterHelper::IsFunctionCompatible(const UFunction* InFunction, EDreamUIEventDelegateParameterType& OutParameterType)
{
	if (InFunction->GetReturnProperty() != nullptr)return false;//not support return value for ProcessEvent
	TFieldIterator<FProperty> IteratorA(InFunction);
	TArray<EDreamUIEventDelegateParameterType> ParameterTypeArray;
	while (IteratorA && (IteratorA->PropertyFlags & CPF_Parm))
	{
		FProperty* PropA = *IteratorA;
		EDreamUIEventDelegateParameterType ParamType;
		if (IsPropertyCompatible(PropA, ParamType))
		{
			ParameterTypeArray.Add(ParamType);
		}
		else
		{
			// Type mismatch between an argument of A and B
			return false;
		}
		++IteratorA;
	}
	if (ParameterTypeArray.Num() == 1)
	{
		OutParameterType = ParameterTypeArray[0];
		return true;
	}
	if (ParameterTypeArray.Num() == 0)
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Empty;
		return true;
	}
	return false;
}
bool UDreamUIEventDelegateParameterHelper::IsPropertyCompatible(const FProperty* InFunctionProperty, EDreamUIEventDelegateParameterType& OutParameterType)
{
	if (!InFunctionProperty)
	{
		return false;
	}

	auto PropertyID = InFunctionProperty->GetID();
	switch (*PropertyID.ToEName())
	{
	case NAME_BoolProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Bool;
		return true;
	}
	case NAME_FloatProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Float;
		return true;
	}
	case NAME_DoubleProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Double;
		return true;
	}
	case NAME_Int8Property:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Int8;
		return true;
	}
	case NAME_ByteProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::UInt8;
		return true;
	}
	case NAME_Int16Property:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Int16;
		return true;
	}
	case NAME_UInt16Property:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::UInt16;
		return true;
	}
	case NAME_IntProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Int32;
		return true;
	}
	case NAME_UInt32Property:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::UInt32;
		return true;
	}
	case NAME_Int64Property:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Int64;
		return true;
	}
	case NAME_UInt64Property:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::UInt64;
		return true;
	}
	case NAME_EnumProperty:
	{
		// An `enum class` may be declared on any integer base, and the width is what matters here:
		// ParamBuffer is the parameter FRAME ProcessEvent reads, so answering UInt8 for an
		// `enum class E : int32` handed the callee a one byte buffer for a four byte argument and it
		// read three bytes past the end of it. The underlying property knows its own width, so the
		// question is asked of that rather than assumed. (For the common `: uint8` case this still
		// answers UInt8, and the enum dropdown the editor draws for that is unchanged.)
		const FEnumProperty* EnumProperty = (const FEnumProperty*)InFunctionProperty;
		const FNumericProperty* UnderlyingProperty = EnumProperty->GetUnderlyingProperty();
		if (UnderlyingProperty == nullptr)
		{
			return false;
		}
		return IsPropertyCompatible(UnderlyingProperty, OutParameterType);
	}
	case NAME_StructProperty:
	{
		auto structProperty = (FStructProperty*)InFunctionProperty;
		auto structName = structProperty->Struct->GetFName();
		if (structName == NAME_Vector2D)
		{
			OutParameterType = EDreamUIEventDelegateParameterType::Vector2; return true;
		}
		else if (structName == NAME_Vector)
		{
			OutParameterType = EDreamUIEventDelegateParameterType::Vector3; return true;
		}
		else if (structName == NAME_Vector4)
		{
			OutParameterType = EDreamUIEventDelegateParameterType::Vector4; return true;
		}
		else if (structName == NAME_Color)
		{
			OutParameterType = EDreamUIEventDelegateParameterType::Color; return true;
		}
		else if (structName == NAME_LinearColor)
		{
			OutParameterType = EDreamUIEventDelegateParameterType::LinearColor; return true;
		}
		else if (structName == NAME_Quat)
		{
			OutParameterType = EDreamUIEventDelegateParameterType::Quaternion; return true;
		}
		else if (structName == NAME_Rotator)
		{
			OutParameterType = EDreamUIEventDelegateParameterType::Rotator; return true;
		}
		// Anything else. The named cases above keep their own types -- they have dedicated editors and,
		// more importantly, dedicated SERIALIZED numbers that saved bindings already use -- and every
		// other USTRUCT is carried generically, by exported text. What used to happen here was a plain
		// `return false`, which took the function off the selector entirely: a handler taking an
		// FMargin, an FSlateColor or a game's own settings struct simply could not be bound.
		OutParameterType = EDreamUIEventDelegateParameterType::Struct;
		return true;
	}

	case NAME_ObjectProperty:
	{
		if (auto classProperty = CastField<FClassProperty>(InFunctionProperty))
		{
			OutParameterType = EDreamUIEventDelegateParameterType::Class;
			return true;
		}
		else if (auto objectProperty = CastField<FObjectProperty>(InFunctionProperty))//if object property
		{
			if (objectProperty->PropertyClass->IsChildOf(UDreamWidget::StaticClass()))//if is DreamWidget
			{
				OutParameterType = EDreamUIEventDelegateParameterType::DreamWidget;
			}
			else if (objectProperty->PropertyClass->IsChildOf(UDreamPointerEventData::StaticClass()))
			{
				OutParameterType = EDreamUIEventDelegateParameterType::PointerEvent;
			}
			else if (objectProperty->PropertyClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
			{
				return false;
			}
			else
			{
				OutParameterType = EDreamUIEventDelegateParameterType::Asset;
			}
			return true;
		}
	}

	case NAME_StrProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::String;
		return true;
	}
	case NAME_NameProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Name;
		return true;
	}
	case NAME_TextProperty:
	{
		OutParameterType = EDreamUIEventDelegateParameterType::Text;
		return true;
	}
	}

	return false;
}

UClass* UDreamUIEventDelegateParameterHelper::GetObjectParameterClass(const UFunction* InFunction)
{
	TFieldIterator<FProperty> paramsIterator(InFunction);
	FProperty* firstProperty = *paramsIterator;
	if (auto objProperty = CastField<FObjectProperty>(firstProperty))
	{
		return objProperty->PropertyClass;
	}
	return nullptr;
}

UScriptStruct* UDreamUIEventDelegateParameterHelper::GetStructParameter(const UFunction* InFunction)
{
	if (InFunction == nullptr)
	{
		return nullptr;
	}
	TFieldIterator<FProperty> ParamsIterator(InFunction);
	if (const FStructProperty* StructProperty = CastField<FStructProperty>(*ParamsIterator))
	{
		return StructProperty->Struct;
	}
	return nullptr;
}

UEnum* UDreamUIEventDelegateParameterHelper::GetEnumParameter(const UFunction* InFunction)
{
	TFieldIterator<FProperty> paramsIterator(InFunction);
	FProperty* firstProperty = *paramsIterator;
	if (auto uint8Property = CastField<FByteProperty>(firstProperty))
	{
		if (uint8Property->IsEnum())
		{
			return uint8Property->Enum;
		}
	}
	if (auto enumProperty = CastField<FEnumProperty>(firstProperty))
	{
		return enumProperty->GetEnum();
	}
	return nullptr;
}
int32 UDreamUIEventDelegateParameterHelper::GetParameterBufferSize(EDreamUIEventDelegateParameterType InParamType)
{
	//No literals here on purpose. ParamBuffer is the parameter frame ProcessEvent reads, so every one
	//of these has to be the compiler's sizeof for the type the target function actually declares.
	switch (InParamType)
	{
	case EDreamUIEventDelegateParameterType::Bool:			return (int32)sizeof(uint8);//a bool parameter is one byte in the frame, and the buffer stores 0/1
	case EDreamUIEventDelegateParameterType::Float:			return (int32)sizeof(float);
	case EDreamUIEventDelegateParameterType::Double:		return (int32)sizeof(double);
	case EDreamUIEventDelegateParameterType::Int8:			return (int32)sizeof(int8);
	case EDreamUIEventDelegateParameterType::UInt8:			return (int32)sizeof(uint8);
	case EDreamUIEventDelegateParameterType::Int16:			return (int32)sizeof(int16);
	case EDreamUIEventDelegateParameterType::UInt16:		return (int32)sizeof(uint16);
	case EDreamUIEventDelegateParameterType::Int32:			return (int32)sizeof(int32);
	case EDreamUIEventDelegateParameterType::UInt32:		return (int32)sizeof(uint32);
	case EDreamUIEventDelegateParameterType::Int64:			return (int32)sizeof(int64);
	case EDreamUIEventDelegateParameterType::UInt64:		return (int32)sizeof(uint64);
	case EDreamUIEventDelegateParameterType::Vector2:		return (int32)sizeof(FVector2D);
	case EDreamUIEventDelegateParameterType::Vector3:		return (int32)sizeof(FVector);
	case EDreamUIEventDelegateParameterType::Vector4:		return (int32)sizeof(FVector4);
	case EDreamUIEventDelegateParameterType::Quaternion:	return (int32)sizeof(FQuat);
	case EDreamUIEventDelegateParameterType::Color:			return (int32)sizeof(FColor);
	case EDreamUIEventDelegateParameterType::LinearColor:	return (int32)sizeof(FLinearColor);
	case EDreamUIEventDelegateParameterType::Rotator:		return (int32)sizeof(FRotator);
	default:												return 0;//not carried in the raw buffer
	}
}
int32 UDreamUIEventDelegateParameterHelper::GetLegacyParameterBufferSize(EDreamUIEventDelegateParameterType InParamType)
{
	//UE5 widened the engine math types to double; buffers written before that are this long. Only the
	//types that actually changed belong here - FLinearColor is four floats then and now.
	switch (InParamType)
	{
	case EDreamUIEventDelegateParameterType::Vector2:		return 2 * (int32)sizeof(float);
	case EDreamUIEventDelegateParameterType::Vector3:		return 3 * (int32)sizeof(float);
	case EDreamUIEventDelegateParameterType::Vector4:		return 4 * (int32)sizeof(float);
	case EDreamUIEventDelegateParameterType::Quaternion:	return 4 * (int32)sizeof(float);
	case EDreamUIEventDelegateParameterType::Rotator:		return 3 * (int32)sizeof(float);
	default:												return 0;//layout never changed
	}
}
bool UDreamUIEventDelegateParameterHelper::UpgradeParameterBuffer(EDreamUIEventDelegateParameterType InParamType, TArray<uint8>& InOutBuffer)
{
	const int32 RequiredSize = GetParameterBufferSize(InParamType);
	if (RequiredSize <= 0 || InOutBuffer.Num() == RequiredSize)
	{
		return false;
	}

	const int32 LegacySize = GetLegacyParameterBufferSize(InParamType);
	const int32 ComponentCount = LegacySize / (int32)sizeof(float);
	if (LegacySize > 0
		&& InOutBuffer.Num() == LegacySize
		&& ComponentCount * (int32)sizeof(double) == RequiredSize)
	{
		//Saved while these were single precision. The values are real, they are just half as wide, so
		//widen them component by component instead of silently losing what the author entered.
		TArray<uint8> WidenedBuffer;
		WidenedBuffer.SetNumUninitialized(RequiredSize);
		for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
		{
			float NarrowComponent = 0.0f;
			FMemory::Memcpy(&NarrowComponent, InOutBuffer.GetData() + ComponentIndex * sizeof(float), sizeof(float));
			const double WideComponent = (double)NarrowComponent;
			FMemory::Memcpy(WidenedBuffer.GetData() + ComponentIndex * sizeof(double), &WideComponent, sizeof(double));
		}
		InOutBuffer = MoveTemp(WidenedBuffer);
		return true;
	}

	//Any other length is a buffer we can not interpret. Fitting it to the size ProcessEvent will read
	//is the difference between a wrong value and a read off the end of the allocation.
	InOutBuffer.SetNumZeroed(RequiredSize);
	return true;
}
UObject* UDreamUIEventDelegateParameterHelper::ResolveBindingTarget(const UDreamWidget* InHelperWidget, const UClass* InHelperClass,
	int32 InHelperComponentIndex, FName InHelperComponentName, int32* OutResolvedComponentIndex, FString* OutError)
{
	auto Fail = [OutError](FString&& InMessage) -> UObject*
	{
		if (OutError != nullptr)
		{
			*OutError = MoveTemp(InMessage);
		}
		return nullptr;
	};
	if (OutResolvedComponentIndex != nullptr)
	{
		*OutResolvedComponentIndex = INDEX_NONE;
	}
	if (OutError != nullptr)
	{
		OutError->Reset();
	}
	if (!IsValid(InHelperWidget))
	{
		return Fail(TEXT("target widget is missing"));
	}
	if (!IsValid(InHelperClass))
	{
		return Fail(TEXT("target class is missing"));
	}
	//const only because callers hold const widgets; the binding hands the result to ProcessEvent
	UDreamWidget* Widget = const_cast<UDreamWidget*>(InHelperWidget);

	if (InHelperClass == UDreamWidget::StaticClass())
	{
		return Widget;
	}
	if (InHelperClass->IsChildOf(UDreamVisual::StaticClass()))
	{
		UObject* Result = Widget->GetVisual();
		return IsValid(Result) && Result->IsA(InHelperClass) ? Result
			: Fail(FString::Printf(TEXT("widget '%s' no longer has visual '%s'"), *Widget->GetDisplayName(), *InHelperClass->GetName()));
	}
	if (InHelperClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
	{
		UObject* Result = Widget->GetLayoutContainer();
		return IsValid(Result) && Result->IsA(InHelperClass) ? Result
			: Fail(FString::Printf(TEXT("widget '%s' no longer has layout container '%s'"), *Widget->GetDisplayName(), *InHelperClass->GetName()));
	}
	if (InHelperClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
	{
		UObject* Result = Widget->GetLayoutSelf();
		return IsValid(Result) && Result->IsA(InHelperClass) ? Result
			: Fail(FString::Printf(TEXT("widget '%s' no longer has layout self '%s'"), *Widget->GetDisplayName(), *InHelperClass->GetName()));
	}
	if (!InHelperClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		return Fail(FString::Printf(TEXT("target class '%s' is not supported"), *InHelperClass->GetName()));
	}

	// Position first. GetAllComponents() is the array positions are counted in -- the same one the
	// property-binding resolver walks -- so the two kinds of binding address a behaviour identically.
	const TArray<UDreamUIBehaviour*>& AllComponents = Widget->GetAllComponents();
	if (AllComponents.IsValidIndex(InHelperComponentIndex))
	{
		UDreamUIBehaviour* AtIndex = AllComponents[InHelperComponentIndex];
		if (IsValid(AtIndex) && AtIndex->IsA(InHelperClass))
		{
			if (OutResolvedComponentIndex != nullptr)
			{
				*OutResolvedComponentIndex = InHelperComponentIndex;
			}
			return AtIndex;
		}
		return Fail(FString::Printf(TEXT("behaviour %d on widget '%s' is no longer a '%s'"),
			InHelperComponentIndex, *Widget->GetDisplayName(), *InHelperClass->GetName()));
	}
	if (InHelperComponentIndex != INDEX_NONE)
	{
		return Fail(FString::Printf(TEXT("widget '%s' no longer has a behaviour at position %d"),
			*Widget->GetDisplayName(), InHelperComponentIndex));
	}

	// Nothing recorded: an asset saved before the index existed, or a class with only one candidate.
	TArray<UDreamUIBehaviour*> Candidates = Widget->GetComponents(const_cast<UClass*>(InHelperClass));
	if (!InHelperComponentName.IsNone())
	{
		for (UDreamUIBehaviour* Candidate : Candidates)
		{
			if (IsValid(Candidate) && Candidate->GetFName() == InHelperComponentName)
			{
				if (OutResolvedComponentIndex != nullptr)
				{
					*OutResolvedComponentIndex = AllComponents.IndexOfByKey(Candidate);
				}
				return Candidate;
			}
		}
		return Fail(FString::Printf(TEXT("component '%s' of class '%s' is missing on widget '%s'"),
			*InHelperComponentName.ToString(), *InHelperClass->GetName(), *Widget->GetDisplayName()));
	}
	if (Candidates.Num() == 1)
	{
		if (OutResolvedComponentIndex != nullptr)
		{
			*OutResolvedComponentIndex = AllComponents.IndexOfByKey(Candidates[0]);
		}
		return Candidates[0];
	}
	return Fail(Candidates.IsEmpty()
		? FString::Printf(TEXT("component class '%s' is missing on widget '%s'"), *InHelperClass->GetName(), *Widget->GetDisplayName())
		: FString::Printf(TEXT("component class '%s' is ambiguous on widget '%s'"), *InHelperClass->GetName(), *Widget->GetDisplayName()));
}

UClass* UDreamUIEventDelegateParameterHelper::GetClassParameterClass(const UFunction* InFunction)
{
	TFieldIterator<FProperty> paramsIterator(InFunction);
	FProperty* firstProperty = *paramsIterator;
	if (auto classProperty = CastField<FClassProperty>(firstProperty))
	{
		return classProperty->MetaClass;
	}
	return nullptr;
}

bool UDreamUIEventDelegateParameterHelper::IsSupportedFunction(UFunction* Target, EDreamUIEventDelegateParameterType& OutParamType)
{
	return IsFunctionCompatible(Target, OutParamType);
}

bool UDreamUIEventDelegateParameterHelper::IsStillSupported(UFunction* Target, EDreamUIEventDelegateParameterType InParamType)
{
	EDreamUIEventDelegateParameterType ParamType;
	if (IsSupportedFunction(Target, ParamType))
	{
		if (ParamType == InParamType)
		{
			return true;
		}
	}
	return false;
}

FString UDreamUIEventDelegateParameterHelper::ParameterTypeToName(EDreamUIEventDelegateParameterType paramType, const UFunction* InFunction)
{
	FString ParamTypeString = "";
	switch (paramType)
	{
	case EDreamUIEventDelegateParameterType::Empty:
		break;
	case EDreamUIEventDelegateParameterType::Bool:
		ParamTypeString = "Bool";
		break;
	case EDreamUIEventDelegateParameterType::Float:
		ParamTypeString = "Float";
		break;
	case EDreamUIEventDelegateParameterType::Double:
		ParamTypeString = "Double";
		break;
	case EDreamUIEventDelegateParameterType::Int8:
		ParamTypeString = "Int8";
		break;
	case EDreamUIEventDelegateParameterType::UInt8:
	{
		if (auto enumValue = GetEnumParameter(InFunction))
		{
			ParamTypeString = enumValue->GetName() + "(Enum)";
		}
		else
		{
			ParamTypeString = "UInt8";
		}
	}
		break;
	case EDreamUIEventDelegateParameterType::Int16:
		ParamTypeString = "Int16";
		break;
	case EDreamUIEventDelegateParameterType::UInt16:
		ParamTypeString = "UInt16";
		break;
	case EDreamUIEventDelegateParameterType::Int32:
		ParamTypeString = "Int32";
		break;
	case EDreamUIEventDelegateParameterType::UInt32:
		ParamTypeString = "UInt32";
		break;
	case EDreamUIEventDelegateParameterType::Int64:
		ParamTypeString = "Int64";
		break;
	case EDreamUIEventDelegateParameterType::UInt64:
		ParamTypeString = "UInt64";
		break;
	case EDreamUIEventDelegateParameterType::Vector2:
		ParamTypeString = "Vector2";
		break;
	case EDreamUIEventDelegateParameterType::Vector3:
		ParamTypeString = "Vector3";
		break;
	case EDreamUIEventDelegateParameterType::Vector4:
		ParamTypeString = "Vector4";
		break;
	case EDreamUIEventDelegateParameterType::Quaternion:
		ParamTypeString = "Quaternion";
		break;
	case EDreamUIEventDelegateParameterType::Color:
		ParamTypeString = "Color";
		break;
	case EDreamUIEventDelegateParameterType::LinearColor:
		ParamTypeString = "LinearColor";
		break;
	case EDreamUIEventDelegateParameterType::String:
		ParamTypeString = "String";
		break;

	case EDreamUIEventDelegateParameterType::Asset:
	{
		TFieldIterator<FProperty> ParamIterator(InFunction);
		if (auto firstProperty = CastField<FObjectProperty>(*ParamIterator))
		{
			if (firstProperty->PropertyClass != UObject::StaticClass())
			{
				ParamTypeString = firstProperty->PropertyClass->GetName() + "(Object)";
			}
			else
			{
				ParamTypeString = "Object";
			}
		}
		else
		{
			ParamTypeString = "Object";
		}
	}
		break;
	case EDreamUIEventDelegateParameterType::DreamWidget:
	{
		TFieldIterator<FProperty> ParamIterator(InFunction);
		if (auto firstProperty = CastField<FObjectProperty>(*ParamIterator))
		{
			if (firstProperty->PropertyClass != UDreamWidget::StaticClass())
			{
				ParamTypeString = firstProperty->PropertyClass->GetName() + "(DreamWidget)";
			}
			else
			{
				ParamTypeString = "UDreamWidget";
			}
		}
		else
		{
			ParamTypeString = "UDreamWidget";
		}
	}
		break;
	case EDreamUIEventDelegateParameterType::PointerEvent:
		ParamTypeString = "PointerEvent";
		break;
	case EDreamUIEventDelegateParameterType::Class:
		ParamTypeString = "Class";
		break;
	case EDreamUIEventDelegateParameterType::Rotator:
		ParamTypeString = "Rotator";
		break;
	case EDreamUIEventDelegateParameterType::Name:
		ParamTypeString = "Name";
		break;
	case EDreamUIEventDelegateParameterType::Text:
		ParamTypeString = "Text";
		break;
	case EDreamUIEventDelegateParameterType::Struct:
	{
		//named, because "Struct" in a function selector tells the author nothing they can act on
		const UScriptStruct* StructValue = GetStructParameter(InFunction);
		ParamTypeString = StructValue != nullptr ? StructValue->GetName() : TEXT("Struct");
	}
		break;
	default:
		break;
	}
	return ParamTypeString;
}



void FDreamUIEventDelegateData::Execute()
{
	if (bUseNativeParameter)
	{
		auto errMsg = LOCTEXT("NativeParameterError", "DreamGUIEventDelegateData.Execute, If use NativeParameter, you must FireEvent with your own parameter!");
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
		return;
	}
	if (ParamType == EDreamUIEventDelegateParameterType::None)
	{
		auto errMsg = LOCTEXT("NotValid", "DreamGUIEventDelegateData.Execute, Not valid DreamGUIEventDelegate.");
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
		return;
	}
	if (CheckTargetObject())
	{
		if (IsCacheFunctionValidFor(TargetObject))
		{
			ExecuteTargetFunction(TargetObject, CacheFunction);
		}
		else
		{
			FindAndExecute(TargetObject);
		}
	}
}
void FDreamUIEventDelegateData::Execute(void* InParam, EDreamUIEventDelegateParameterType InParameterType)
{
	if (ParamType == EDreamUIEventDelegateParameterType::None)
	{
		auto errMsg = LOCTEXT("NotValid", "DreamGUIEventDelegateData.Execute, Not valid DreamGUIEventDelegate.");
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
		return;
	}

	if (bUseNativeParameter)//should use native parameter (pass in param)
	{
		//conversion storage must outlive the branch below, because InParam is dereferenced after CheckTargetObject()
		float ConvertedFloatValue = 0.0f;
		double ConvertedDoubleValue = 0.0;
		if (ParamType != InParameterType)//function's supported parameter is equal to event's parameter
		{
			if (InParameterType == EDreamUIEventDelegateParameterType::Double && ParamType == EDreamUIEventDelegateParameterType::Float)
			{
				ConvertedFloatValue = (float)(*((double*)InParam));
				InParam = &ConvertedFloatValue;
				auto errMsg = LOCTEXT("ParameterTypeNotEqual_DoubleToFloat", "DreamGUIEventDelegateData.Execute, Parameter type not equal, DreamGUI will automatic convert it from double to float.");
#if WITH_EDITOR
				FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
			}
			else if (InParameterType == EDreamUIEventDelegateParameterType::Float && ParamType == EDreamUIEventDelegateParameterType::Double)
			{
				ConvertedDoubleValue = (double)(*((float*)InParam));
				InParam = &ConvertedDoubleValue;
				auto errMsg = LOCTEXT("ParameterTypeNotEqual_FloatToDouble", "DreamGUIEventDelegateData.Execute, Parameter type not equal, DreamGUI will automatic convert it from float to double.");
#if WITH_EDITOR
				FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
			}
			else
			{
				auto errMsg = LOCTEXT("ParameterTypeNotEqual", "DreamGUIEventDelegateData.Execute, Parameter type not equal!");
#if WITH_EDITOR
				FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
				return;
			}
		}
		if (CheckTargetObject())
		{
			if (IsCacheFunctionValidFor(TargetObject))
			{
				ExecuteTargetFunction(TargetObject, CacheFunction, InParam);
			}
			else
			{
				FindAndExecute(TargetObject, InParam);
			}
		}
	}
	else
	{
		if (CheckTargetObject())
		{
			if (IsCacheFunctionValidFor(TargetObject))
			{
				ExecuteTargetFunction(TargetObject, CacheFunction);
			}
			else
			{
				FindAndExecute(TargetObject);
			}
		}
	}
}

#if WITH_EDITOR
UObject* FDreamUIEventDelegateData::ResolveTargetForValidation(FString& OutError) const
{
	// The shared resolver, so validation cannot disagree with what the runtime will actually reach.
	return UDreamUIEventDelegateParameterHelper::ResolveBindingTarget(
		HelperWidget, HelperClass, HelperComponentIndex, HelperComponentName, nullptr, &OutError);
}

bool FDreamUIEventDelegateData::CheckFunctionParameter()const
{
	if (ParamType == EDreamUIEventDelegateParameterType::None)
	{
		return false;
	}

	FString ResolveError;
	UObject* ResolvedTarget = ResolveTargetForValidation(ResolveError);
	if (!IsValid(ResolvedTarget))
	{
		return false;
	}

	auto TargetFunction = ResolvedTarget->FindFunction(FunctionName);
	if (!TargetFunction)
	{
		return false;
	}
	if (!UDreamUIEventDelegateParameterHelper::IsStillSupported(TargetFunction, ParamType))
	{
		return false;
	}

	return true;
}
#endif

bool FDreamUIEventDelegateData::CheckTargetObject()
{
	if (IsValid(TargetObject))
	{
		return true;
	}

	// One resolver for the runtime, the editor panel and validation. The old copy here disagreed with
	// the other two in the case that mattered: it only consulted HelperComponentName when there was
	// more than one candidate, and it addressed behaviours by name at all -- a key UE re-numbers on
	// every preview rebuild, so the author's choice was silently lost and the target cleared.
	FString ResolveError;
	int32 ResolvedComponentIndex = INDEX_NONE;
	TargetObject = UDreamUIEventDelegateParameterHelper::ResolveBindingTarget(
		HelperWidget, HelperClass, HelperComponentIndex, HelperComponentName, &ResolvedComponentIndex, &ResolveError);
	if (!IsValid(TargetObject))
	{
		// Only when there was something to resolve: an unset binding (no widget picked yet) is the
		// ordinary state of a freshly added row and is not worth a line in the log.
		if (IsValid(HelperWidget) && IsValid(HelperClass))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot resolve this event's target: %s"),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ResolveError);
		}
		return false;
	}
	// Recorded the first time a legacy name resolves, so the binding stops depending on the name.
	// In memory only until something else saves the asset, which is the point: an upgrade nobody has
	// to run, and a package this does not dirty on its own.
	if (HelperComponentIndex != ResolvedComponentIndex)
	{
		HelperComponentIndex = ResolvedComponentIndex;
	}
	return true;
}
bool FDreamUIEventDelegateData::IsCacheFunctionValidFor(const UObject* Target) const
{
	if (!IsValid(CacheFunction) || !IsValid(Target))
	{
		return false;
	}
	const UClass* OwnerClass = CacheFunction->GetOwnerClass();
	if (!IsValid(OwnerClass) || OwnerClass->HasAnyClassFlags(CLASS_NewerVersionExists))
	{
		return false;//blueprint recompiled, the cached function now lives on a REINST_/TRASHCLASS_ class
	}
	return Target->GetClass()->IsChildOf(OwnerClass);//target was re-resolved to an unrelated class
}
void FDreamUIEventDelegateData::FindAndExecute(UObject* Target, void* ParamData)
{
	CacheFunction = Target->FindFunction(FunctionName);
	if (CacheFunction)
	{
		if (!UDreamUIEventDelegateParameterHelper::IsStillSupported(CacheFunction, ParamType))
		{
			auto errMsg = FText::Format(LOCTEXT("FunctionNotSupport", "DreamGUIEventDelegateData.FindAndExecute, Target function: {0} not supported!"), FText::FromName(FunctionName));
#if WITH_EDITOR
			FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
			CacheFunction = nullptr;
		}
		else
		{
			if (ParamData == nullptr)
			{
				ExecuteTargetFunction(Target, CacheFunction);
			}
			else
			{
				ExecuteTargetFunction(Target, CacheFunction, ParamData);
			}
		}
	}
	else
	{
		auto errMsg = FText::Format(LOCTEXT("FunctionNotExist", "DreamGUIEventDelegateData.FindAndExecute, Target function: {0} not exist!"), FText::FromName(FunctionName));
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
	}
}
namespace DreamUIEventDelegateParamBuffer
{
	/** Read a fixed size parameter out of the stored buffer, upgrading a legacy one on the way. */
	template<typename T>
	static T ReadValue(EDreamUIEventDelegateParameterType InParamType, const TArray<uint8>& InBuffer)
	{
		T Value;
		FMemory::Memzero(&Value, sizeof(T));
		TArray<uint8> Bytes(InBuffer);
		UDreamUIEventDelegateParameterHelper::UpgradeParameterBuffer(InParamType, Bytes);
		if (Bytes.Num() == (int32)sizeof(T))
		{
			FMemory::Memcpy(&Value, Bytes.GetData(), sizeof(T));
		}
		return Value;
	}
}
void FDreamUIEventDelegateData::ExecuteTargetFunction(UObject* Target, UFunction* Func)
{
	switch (ParamType)
	{
	case EDreamUIEventDelegateParameterType::String:
	{
		FString TempString;
		auto FromBinary = FMemoryReader(ParamBuffer, false);
		FromBinary << TempString;
		Target->ProcessEvent(Func, &TempString);
	}
	break;
	case EDreamUIEventDelegateParameterType::Name:
	{
		FName TempName;
		auto FromBinary = FMemoryReader(ParamBuffer, false);
		FromBinary << TempName;
		Target->ProcessEvent(Func, &TempName);
	}
	break;
	case EDreamUIEventDelegateParameterType::Text:
	{
		FText TempText;
		auto FromBinary = FMemoryReader(ParamBuffer, false);
		FromBinary << TempText;
		Target->ProcessEvent(Func, &TempText);
	}
	break;
	case EDreamUIEventDelegateParameterType::Asset:
	case EDreamUIEventDelegateParameterType::DreamWidget:
	case EDreamUIEventDelegateParameterType::Class:
	{
		Target->ProcessEvent(Func, &ReferenceObject);
	}
	break;
	case EDreamUIEventDelegateParameterType::Struct:
	{
		// A REAL instance, constructed and destroyed by the struct's own operations. A struct
		// parameter may own heap memory (an FString member, a TArray), so the raw-buffer path below
		// cannot carry one: it would hand ProcessEvent a copy of somebody else's pointers, and the
		// callee's destructor would free them a second time. The type is the function's own
		// declaration rather than anything stored, so a signature change is a mismatch the checks
		// above catch instead of a stale type nobody notices.
		UScriptStruct* ParameterStruct = UDreamUIEventDelegateParameterHelper::GetStructParameter(Func);
		if (!IsValid(ParameterStruct))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' takes a struct this build cannot identify; the event was not fired."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FunctionName.ToString());
			break;
		}
		void* Storage = FMemory::Malloc(FMath::Max(1, ParameterStruct->GetStructureSize()), ParameterStruct->GetMinAlignment());
		ParameterStruct->InitializeStruct(Storage);
		if (!StructValue.IsEmpty() && StructValueType != ParameterStruct)
		{
			// The stored literal belongs to a different struct -- the handler's signature changed under
			// it. Defaults are sent instead of a partial parse, and it is said out loud: two structs
			// can share a member name, and half a value that looks plausible is the worst outcome here.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' now takes a '%s', but its stored value was written for '%s'; the struct's defaults were sent. Re-enter the value."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *FunctionName.ToString(), *ParameterStruct->GetName(), *GetNameSafe(StructValueType));
		}
		else if (!StructValue.IsEmpty())
		{
			// Errors to the log, not swallowed: a value that no longer parses (a member renamed out
			// from under it) leaves the struct at its defaults, and silently sending defaults is the
			// kind of wrong that looks like the handler being broken.
			FStringOutputDevice ImportErrors;
			ParameterStruct->ImportText(*StructValue, Storage, nullptr, PPF_None, &ImportErrors, ParameterStruct->GetName());
			if (!ImportErrors.IsEmpty())
			{
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot read the stored '%s' value for '%s': %s"),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ParameterStruct->GetName(), *FunctionName.ToString(), *ImportErrors);
			}
		}
		Target->ProcessEvent(Func, Storage);
		ParameterStruct->DestroyStruct(Storage);
		FMemory::Free(Storage);
	}
	break;
	//The engine math types went double precision in UE5, so a buffer saved before that is both the
	//wrong length and the wrong encoding. Decoding into a local also gives ProcessEvent a parameter
	//frame with the type's own alignment, which TArray<uint8> storage does not promise for the
	//16-byte-aligned ones.
	case EDreamUIEventDelegateParameterType::Vector2:
	{
		FVector2D TempVector2 = DreamUIEventDelegateParamBuffer::ReadValue<FVector2D>(ParamType, ParamBuffer);
		Target->ProcessEvent(Func, &TempVector2);
	}
	break;
	case EDreamUIEventDelegateParameterType::Vector3:
	{
		FVector TempVector3 = DreamUIEventDelegateParamBuffer::ReadValue<FVector>(ParamType, ParamBuffer);
		Target->ProcessEvent(Func, &TempVector3);
	}
	break;
	case EDreamUIEventDelegateParameterType::Vector4:
	{
		FVector4 TempVector4 = DreamUIEventDelegateParamBuffer::ReadValue<FVector4>(ParamType, ParamBuffer);
		Target->ProcessEvent(Func, &TempVector4);
	}
	break;
	case EDreamUIEventDelegateParameterType::Quaternion:
	{
		FQuat TempQuat = DreamUIEventDelegateParamBuffer::ReadValue<FQuat>(ParamType, ParamBuffer);
		Target->ProcessEvent(Func, &TempQuat);
	}
	break;
	case EDreamUIEventDelegateParameterType::Rotator:
	{
		FRotator TempRotator = DreamUIEventDelegateParamBuffer::ReadValue<FRotator>(ParamType, ParamBuffer);
		Target->ProcessEvent(Func, &TempRotator);
	}
	break;
	default:
	{
		//ProcessEvent reads GetParameterBufferSize() bytes out of whatever is handed to it, so a short
		//buffer - an event whose function was picked but whose value was never edited, say - used to be
		//a read off the end of the allocation.
		const int32 RequiredSize = UDreamUIEventDelegateParameterHelper::GetParameterBufferSize(ParamType);
		if (RequiredSize > 0 && ParamBuffer.Num() != RequiredSize)
		{
			TArray<uint8> FittedBuffer(ParamBuffer);
			UDreamUIEventDelegateParameterHelper::UpgradeParameterBuffer(ParamType, FittedBuffer);
			Target->ProcessEvent(Func, FittedBuffer.GetData());
		}
		else
		{
			Target->ProcessEvent(Func, ParamBuffer.GetData());
		}
	}
	break;
	}
}
void FDreamUIEventDelegateData::ExecuteTargetFunction(UObject* Target, UFunction* Func, void* ParamData)
{
	Target->ProcessEvent(Func, ParamData);
}

FDreamUIEventDelegate::FDreamUIEventDelegate()
{
}
FDreamUIEventDelegate::FDreamUIEventDelegate(EDreamUIEventDelegateParameterType InParameterType)
{
	SupportParameterType = InParameterType;
}

bool FDreamUIEventDelegate::IsBound()const
{
	return EventList.Num() != 0;
}
void FDreamUIEventDelegate::AddRuntimeRoute(UObject* InHandlerObject, FName InFunctionName)
{
	if (!IsValid(InHandlerObject) || InFunctionName.IsNone())
	{
		return;
	}
	// Already routed. BindEventBindings runs again on a re-initialise (and on the designer's preview
	// rebuild), and one route in the file must mean one call, not one per initialise.
	const bool bAlreadyRouted = EventList.ContainsByPredicate([InHandlerObject, InFunctionName](const FDreamUIEventDelegateData& Candidate)
		{ return Candidate.TargetObject == InHandlerObject && Candidate.FunctionName == InFunctionName; });
	if (bAlreadyRouted)
	{
		return;
	}
	FDreamUIEventDelegateData& Data = EventList.AddDefaulted_GetRef();
	// The target directly, with no helper fields: this route names an object, not a place in a widget
	// hierarchy, and CheckTargetObject takes a valid TargetObject as the answer without resolving.
	Data.TargetObject = InHandlerObject;
	Data.FunctionName = InFunctionName;
	Data.ParamType = SupportParameterType;
	// An Empty event fires through Execute() with no parameter at all, and that overload refuses
	// bUseNativeParameter outright; every other one forwards what the event was fired with.
	Data.bUseNativeParameter = SupportParameterType != EDreamUIEventDelegateParameterType::Empty;
}
void FDreamUIEventDelegate::FireEvent()const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Empty)
	{
		for (auto& item : EventList)
		{
			item.Execute();
		}
	}
	else
		LogParameterError(EDreamUIEventDelegateParameterType::Empty);
}
void FDreamUIEventDelegate::LogParameterError(EDreamUIEventDelegateParameterType WrongParamType)const
{
	auto enumObject = FindObject<UEnum>(nullptr, TEXT("/Script/DreamGUI.EDreamUIEventDelegateParameterType"), EFindObjectFlags::ExactClass);
	auto errMsg = FText::Format(LOCTEXT("ParameterTypeMismatch", "DreamUIEventDelegate parameter type must be the same as your declaration. support parameter type: {0}, execute parameter type: {1}")
		, enumObject->GetDisplayNameTextByValue((int64)SupportParameterType)
		, enumObject->GetDisplayNameTextByValue((int64)WrongParamType)
	);
#if WITH_EDITOR
	FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
	UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
}
void FDreamUIEventDelegate::FireEvent(void* InParam)const
{
	for (auto& item : EventList)
	{
		item.Execute(InParam, SupportParameterType);
	}
}

void FDreamUIEventDelegate::FireEvent(bool InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Bool)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Bool);
}
void FDreamUIEventDelegate::FireEvent(float InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Float)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Float);
}
void FDreamUIEventDelegate::FireEvent(double InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Double)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Double);
}
void FDreamUIEventDelegate::FireEvent(int8 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Int8)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Int8);
}
void FDreamUIEventDelegate::FireEvent(uint8 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::UInt8)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::UInt8);
}
void FDreamUIEventDelegate::FireEvent(int16 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Int16)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Int16);
}
void FDreamUIEventDelegate::FireEvent(uint16 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::UInt16)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::UInt16);
}
void FDreamUIEventDelegate::FireEvent(int32 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Int32)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Int32);
}
void FDreamUIEventDelegate::FireEvent(uint32 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::UInt32)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::UInt32);
}
void FDreamUIEventDelegate::FireEvent(int64 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Int64)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Int64);
}
void FDreamUIEventDelegate::FireEvent(uint64 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::UInt64)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::UInt64);
}
void FDreamUIEventDelegate::FireEvent(FVector2D InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Vector2)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Vector2);
}
void FDreamUIEventDelegate::FireEvent(FVector InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Vector3)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Vector3);
}
void FDreamUIEventDelegate::FireEvent(FVector4 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Vector4)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Vector4);
}
void FDreamUIEventDelegate::FireEvent(FColor InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Color)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Color);
}
void FDreamUIEventDelegate::FireEvent(FLinearColor InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::LinearColor)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::LinearColor);
}
void FDreamUIEventDelegate::FireEvent(FQuat InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Quaternion)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Quaternion);
}
void FDreamUIEventDelegate::FireEvent(const FString& InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::String)
	{
		FireEvent((void*)&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::String);
}
void FDreamUIEventDelegate::FireEvent(UObject* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Asset)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Asset);
}
void FDreamUIEventDelegate::FireEvent(UDreamWidget* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::DreamWidget)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::DreamWidget);
}
void FDreamUIEventDelegate::FireEvent(UDreamPointerEventData* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::PointerEvent)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::PointerEvent);
}
void FDreamUIEventDelegate::FireEvent(UClass* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Class)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Class);
}
void FDreamUIEventDelegate::FireEvent(FRotator InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Rotator)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Rotator);
}
void FDreamUIEventDelegate::FireEvent(const FName& InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Name)
	{
		FireEvent((void*)&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Name);
}
void FDreamUIEventDelegate::FireEvent(const FText& InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == EDreamUIEventDelegateParameterType::Text)
	{
		FireEvent((void*)&InParam);
	}
	else LogParameterError(EDreamUIEventDelegateParameterType::Text);
}

#if WITH_EDITOR
bool FDreamUIEventDelegate::CheckFunctionParameter()const
{
	TArray<FDreamUIEventBindingValidationIssue> Issues;
	GetValidationIssues(Issues);
	return Issues.IsEmpty();
}
void FDreamUIEventDelegate::GetValidationIssues(TArray<FDreamUIEventBindingValidationIssue>& OutIssues, const UDreamWidget* RootWidget) const
{
	for (int32 Index = 0; Index < EventList.Num(); ++Index)
	{
		const FDreamUIEventDelegateData& Item = EventList[Index];
		FDreamUIEventBindingValidationIssue Issue;
		Issue.BindingIndex = Index;
		Issue.TargetWidget = Item.HelperWidget;
		Issue.FunctionName = Item.FunctionName;
		if (IsValid(RootWidget) && IsValid(Item.HelperWidget)
			&& Item.HelperWidget != RootWidget && !Item.HelperWidget->IsChildOf(RootWidget))
		{
			Issue.Message = TEXT("target widget is outside this prefab and cannot be serialized");
			OutIssues.Add(MoveTemp(Issue));
			continue;
		}

		FString ResolveError;
		UObject* Target = Item.ResolveTargetForValidation(ResolveError);
		if (!IsValid(Target))
		{
			Issue.Message = MoveTemp(ResolveError);
			OutIssues.Add(MoveTemp(Issue));
			continue;
		}
		if (Item.ParamType == EDreamUIEventDelegateParameterType::None)
		{
			Issue.Message = TEXT("binding parameter type is invalid");
			OutIssues.Add(MoveTemp(Issue));
			continue;
		}
		if (Item.FunctionName.IsNone())
		{
			Issue.Message = TEXT("target function is not set");
			OutIssues.Add(MoveTemp(Issue));
			continue;
		}

		UFunction* Function = Target->FindFunction(Item.FunctionName);
		if (Function == nullptr)
		{
			Issue.Message = FString::Printf(TEXT("function '%s' no longer exists on '%s'"), *Item.FunctionName.ToString(), *Target->GetClass()->GetName());
			OutIssues.Add(MoveTemp(Issue));
			continue;
		}
		if (!UDreamUIEventDelegateParameterHelper::IsStillSupported(Function, Item.ParamType))
		{
			Issue.Message = FString::Printf(TEXT("function '%s' has an incompatible signature"), *Item.FunctionName.ToString());
			OutIssues.Add(MoveTemp(Issue));
			continue;
		}
		if (Item.bUseNativeParameter && Item.ParamType != SupportParameterType)
		{
			const bool bConvertibleFloatPair =
				(Item.ParamType == EDreamUIEventDelegateParameterType::Float && SupportParameterType == EDreamUIEventDelegateParameterType::Double)
				|| (Item.ParamType == EDreamUIEventDelegateParameterType::Double && SupportParameterType == EDreamUIEventDelegateParameterType::Float);
			if (!bConvertibleFloatPair)
			{
				Issue.Message = FString::Printf(TEXT("function '%s' expects a different native event parameter"), *Item.FunctionName.ToString());
				OutIssues.Add(MoveTemp(Issue));
			}
		}
	}
}
void FDreamUIEventDelegate::ReplaceBindingTarget(UDreamUIBehaviour* InOldTarget, UDreamUIBehaviour* InNewTarget)
{
	if (!IsValid(InOldTarget) || !IsValid(InNewTarget))
	{
		return;
	}
	for (FDreamUIEventDelegateData& Item : EventList)
	{
		FString ResolveError;
		UObject* ResolvedTarget = Item.ResolveTargetForValidation(ResolveError);
		if (Item.TargetObject == InOldTarget || ResolvedTarget == InOldTarget)
		{
			Item.TargetObject = InNewTarget;
			Item.HelperWidget = InNewTarget->GetWidget();
			Item.HelperClass = InNewTarget->GetClass();
			//position is the key; the name is written alongside it only so an older build still reads it
			Item.HelperComponentIndex = IsValid(Item.HelperWidget)
				? Item.HelperWidget->GetAllComponents().IndexOfByKey(InNewTarget) : INDEX_NONE;
			Item.HelperComponentName = InNewTarget->GetFName();
			Item.CacheFunction = nullptr;
		}
	}
}
bool FDreamUIEventDelegate::HasFunctionBinding(UDreamUIBehaviour* InTargetComponent, FName InFunctionName)const
{
	for (auto& item : EventList)
	{
		if (item.TargetObject == InTargetComponent && item.FunctionName == InFunctionName)return true;
	}
	return false;
}
FName FDreamUIEventDelegate::FindFunctionBoundToComponent(UDreamUIBehaviour* InTargetComponent)const
{
	for (auto& item : EventList)
	{
		if (item.TargetObject == InTargetComponent && !item.FunctionName.IsNone())return item.FunctionName;
	}
	return NAME_None;
}
void FDreamUIEventDelegate::AddFunctionBinding(UDreamWidget* InHelperWidget, UDreamUIBehaviour* InTargetComponent, FName InFunctionName, EDreamUIEventDelegateParameterType InParamType, bool bInUseNativeParameter)
{
	if (InTargetComponent == nullptr)return;
	// FDreamUIEventDelegate is a friend of FDreamUIEventDelegateData, so the helper fields the
	// event customization normally fills can be set directly -- same result as picking the
	// component + function in the details panel by hand
	FDreamUIEventDelegateData Data;
	Data.HelperWidget = InHelperWidget;
	Data.HelperClass = InTargetComponent->GetClass();
	//position is the key; the name is written alongside it only so an older build still reads it
	Data.HelperComponentIndex = IsValid(InHelperWidget)
		? InHelperWidget->GetAllComponents().IndexOfByKey(InTargetComponent) : INDEX_NONE;
	Data.HelperComponentName = InTargetComponent->GetFName();
	Data.TargetObject = InTargetComponent;
	Data.FunctionName = InFunctionName;
	Data.ParamType = InParamType;
	Data.bUseNativeParameter = bInUseNativeParameter;
	EventList.Add(Data);
}
void FDreamUIEventDelegate::SetStructParameterValue(int32 InBindingIndex, UScriptStruct* InStruct, const FString& InExportedText)
{
	if (!EventList.IsValidIndex(InBindingIndex))
	{
		return;
	}
	FDreamUIEventDelegateData& Data = EventList[InBindingIndex];
	Data.StructValue = InExportedText;
	Data.StructValueType = InStruct;
}
#endif

#undef LOCTEXT_NAMESPACE


