// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamUIEventDelegate.h"
#include "DreamGUI.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Serialization/MemoryReader.h"
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
		OutParameterType = EDreamUIEventDelegateParameterType::UInt8;
		return true;
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
		return false;
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
		if (CacheFunction != nullptr)
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
		if (ParamType != InParameterType)//function's supported parameter is equal to event's parameter
		{
			if (InParameterType == EDreamUIEventDelegateParameterType::Double && ParamType == EDreamUIEventDelegateParameterType::Float)
			{
				auto InValue = *((double*)InParam);
				auto ConvertValue = (float)InValue;
				InParam = &ConvertValue;
				auto errMsg = LOCTEXT("ParameterTypeNotEqual_DoubleToFloat", "DreamGUIEventDelegateData.Execute, Parameter type not equal, DreamGUI will automatic convert it from double to float.");
#if WITH_EDITOR
				FDreamUIUtils::EditorNotification(errMsg, false, 10);
#endif
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
			}
			else if (InParameterType == EDreamUIEventDelegateParameterType::Float && ParamType == EDreamUIEventDelegateParameterType::Double)
			{
				auto InValue = *((float*)InParam);
				auto ConvertValue = (double)InValue;
				InParam = &ConvertValue;
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
			if (CacheFunction != nullptr)
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
			if (CacheFunction != nullptr)
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
	OutError.Reset();
	if (!IsValid(HelperWidget))
	{
		OutError = TEXT("target widget is missing");
		return nullptr;
	}
	if (!IsValid(HelperClass))
	{
		OutError = TEXT("target class is missing");
		return nullptr;
	}

	if (HelperClass == UDreamWidget::StaticClass())
	{
		return HelperWidget;
	}
	if (HelperClass->IsChildOf(UDreamVisual::StaticClass()))
	{
		UObject* Result = HelperWidget->GetVisual();
		if (!IsValid(Result) || !Result->IsA(HelperClass))
		{
			OutError = FString::Printf(TEXT("widget '%s' no longer has visual '%s'"), *HelperWidget->GetDisplayName(), *HelperClass->GetName());
			return nullptr;
		}
		return Result;
	}
	if (HelperClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
	{
		UObject* Result = HelperWidget->GetLayoutContainer();
		if (!IsValid(Result) || !Result->IsA(HelperClass))
		{
			OutError = FString::Printf(TEXT("widget '%s' no longer has layout container '%s'"), *HelperWidget->GetDisplayName(), *HelperClass->GetName());
			return nullptr;
		}
		return Result;
	}
	if (HelperClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
	{
		UObject* Result = HelperWidget->GetLayoutSelf();
		if (!IsValid(Result) || !Result->IsA(HelperClass))
		{
			OutError = FString::Printf(TEXT("widget '%s' no longer has layout self '%s'"), *HelperWidget->GetDisplayName(), *HelperClass->GetName());
			return nullptr;
		}
		return Result;
	}
	if (!HelperClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		OutError = FString::Printf(TEXT("target class '%s' is not supported"), *HelperClass->GetName());
		return nullptr;
	}

	TArray<UDreamUIBehaviour*> Components = HelperWidget->GetComponents(HelperClass);
	if (!HelperComponentName.IsNone())
	{
		for (UDreamUIBehaviour* Component : Components)
		{
			if (IsValid(Component) && Component->GetFName() == HelperComponentName)
			{
				return Component;
			}
		}
		OutError = FString::Printf(TEXT("component '%s' of class '%s' is missing on widget '%s'"), *HelperComponentName.ToString(), *HelperClass->GetName(), *HelperWidget->GetDisplayName());
		return nullptr;
	}
	if (Components.Num() == 1)
	{
		return Components[0];
	}
	OutError = Components.IsEmpty()
		? FString::Printf(TEXT("component class '%s' is missing on widget '%s'"), *HelperClass->GetName(), *HelperWidget->GetDisplayName())
		: FString::Printf(TEXT("component class '%s' is ambiguous on widget '%s'"), *HelperClass->GetName(), *HelperWidget->GetDisplayName());
	return nullptr;
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
	else
	{
		if (IsValid(HelperWidget))
		{
			if (IsValid(HelperClass))
			{
				if (HelperClass == UDreamWidget::StaticClass())
				{
					TargetObject = HelperWidget;
				}
				else
				{
					if (HelperClass->IsChildOf(UDreamVisual::StaticClass()))
					{
						TargetObject = HelperWidget->GetVisual();
					}
					else if (HelperClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
					{
						TargetObject = HelperWidget->GetLayoutContainer();
					}
					else if (HelperClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
					{
						TargetObject = HelperWidget->GetLayoutSelf();
					}
					else if (HelperClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
					{
						auto Components = HelperWidget->GetComponents(HelperClass);
						if (Components.Num() == 1)
						{
							TargetObject = Components[0];
						}
						else if (Components.Num() > 1)
						{
							if (!HelperComponentName.IsNone())
							{
								for (auto& Comp : Components)
								{
									if (IsValid(Comp) && Comp->GetFName() == HelperComponentName)
									{
										TargetObject = Comp;
										return true;
									}
								}
								FString WidgetName = HelperWidget->GetDisplayName();
								UE_LOG(DreamGUI, Error, TEXT("[%s].%d Can't find component of name '%s' on widget '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *HelperComponentName.ToString(), *WidgetName);
							}
						}
					}
				}
			}
		}

		return IsValid(TargetObject);
	}
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
	default:
	{
		Target->ProcessEvent(Func, ParamBuffer.GetData());
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
	Data.HelperComponentName = InTargetComponent->GetFName();
	Data.TargetObject = InTargetComponent;
	Data.FunctionName = InFunctionName;
	Data.ParamType = InParamType;
	Data.bUseNativeParameter = bInUseNativeParameter;
	EventList.Add(Data);
}
#endif

#undef LOCTEXT_NAMESPACE


