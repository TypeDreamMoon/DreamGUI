// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamPointerEventData.h"
#include "DreamUIEventDelegate.generated.h"

class UDreamUIBehaviour;
class UDreamWidget;

#if WITH_EDITOR
struct DREAMGUI_API FDreamUIEventBindingValidationIssue
{
	int32 BindingIndex = INDEX_NONE;
	TWeakObjectPtr<UDreamWidget> TargetWidget;
	FName FunctionName;
	FString Message;
};
#endif


UENUM()
enum class EDreamUIEventDelegateParameterType :uint8
{
	/** not initialized */
	None		UMETA(Hidden),
	Empty,
	Bool		UMETA(DisplayName = "Boolean"),
	Float,
	Double,
	Int8		UMETA(Hidden),
	UInt8		UMETA(DisplayName = "UInt8\Enum\Byte"),
	Int16		UMETA(Hidden),
	UInt16		UMETA(Hidden),
	Int32		UMETA(DisplayName = "Integer"),
	UInt32		UMETA(Hidden),
	Int64		UMETA(Hidden),
	UInt64		UMETA(Hidden),
	Vector2		UMETA(DisplayName = "Vector2"),
	Vector3		UMETA(DisplayName = "Vector3"),
	Vector4		UMETA(DisplayName = "Vector4"),
	Color,
	LinearColor,
	Quaternion,
	String,
	/** for asset reference */
	Asset,
	/** for DreamWidget reference */
	DreamWidget,
	/** for DreamPointerEventData */
	PointerEvent	UMETA(DisplayName = "DreamPointerEventData"),
	/** Class for UClass reference */
	Class,
	
	Rotator,

	Name,
	Text,
};
/** helper class for finding function */
class DREAMGUI_API UDreamUIEventDelegateParameterHelper
{
public:
	static bool IsSupportedFunction(UFunction* Target, EDreamUIEventDelegateParameterType& OutParamType);
	static bool IsStillSupported(UFunction* Target, EDreamUIEventDelegateParameterType InParamType);
	static FString ParameterTypeToName(EDreamUIEventDelegateParameterType paramType, const UFunction* InFunction = nullptr);
	/** if first parameter is an object type, then return it's objectclass */
	static UClass* GetObjectParameterClass(const UFunction* InFunction);
	static UEnum* GetEnumParameter(const UFunction* InFunction);
	static UClass* GetClassParameterClass(const UFunction* InFunction);

	/**
	 * How many bytes a parameter of this type occupies in FDreamUIEventDelegateData::ParamBuffer.
	 * The buffer is NOT a serialization format: ExecuteTargetFunction hands it straight to
	 * UObject::ProcessEvent as the target function's parameter frame, so the only correct length is
	 * sizeof() of the parameter type as the compiler lays it out.
	 * @return the size, or 0 for the types that never travel through the raw buffer (None/Empty,
	 *		   String/Name/Text which are serialized at their own length, PointerEvent, and the
	 *		   reference types which live in ReferenceObject)
	 */
	static int32 GetParameterBufferSize(EDreamUIEventDelegateParameterType InParamType);
	/**
	 * How many bytes the same parameter occupied while the engine math types were single precision.
	 * @return the old size, or 0 for the types whose layout never changed
	 */
	static int32 GetLegacyParameterBufferSize(EDreamUIEventDelegateParameterType InParamType);
	/**
	 * Bring a stored buffer up to GetParameterBufferSize. A buffer saved at the single precision
	 * length holds real values, so it is widened component by component rather than thrown away;
	 * a buffer of any other unexpected length is zero-padded or truncated to fit.
	 * @return true if InOutBuffer was changed
	 */
	static bool UpgradeParameterBuffer(EDreamUIEventDelegateParameterType InParamType, TArray<uint8>& InOutBuffer);
private:
	static bool IsFunctionCompatible(const UFunction* InFunction, EDreamUIEventDelegateParameterType& OutParameterType);
	static bool IsPropertyCompatible(const FProperty* InFunctionProperty, EDreamUIEventDelegateParameterType& OutParameterType);
};

/**
 * Editable event type in editor
 */
USTRUCT()
struct DREAMGUI_API FDreamUIEventDelegateData
{
	GENERATED_BODY()
private:
	friend struct FDreamUIEventDelegate;
	friend class FDreamUIEventDelegateCustomization;
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")bool BoolValue = false;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")float FloatValue = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")double DoubleValue = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")int8 Int8Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")uint8 UInt8Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")int16 Int16Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")uint16 UInt16Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")int32 Int32Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")uint32 UInt32Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")int64 Int64Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")uint64 UInt64Value = 0;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FVector2D Vector2Value = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FVector Vector3Value = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FVector4 Vector4Value = FVector4(0, 0, 0, 0);
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FQuat QuatValue = FQuat::Identity;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FColor ColorValue = FColor::White;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FLinearColor LinearColorValue = FLinearColor::White;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FRotator RotatorValue = FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FString StringValue;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FName NameValue;
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")FText TextValue;
#endif

	/** target widget */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UDreamWidget> HelperWidget = nullptr;
	/** target object class. If class is DreamWidget then TargetObject is HelperWidget, if class is DreamUIBehaviour then TargetObject is the component. */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TObjectPtr<UClass> HelperClass = nullptr;
	/** if TargetObject is widget component and HelperWidget have multiple components, then select by component name. */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		FName HelperComponentName;

	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")
		TObjectPtr<UObject> TargetObject = nullptr;
	/** target function name */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FName FunctionName;
	/** target function supported parameter type */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamUIEventDelegateParameterType ParamType = EDreamUIEventDelegateParameterType::None;

	/** data buffer stores function's parameter */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TArray<uint8> ParamBuffer;
	/** Object reference, can reference widget/class/asset */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UObject> ReferenceObject = nullptr;

	/** use the function's native parameter? */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bUseNativeParameter = false;
