// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexUIEventDelegate.h"
#include "LGUI.h"
#include "Core/LexUIBehaviour.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"
#include "Serialization/MemoryReader.h"
#if WITH_EDITOR
#include "Utils/LexUIUtils.h"
#endif



#define LOCTEXT_NAMESPACE "LGUIEventDelegate"

bool ULexUIEventDelegateParameterHelper::IsFunctionCompatible(const UFunction* InFunction, ELexUIEventDelegateParameterType& OutParameterType)
{
	if (InFunction->GetReturnProperty() != nullptr)return false;//not support return value for ProcessEvent
	TFieldIterator<FProperty> IteratorA(InFunction);
	TArray<ELexUIEventDelegateParameterType> ParameterTypeArray;
	while (IteratorA && (IteratorA->PropertyFlags & CPF_Parm))
	{
		FProperty* PropA = *IteratorA;
		ELexUIEventDelegateParameterType ParamType;
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
		OutParameterType = ELexUIEventDelegateParameterType::Empty;
		return true;
	}
	return false;
}
bool ULexUIEventDelegateParameterHelper::IsPropertyCompatible(const FProperty* InFunctionProperty, ELexUIEventDelegateParameterType& OutParameterType)
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
		OutParameterType = ELexUIEventDelegateParameterType::Bool;
		return true;
	}
	case NAME_FloatProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Float;
		return true;
	}
	case NAME_DoubleProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Double;
		return true;
	}
	case NAME_Int8Property:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Int8;
		return true;
	}
	case NAME_ByteProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::UInt8;
		return true;
	}
	case NAME_Int16Property:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Int16;
		return true;
	}
	case NAME_UInt16Property:
	{
		OutParameterType = ELexUIEventDelegateParameterType::UInt16;
		return true;
	}
	case NAME_IntProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Int32;
		return true;
	}
	case NAME_UInt32Property:
	{
		OutParameterType = ELexUIEventDelegateParameterType::UInt32;
		return true;
	}
	case NAME_Int64Property:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Int64;
		return true;
	}
	case NAME_UInt64Property:
	{
		OutParameterType = ELexUIEventDelegateParameterType::UInt64;
		return true;
	}
	case NAME_EnumProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::UInt8;
		return true;
	}
	case NAME_StructProperty:
	{
		auto structProperty = (FStructProperty*)InFunctionProperty;
		auto structName = structProperty->Struct->GetFName();
		if (structName == NAME_Vector2D)
		{
			OutParameterType = ELexUIEventDelegateParameterType::Vector2; return true;
		}
		else if (structName == NAME_Vector)
		{
			OutParameterType = ELexUIEventDelegateParameterType::Vector3; return true;
		}
		else if (structName == NAME_Vector4)
		{
			OutParameterType = ELexUIEventDelegateParameterType::Vector4; return true;
		}
		else if (structName == NAME_Color)
		{
			OutParameterType = ELexUIEventDelegateParameterType::Color; return true;
		}
		else if (structName == NAME_LinearColor)
		{
			OutParameterType = ELexUIEventDelegateParameterType::LinearColor; return true;
		}
		else if (structName == NAME_Quat)
		{
			OutParameterType = ELexUIEventDelegateParameterType::Quaternion; return true;
		}
		else if (structName == NAME_Rotator)
		{
			OutParameterType = ELexUIEventDelegateParameterType::Rotator; return true;
		}
		return false;
	}

	case NAME_ObjectProperty:
	{
		if (auto classProperty = CastField<FClassProperty>(InFunctionProperty))
		{
			OutParameterType = ELexUIEventDelegateParameterType::Class;
			return true;
		}
		else if (auto objectProperty = CastField<FObjectProperty>(InFunctionProperty))//if object property
		{
			if (objectProperty->PropertyClass->IsChildOf(ULexWidget::StaticClass()))//if is LexWidget
			{
				OutParameterType = ELexUIEventDelegateParameterType::LexWidget;
			}
			else if (objectProperty->PropertyClass->IsChildOf(ULexPointerEventData::StaticClass()))
			{
				OutParameterType = ELexUIEventDelegateParameterType::PointerEvent;
			}
			else if (objectProperty->PropertyClass->IsChildOf(ULexUIBehaviour::StaticClass()))
			{
				return false;
			}
			else
			{
				OutParameterType = ELexUIEventDelegateParameterType::Asset;
			}
			return true;
		}
	}

	case NAME_StrProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::String;
		return true;
	}
	case NAME_NameProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Name;
		return true;
	}
	case NAME_TextProperty:
	{
		OutParameterType = ELexUIEventDelegateParameterType::Text;
		return true;
	}
	}

	return false;
}

