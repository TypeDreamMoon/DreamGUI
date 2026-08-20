// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Event/DreamUIEventDelegate.h"
#include "DreamUIEventDelegate_PresetParameter.generated.h"

#define MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(EventDelegateParamType, ParamType)\
DECLARE_DELEGATE_OneParam(FDreamUIEventDelegate_##EventDelegateParamType##_Delegate, ParamType);\
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIEventDelegate_##EventDelegateParamType##_MulticastDelegate, ParamType);


#define MAKE_EVENTDELEGATE_PRESETPARAM(EventDelegateParamType, ParamType)\
public:\
	FDreamUIEventDelegate_##EventDelegateParamType() :FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::EventDelegateParamType) {}\
private:\
	mutable FDreamUIEventDelegate_##EventDelegateParamType##_MulticastDelegate eventDelegate;\
public:\
	FDelegateHandle Register(const TFunction<void(ParamType)>& function)const\
	{\
		return eventDelegate.AddLambda(function);\
	}\
	FDelegateHandle Register(const FDreamUIEventDelegate_##EventDelegateParamType##_Delegate& function)const\
	{\
		return eventDelegate.Add(function);\
	}\
	void Unregister(const FDelegateHandle& delegateHandle)const\
	{\
		eventDelegate.Remove(delegateHandle);\
	}\
	void operator() (ParamType InParam)const\
	{\
		FDreamUIEventDelegate::FireEvent(InParam);\
		if (eventDelegate.IsBound())eventDelegate.Broadcast(InParam);\
	}



DECLARE_DELEGATE(FDreamUIEventDelegate_Empty_Delegate); 
DECLARE_MULTICAST_DELEGATE(FDreamUIEventDelegate_Empty_MulticastDelegate);
DECLARE_DYNAMIC_DELEGATE(FDreamUIEventDelegate_Empty_DynamicDelegate);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Empty : public FDreamUIEventDelegate
{
	GENERATED_BODY()
public:
	FDreamUIEventDelegate_Empty() :FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Empty) {}
private:
	mutable FDreamUIEventDelegate_Empty_MulticastDelegate eventDelegate;
public:
	FDelegateHandle Register(const TFunction<void()>& function)const
	{
		return eventDelegate.AddLambda(function);
	}
	FDelegateHandle Register(const FDreamUIEventDelegate_Empty_Delegate& function)const
	{
		return eventDelegate.Add(function);
	}
	void Unregister(const FDelegateHandle& delegateHandle)const
	{
		eventDelegate.Remove(delegateHandle);
	}
	void operator() ()const
	{
		FDreamUIEventDelegate::FireEvent();
		if (eventDelegate.IsBound())eventDelegate.Broadcast();
	}
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Bool, bool);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Bool_DynamicDelegate, bool, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Bool : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Bool, bool);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Float, float);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Float_DynamicDelegate, float, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Float : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Float, float);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Double, double);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Double_DynamicDelegate, double, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Double : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Double, double);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int8, int8);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Int8_DynamicDelegate, int8, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Int8 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int8, int8);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt8, uint8);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_UInt8_DynamicDelegate, uint8, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_UInt8 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt8, uint8);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int16, int16);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Int16_DynamicDelegate, int16, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Int16 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int16, int16);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt16, uint16);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_UInt16_DynamicDelegate, uint16, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_UInt16 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt16, uint16);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int32, int32);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Int32_DynamicDelegate, int32, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Int32 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int32, int32);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt32, uint32);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_UInt32_DynamicDelegate, uint32, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_UInt32 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt32, uint32);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int64, int64);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Int64_DynamicDelegate, int64, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Int64 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int64, int64);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt64, uint64);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_UInt64_DynamicDelegate, uint64, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_UInt64 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt64, uint64);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Vector2, FVector2D);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Vector2_DynamicDelegate, FVector2D, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Vector2 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Vector2, FVector2D);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Vector3, FVector);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Vector3_DynamicDelegate, FVector, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Vector3 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Vector3, FVector);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Vector4, FVector4);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Vector4_DynamicDelegate, FVector4, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Vector4 : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Vector4, FVector4);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Color, FColor);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Color_DynamicDelegate, FColor, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Color : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Color, FColor);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(LinearColor, FLinearColor);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_LinearColor_DynamicDelegate, FLinearColor, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_LinearColor : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(LinearColor, FLinearColor);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Quaternion, FQuat);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Quaternion_DynamicDelegate, FQuat, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Quaternion : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Quaternion, FQuat);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(String, FString);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_String_DynamicDelegate, FString, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_String : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(String, FString);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Asset, UObject*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Asset_DynamicDelegate, UObject*, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Asset : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Asset, UObject*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(DreamWidget, UDreamWidget*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_DreamWidget_DynamicDelegate, UDreamWidget*, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_DreamWidget : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(DreamWidget, UDreamWidget*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(PointerEvent, UDreamPointerEventData*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_PointerEvent_DynamicDelegate, UDreamPointerEventData*, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_PointerEvent : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(PointerEvent, UDreamPointerEventData*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Class, UClass*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Class_DynamicDelegate, UClass*, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Class : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Class, UClass*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Rotator, FRotator);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Rotator_DynamicDelegate, FRotator, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Rotator : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Rotator, FRotator);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Text, FText);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Text_DynamicDelegate, FText, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Text : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Text, FText);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Name, FName);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamUIEventDelegate_Name_DynamicDelegate, FName, value);
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate_Name : public FDreamUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Name, FName);
};