private:
	UPROPERTY(Transient) TObjectPtr<UFunction> CacheFunction = nullptr;
public:
	void Execute();
	void Execute(void* InParam, EDreamUIEventDelegateParameterType InParameterType);
#if WITH_EDITOR
	/**
	 * Check if function parameter compatible with target function
	 * @return	true- is compatible, false- not
	 */
	bool CheckFunctionParameter()const;
	UObject* ResolveTargetForValidation(FString& OutError) const;
#endif
private:
	bool CheckTargetObject();
	void FindAndExecute(UObject* Target, void* ParamData = nullptr);
	void ExecuteTargetFunction(UObject* Target, UFunction* Func);
	void ExecuteTargetFunction(UObject* Target, UFunction* Func, void* ParamData);
};

/**
 * event or callback that can edit inside editor
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIEventDelegate
{
	GENERATED_BODY()

public:
	FDreamUIEventDelegate();
	FDreamUIEventDelegate(EDreamUIEventDelegateParameterType InParameterType);
private:
	friend class FDreamUIEventDelegateCustomization;
	/** event list */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		mutable TArray<FDreamUIEventDelegateData> EventList;
	/** supported parameter type of this event */
	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI", meta = (DisplayName = "NativeParameterType"))
		EDreamUIEventDelegateParameterType SupportParameterType = EDreamUIEventDelegateParameterType::Empty;
	/** Parameter type must be the same as your declaration of FDreamUIEventDelegate(DreamUIEventDelegateParameterType InParameterType) */
	void FireEvent(void* InParam)const;
	void LogParameterError(EDreamUIEventDelegateParameterType WrongParamType)const;
public:
	bool IsBound()const;
public:
	void FireEvent()const;
	void FireEvent(bool InParam)const;
	void FireEvent(float InParam)const;
	void FireEvent(double InParam)const;
	void FireEvent(int8 InParam)const;
	void FireEvent(uint8 InParam)const;
	void FireEvent(int16 InParam)const;
	void FireEvent(uint16 InParam)const;
	void FireEvent(int32 InParam)const;
	void FireEvent(uint32 InParam)const;
	void FireEvent(int64 InParam)const;
	void FireEvent(uint64 InParam)const;
	void FireEvent(FVector2D InParam)const;
	void FireEvent(FVector InParam)const;
	void FireEvent(FVector4 InParam)const;
	void FireEvent(FColor InParam)const;
	void FireEvent(FLinearColor InParam)const;
	void FireEvent(FQuat InParam)const; 
	void FireEvent(const FString& InParam)const;
	void FireEvent(UObject* InParam)const;
	void FireEvent(UDreamWidget* InParam)const;
	void FireEvent(UDreamPointerEventData* InParam)const;
	void FireEvent(UClass* InParam)const;
	void FireEvent(FRotator InParam)const;
	void FireEvent(const FName& InParam)const;
	void FireEvent(const FText& InParam)const;

#if WITH_EDITOR
	/**
	 * Check if function parameter compatible with target function
	 * @return	true- is compatible, false- not
	 */
	bool CheckFunctionParameter()const;
	/** Append one actionable issue for every invalid serialized binding. RootWidget limits targets to one prefab tree. */
	void GetValidationIssues(TArray<FDreamUIEventBindingValidationIssue>& OutIssues, const UDreamWidget* RootWidget = nullptr) const;
	/** Redirect bindings that target a behaviour instance after the prefab replaces its primary behaviour. */
	void ReplaceBindingTarget(UDreamUIBehaviour* InOldTarget, UDreamUIBehaviour* InNewTarget);

	/** This event's native parameter type (the value it fires with). */
	EDreamUIEventDelegateParameterType GetSupportParameterType()const { return SupportParameterType; }
	/**
	 * Editor: append a binding calling InTargetComponent's InFunctionName, wiring the helper
	 * fields so it survives serialization and shows in the customization -- the programmatic
	 * counterpart of picking a function in the details panel, for the designer's Event "+".
	 */
	void AddFunctionBinding(UDreamWidget* InHelperWidget, UDreamUIBehaviour* InTargetComponent, FName InFunctionName, EDreamUIEventDelegateParameterType InParamType, bool bInUseNativeParameter);
	/** True when any binding already targets InTargetComponent's InFunctionName. */
	bool HasFunctionBinding(UDreamUIBehaviour* InTargetComponent, FName InFunctionName)const;
	/** Function name of the first binding targeting InTargetComponent, or NAME_None (for "reuse the existing handler"). */
	FName FindFunctionBoundToComponent(UDreamUIBehaviour* InTargetComponent)const;
#endif
};