UClass* ULexUIEventDelegateParameterHelper::GetObjectParameterClass(const UFunction* InFunction)
{
	TFieldIterator<FProperty> paramsIterator(InFunction);
	FProperty* firstProperty = *paramsIterator;
	if (auto objProperty = CastField<FObjectProperty>(firstProperty))
	{
		return objProperty->PropertyClass;
	}
	return nullptr;
}

UEnum* ULexUIEventDelegateParameterHelper::GetEnumParameter(const UFunction* InFunction)
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
UClass* ULexUIEventDelegateParameterHelper::GetClassParameterClass(const UFunction* InFunction)
{
	TFieldIterator<FProperty> paramsIterator(InFunction);
	FProperty* firstProperty = *paramsIterator;
	if (auto classProperty = CastField<FClassProperty>(firstProperty))
	{
		return classProperty->MetaClass;
	}
	return nullptr;
}

bool ULexUIEventDelegateParameterHelper::IsSupportedFunction(UFunction* Target, ELexUIEventDelegateParameterType& OutParamType)
{
	return IsFunctionCompatible(Target, OutParamType);
}

bool ULexUIEventDelegateParameterHelper::IsStillSupported(UFunction* Target, ELexUIEventDelegateParameterType InParamType)
{
	ELexUIEventDelegateParameterType ParamType;
	if (IsSupportedFunction(Target, ParamType))
	{
		if (ParamType == InParamType)
		{
			return true;
		}
	}
	return false;
}

FString ULexUIEventDelegateParameterHelper::ParameterTypeToName(ELexUIEventDelegateParameterType paramType, const UFunction* InFunction)
{
	FString ParamTypeString = "";
	switch (paramType)
	{
	case ELexUIEventDelegateParameterType::Empty:
		break;
	case ELexUIEventDelegateParameterType::Bool:
		ParamTypeString = "Bool";
		break;
	case ELexUIEventDelegateParameterType::Float:
		ParamTypeString = "Float";
		break;
	case ELexUIEventDelegateParameterType::Double:
		ParamTypeString = "Double";
		break;
	case ELexUIEventDelegateParameterType::Int8:
		ParamTypeString = "Int8";
		break;
	case ELexUIEventDelegateParameterType::UInt8:
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
	case ELexUIEventDelegateParameterType::Int16:
		ParamTypeString = "Int16";
		break;
	case ELexUIEventDelegateParameterType::UInt16:
		ParamTypeString = "UInt16";
		break;
	case ELexUIEventDelegateParameterType::Int32:
		ParamTypeString = "Int32";
		break;
	case ELexUIEventDelegateParameterType::UInt32:
		ParamTypeString = "UInt32";
		break;
	case ELexUIEventDelegateParameterType::Int64:
		ParamTypeString = "Int64";
		break;
	case ELexUIEventDelegateParameterType::UInt64:
		ParamTypeString = "UInt64";
		break;
	case ELexUIEventDelegateParameterType::Vector2:
		ParamTypeString = "Vector2";
		break;
	case ELexUIEventDelegateParameterType::Vector3:
		ParamTypeString = "Vector3";
		break;
	case ELexUIEventDelegateParameterType::Vector4:
		ParamTypeString = "Vector4";
		break;
	case ELexUIEventDelegateParameterType::Quaternion:
		ParamTypeString = "Quaternion";
		break;
	case ELexUIEventDelegateParameterType::Color:
		ParamTypeString = "Color";
		break;
	case ELexUIEventDelegateParameterType::LinearColor:
		ParamTypeString = "LinearColor";
		break;
	case ELexUIEventDelegateParameterType::String:
		ParamTypeString = "String";
		break;

	case ELexUIEventDelegateParameterType::Asset:
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
	case ELexUIEventDelegateParameterType::LexWidget:
	{
		TFieldIterator<FProperty> ParamIterator(InFunction);
		if (auto firstProperty = CastField<FObjectProperty>(*ParamIterator))
		{
			if (firstProperty->PropertyClass != ULexWidget::StaticClass())
			{
				ParamTypeString = firstProperty->PropertyClass->GetName() + "(LexWidget)";
			}
			else
			{
				ParamTypeString = "ULexWidget";
			}
		}
		else
		{
			ParamTypeString = "ULexWidget";
		}
	}
		break;
	case ELexUIEventDelegateParameterType::PointerEvent:
		ParamTypeString = "PointerEvent";
		break;
	case ELexUIEventDelegateParameterType::Class:
		ParamTypeString = "Class";
		break;
	case ELexUIEventDelegateParameterType::Rotator:
		ParamTypeString = "Rotator";
		break;
	case ELexUIEventDelegateParameterType::Name:
		ParamTypeString = "Name";
		break;
	case ELexUIEventDelegateParameterType::Text:
		ParamTypeString = "Text";
		break;
	default:
		break;
	}
	return ParamTypeString;
}



