// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Event/LexUIEventDelegate.h"
#include "LexUIEventDelegate_PresetParameter.generated.h"

#define MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(EventDelegateParamType, ParamType)\
DECLARE_DELEGATE_OneParam(FLexUIEventDelegate_##EventDelegateParamType##_Delegate, ParamType);\
DECLARE_MULTICAST_DELEGATE_OneParam(FLexUIEventDelegate_##EventDelegateParamType##_MulticastDelegate, ParamType);


#define MAKE_EVENTDELEGATE_PRESETPARAM(EventDelegateParamType, ParamType)\
public:\
	FLexUIEventDelegate_##EventDelegateParamType() :FLexUIEventDelegate(ELexUIEventDelegateParameterType::EventDelegateParamType) {}\
private:\
	mutable FLexUIEventDelegate_##EventDelegateParamType##_MulticastDelegate eventDelegate;\
public:\
	FDelegateHandle Register(const TFunction<void(ParamType)>& function)const\
	{\
		return eventDelegate.AddLambda(function);\
	}\
	FDelegateHandle Register(const FLexUIEventDelegate_##EventDelegateParamType##_Delegate& function)const\
	{\
		return eventDelegate.Add(function);\
	}\
	void Unregister(const FDelegateHandle& delegateHandle)const\
	{\
		eventDelegate.Remove(delegateHandle);\
	}\
	void operator() (ParamType InParam)const\
	{\
		FLexUIEventDelegate::FireEvent(InParam);\
		if (eventDelegate.IsBound())eventDelegate.Broadcast(InParam);\
	}



DECLARE_DELEGATE(FLexUIEventDelegate_Empty_Delegate); 
DECLARE_MULTICAST_DELEGATE(FLexUIEventDelegate_Empty_MulticastDelegate);
DECLARE_DYNAMIC_DELEGATE(FLexUIEventDelegate_Empty_DynamicDelegate);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Empty : public FLexUIEventDelegate
{
	GENERATED_BODY()
public:
	FLexUIEventDelegate_Empty() :FLexUIEventDelegate(ELexUIEventDelegateParameterType::Empty) {}
private:
	mutable FLexUIEventDelegate_Empty_MulticastDelegate eventDelegate;
public:
	FDelegateHandle Register(const TFunction<void()>& function)const
	{
		return eventDelegate.AddLambda(function);
	}
	FDelegateHandle Register(const FLexUIEventDelegate_Empty_Delegate& function)const
	{
		return eventDelegate.Add(function);
	}
	void Unregister(const FDelegateHandle& delegateHandle)const
	{
		eventDelegate.Remove(delegateHandle);
	}
	void operator() ()const
	{
		FLexUIEventDelegate::FireEvent();
		if (eventDelegate.IsBound())eventDelegate.Broadcast();
	}
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Bool, bool);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Bool_DynamicDelegate, bool, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Bool : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Bool, bool);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Float, float);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Float_DynamicDelegate, float, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Float : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Float, float);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Double, double);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Double_DynamicDelegate, double, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Double : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Double, double);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int8, int8);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Int8_DynamicDelegate, int8, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Int8 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int8, int8);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt8, uint8);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_UInt8_DynamicDelegate, uint8, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_UInt8 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt8, uint8);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int16, int16);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Int16_DynamicDelegate, int16, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Int16 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int16, int16);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt16, uint16);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_UInt16_DynamicDelegate, uint16, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_UInt16 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt16, uint16);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int32, int32);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Int32_DynamicDelegate, int32, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Int32 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int32, int32);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt32, uint32);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_UInt32_DynamicDelegate, uint32, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_UInt32 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt32, uint32);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Int64, int64);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Int64_DynamicDelegate, int64, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Int64 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Int64, int64);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(UInt64, uint64);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_UInt64_DynamicDelegate, uint64, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_UInt64 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(UInt64, uint64);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Vector2, FVector2D);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Vector2_DynamicDelegate, FVector2D, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Vector2 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Vector2, FVector2D);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Vector3, FVector);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Vector3_DynamicDelegate, FVector, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Vector3 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Vector3, FVector);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Vector4, FVector4);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Vector4_DynamicDelegate, FVector4, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Vector4 : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Vector4, FVector4);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Color, FColor);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Color_DynamicDelegate, FColor, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Color : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Color, FColor);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(LinearColor, FLinearColor);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_LinearColor_DynamicDelegate, FLinearColor, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_LinearColor : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(LinearColor, FLinearColor);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Quaternion, FQuat);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Quaternion_DynamicDelegate, FQuat, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Quaternion : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Quaternion, FQuat);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(String, FString);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_String_DynamicDelegate, FString, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_String : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(String, FString);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Object, UObject*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Object_DynamicDelegate, UObject*, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Object : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Object, UObject*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Actor, AActor*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Actor_DynamicDelegate, AActor*, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Actor : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Actor, AActor*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(PointerEvent, ULexPointerEventData*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_PointerEvent_DynamicDelegate, ULexPointerEventData*, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_PointerEvent : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(PointerEvent, ULexPointerEventData*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Class, UClass*);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Class_DynamicDelegate, UClass*, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Class : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Class, UClass*);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Rotator, FRotator);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Rotator_DynamicDelegate, FRotator, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Rotator : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Rotator, FRotator);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Text, FText);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Text_DynamicDelegate, FText, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Text : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Text, FText);
};

MAKE_EVENTDELEGATE_PRESETPARAM_DELEGATE(Name, FName);
DECLARE_DYNAMIC_DELEGATE_OneParam(FLexUIEventDelegate_Name_DynamicDelegate, FName, value);
USTRUCT(BlueprintType)
struct LGUI_API FLexUIEventDelegate_Name : public FLexUIEventDelegate
{
	GENERATED_BODY()
	MAKE_EVENTDELEGATE_PRESETPARAM(Name, FName);
};