void FLexUIEventDelegateData::Execute()
{
	if (bUseNativeParameter)
	{
		auto errMsg = LOCTEXT("NativeParameterError", "LGUIEventDelegateData.Execute, If use NativeParameter, you must FireEvent with your own parameter!");
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
		return;
	}
	if (ParamType == ELexUIEventDelegateParameterType::None)
	{
		auto errMsg = LOCTEXT("NotValid", "LGUIEventDelegateData.Execute, Not valid LGUIEventDelegate.");
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
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
void FLexUIEventDelegateData::Execute(void* InParam, ELexUIEventDelegateParameterType InParameterType)
{
	if (ParamType == ELexUIEventDelegateParameterType::None)
	{
		auto errMsg = LOCTEXT("NotValid", "LGUIEventDelegateData.Execute, Not valid LGUIEventDelegate.");
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
		return;
	}

	if (bUseNativeParameter)//should use native parameter (pass in param)
	{
		if (ParamType != InParameterType)//function's supported parameter is equal to event's parameter
		{
			if (InParameterType == ELexUIEventDelegateParameterType::Double && ParamType == ELexUIEventDelegateParameterType::Float)
			{
				auto InValue = *((double*)InParam);
				auto ConvertValue = (float)InValue;
				InParam = &ConvertValue;
				auto errMsg = LOCTEXT("ParameterTypeNotEqual_DoubleToFloat", "LGUIEventDelegateData.Execute, Parameter type not equal, LGUI will automatic convert it from double to float.");
#if WITH_EDITOR
				FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
				UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
			}
			else if (InParameterType == ELexUIEventDelegateParameterType::Float && ParamType == ELexUIEventDelegateParameterType::Double)
			{
				auto InValue = *((float*)InParam);
				auto ConvertValue = (double)InValue;
				InParam = &ConvertValue;
				auto errMsg = LOCTEXT("ParameterTypeNotEqual_FloatToDouble", "LGUIEventDelegateData.Execute, Parameter type not equal, LGUI will automatic convert it from float to double.");
#if WITH_EDITOR
				FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
				UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
			}
			else
			{
				auto errMsg = LOCTEXT("ParameterTypeNotEqual", "LGUIEventDelegateData.Execute, Parameter type not equal!");
#if WITH_EDITOR
				FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
				UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
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
bool FLexUIEventDelegateData::CheckFunctionParameter()const
{
	if (ParamType == ELexUIEventDelegateParameterType::None)
	{
		return false;
	}

	auto TargetFunction = TargetObject->FindFunction(FunctionName);
	if (!TargetFunction)
	{
		return false;
	}
	if (!ULexUIEventDelegateParameterHelper::IsStillSupported(TargetFunction, ParamType))
	{
		return false;
	}

	return true;
}
#endif

bool FLexUIEventDelegateData::CheckTargetObject()
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
				if (HelperClass == ULexWidget::StaticClass())
				{
					TargetObject = HelperWidget;
				}
				else
				{
					if (HelperClass->IsChildOf(ULexVisual::StaticClass()))
					{
						TargetObject = HelperWidget->GetVisual();
					}
					else if (HelperClass->IsChildOf(ULexLayoutContainer::StaticClass()))
					{
						TargetObject = HelperWidget->GetLayoutContainer();
					}
					else if (HelperClass->IsChildOf(ULexLayoutSelf::StaticClass()))
					{
						TargetObject = HelperWidget->GetLayoutSelf();
					}
					else
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
									if (Comp->GetFName() == HelperComponentName)
									{
										TargetObject = Comp;
										return true;
									}
								}
								FString WidgetName = HelperWidget->GetDisplayName();
								UE_LOG(LGUI, Error, TEXT("[%s].%d Can't find component of name '%s' on widget '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *HelperComponentName.ToString(), *WidgetName);
							}
						}
					}
				}
			}
		}

		return IsValid(TargetObject);
	}
}
void FLexUIEventDelegateData::FindAndExecute(UObject* Target, void* ParamData)
{
	CacheFunction = Target->FindFunction(FunctionName);
	if (CacheFunction)
	{
		if (!ULexUIEventDelegateParameterHelper::IsStillSupported(CacheFunction, ParamType))
		{
			auto errMsg = FText::Format(LOCTEXT("FunctionNotSupport", "LGUIEventDelegateData.FindAndExecute, Target function: {0} not supported!"), FText::FromName(FunctionName));
#if WITH_EDITOR
			FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
			UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
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
		auto errMsg = FText::Format(LOCTEXT("FunctionNotExist", "LGUIEventDelegateData.FindAndExecute, Target function: {0} not exist!"), FText::FromName(FunctionName));
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
		UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
	}
}
void FLexUIEventDelegateData::ExecuteTargetFunction(UObject* Target, UFunction* Func)
{
	switch (ParamType)
	{
	case ELexUIEventDelegateParameterType::String:
	{
		FString TempString;
		auto FromBinary = FMemoryReader(ParamBuffer, false);
		FromBinary << TempString;
		Target->ProcessEvent(Func, &TempString);
	}
	break;
	case ELexUIEventDelegateParameterType::Name:
	{
		FName TempName;
		auto FromBinary = FMemoryReader(ParamBuffer, false);
		FromBinary << TempName;
		Target->ProcessEvent(Func, &TempName);
	}
	break;
	case ELexUIEventDelegateParameterType::Text:
	{
		FText TempText;
		auto FromBinary = FMemoryReader(ParamBuffer, false);
		FromBinary << TempText;
		Target->ProcessEvent(Func, &TempText);
	}
	break;
	case ELexUIEventDelegateParameterType::Asset:
	case ELexUIEventDelegateParameterType::LexWidget:
	case ELexUIEventDelegateParameterType::Class:
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
void FLexUIEventDelegateData::ExecuteTargetFunction(UObject* Target, UFunction* Func, void* ParamData)
{
	Target->ProcessEvent(Func, ParamData);
}

FLexUIEventDelegate::FLexUIEventDelegate()
{
}
FLexUIEventDelegate::FLexUIEventDelegate(ELexUIEventDelegateParameterType InParameterType)
{
	SupportParameterType = InParameterType;
}

bool FLexUIEventDelegate::IsBound()const
{
	return EventList.Num() != 0;
}
void FLexUIEventDelegate::FireEvent()const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Empty)
	{
		for (auto& item : EventList)
		{
			item.Execute();
		}
	}
	else
		LogParameterError(ELexUIEventDelegateParameterType::Empty);
}
void FLexUIEventDelegate::LogParameterError(ELexUIEventDelegateParameterType WrongParamType)const
{
	auto enumObject = FindObject<UEnum>(nullptr, TEXT("/Script/LGUI.ELexUIEventDelegateParameterType"), EFindObjectFlags::ExactClass);
	auto errMsg = FText::Format(LOCTEXT("ParameterTypeMismatch", "LexUIEventDelegate parameter type must be the same as your declaration. support parameter type: {0}, execute parameter type: {1}")
		, enumObject->GetDisplayNameTextByValue((int64)SupportParameterType)
		, enumObject->GetDisplayNameTextByValue((int64)WrongParamType)
	);
#if WITH_EDITOR
	FLexUIUtils::EditorNotification(errMsg, false, 10);
#endif
	UE_LOG(LGUI, Error, TEXT("[%s].%d %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *errMsg.ToString());
}
void FLexUIEventDelegate::FireEvent(void* InParam)const
{
	for (auto& item : EventList)
	{
		item.Execute(InParam, SupportParameterType);
	}
}

void FLexUIEventDelegate::FireEvent(bool InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Bool)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Bool);
}
void FLexUIEventDelegate::FireEvent(float InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Float)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Float);
}
void FLexUIEventDelegate::FireEvent(double InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Double)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Double);
}
void FLexUIEventDelegate::FireEvent(int8 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Int8)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Int8);
}
void FLexUIEventDelegate::FireEvent(uint8 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::UInt8)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::UInt8);
}
void FLexUIEventDelegate::FireEvent(int16 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Int16)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Int16);
}
void FLexUIEventDelegate::FireEvent(uint16 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::UInt16)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::UInt16);
}
void FLexUIEventDelegate::FireEvent(int32 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Int32)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Int32);
}
void FLexUIEventDelegate::FireEvent(uint32 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::UInt32)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::UInt32);
}
void FLexUIEventDelegate::FireEvent(int64 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Int64)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Int64);
}
void FLexUIEventDelegate::FireEvent(uint64 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::UInt64)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::UInt64);
}
void FLexUIEventDelegate::FireEvent(FVector2D InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Vector2)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Vector2);
}
void FLexUIEventDelegate::FireEvent(FVector InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Vector3)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Vector3);
}
void FLexUIEventDelegate::FireEvent(FVector4 InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Vector4)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Vector4);
}
void FLexUIEventDelegate::FireEvent(FColor InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Color)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Color);
}
void FLexUIEventDelegate::FireEvent(FLinearColor InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::LinearColor)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::LinearColor);
}
void FLexUIEventDelegate::FireEvent(FQuat InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Quaternion)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Quaternion);
}
void FLexUIEventDelegate::FireEvent(const FString& InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::String)
	{
		FireEvent((void*)&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::String);
}
void FLexUIEventDelegate::FireEvent(UObject* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Asset)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Asset);
}
void FLexUIEventDelegate::FireEvent(ULexWidget* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::LexWidget)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::LexWidget);
}
void FLexUIEventDelegate::FireEvent(ULexPointerEventData* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::PointerEvent)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::PointerEvent);
}
void FLexUIEventDelegate::FireEvent(UClass* InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Class)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Class);
}
void FLexUIEventDelegate::FireEvent(FRotator InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Rotator)
	{
		FireEvent(&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Rotator);
}
void FLexUIEventDelegate::FireEvent(const FName& InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Name)
	{
		FireEvent((void*)&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Name);
}
void FLexUIEventDelegate::FireEvent(const FText& InParam)const
{
	if (EventList.Num() == 0)return;
	if (SupportParameterType == ELexUIEventDelegateParameterType::Text)
	{
		FireEvent((void*)&InParam);
	}
	else LogParameterError(ELexUIEventDelegateParameterType::Text);
}

#if WITH_EDITOR
bool FLexUIEventDelegate::CheckFunctionParameter()const
{
	for (auto& item : EventList)
	{
		if (!item.CheckFunctionParameter())
		{
			return false;
		}
	}
	return true;
}
bool FLexUIEventDelegate::HasFunctionBinding(ULexUIBehaviour* InTargetComponent, FName InFunctionName)const
{
	for (auto& item : EventList)
	{
		if (item.TargetObject == InTargetComponent && item.FunctionName == InFunctionName)return true;
	}
	return false;
}
FName FLexUIEventDelegate::FindFunctionBoundToComponent(ULexUIBehaviour* InTargetComponent)const
{
	for (auto& item : EventList)
	{
		if (item.TargetObject == InTargetComponent && !item.FunctionName.IsNone())return item.FunctionName;
	}
	return NAME_None;
}
void FLexUIEventDelegate::AddFunctionBinding(ULexWidget* InHelperWidget, ULexUIBehaviour* InTargetComponent, FName InFunctionName, ELexUIEventDelegateParameterType InParamType, bool bInUseNativeParameter)
{
	if (InTargetComponent == nullptr)return;
	// FLexUIEventDelegate is a friend of FLexUIEventDelegateData, so the helper fields the
	// event customization normally fills can be set directly -- same result as picking the
	// component + function in the details panel by hand
	FLexUIEventDelegateData Data;
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


