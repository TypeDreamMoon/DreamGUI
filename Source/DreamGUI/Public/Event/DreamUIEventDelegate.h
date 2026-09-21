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
	// The narrow and unsigned integers are offered like any other type: every one of them has a
	// buffer size (GetParameterBufferSize), a typed field on FDreamUIEventDelegateData, an editor row
	// and a change handler, so the only thing Hidden bought was an author who could not declare a
	// native event carrying the argument their own C++ delegate already passes. None stays hidden --
	// it is the enum's "not initialized", not a type anyone can mean.
	Int8,
	UInt8		UMETA(DisplayName = "UInt8\Enum\Byte"),
	Int16,
	UInt16,
	Int32		UMETA(DisplayName = "Integer"),
	UInt32,
	Int64,
	UInt64,
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

	/**
	 * Any other USTRUCT. Appended rather than slotted in beside the named struct types above because
	 * these values are serialized as their NUMBER: inserting anywhere else would silently re-read every
	 * saved binding as a different type.
	 *
	 * The value is stored as exported text (FDreamUIEventDelegateData::StructValue), not as raw bytes
	 * in ParamBuffer. A struct is free to own heap memory -- an FString member, a TArray -- and a byte
	 * copy of one is a copy of the pointers, which ProcessEvent would then read as another object's
	 * memory and the callee's destructor would free twice. Text is what every other part of the engine
	 * that has to persist an arbitrary struct literal uses, for the same reason.
	 */
	Struct		UMETA(DisplayName = "Struct"),
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
	/** If the first parameter is a struct, the struct it is. The type of a `Struct` parameter is never stored: it is whatever the function declares today. */
	static UScriptStruct* GetStructParameter(const UFunction* InFunction);

	/**
	 * How many bytes a parameter of this type occupies in FDreamUIEventDelegateData::ParamBuffer.
	 * The buffer is NOT a serialization format: ExecuteTargetFunction hands it straight to
	 * UObject::ProcessEvent as the target function's parameter frame, so the only correct length is
	 * sizeof() of the parameter type as the compiler lays it out.
	 * @return the size, or 0 for the types that never travel through the raw buffer (None/Empty,
	 *		   String/Name/Text which are serialized at their own length, Struct which travels as
	 *		   exported text, PointerEvent, and the reference types which live in ReferenceObject)
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

	/**
	 * The object a binding points at, given the helper fields that describe it. ONE implementation,
	 * because the runtime resolve, the editor panel and the validation pass all have to agree about
	 * what a binding means, and three copies of this walk were three chances to disagree.
	 *
	 * Behaviours are addressed by POSITION in UDreamWidget::GetAllComponents(), exactly as
	 * FDreamWidgetPropertyBinding::BehaviourIndex is: an instanced sub-object's FName is assigned by
	 * UE's own auto-numbering, so a preview rebuilt from the authoring tree does not get the same one
	 * back, and a name-keyed binding silently lost its target every time the designer rebuilt.
	 * InHelperComponentName is the LEGACY key and is only consulted when the index is INDEX_NONE (an
	 * asset saved before the index existed); OutResolvedComponentIndex then reports the position that
	 * name resolved to, so the caller can write it back and stop depending on the name.
	 *
	 * @param OutResolvedComponentIndex	receives the behaviour's position, or INDEX_NONE
	 * @param OutError					receives a sentence naming what was missing, when it fails
	 */
	static UObject* ResolveBindingTarget(const UDreamWidget* InHelperWidget, const UClass* InHelperClass,
		int32 InHelperComponentIndex, FName InHelperComponentName,
		int32* OutResolvedComponentIndex = nullptr, FString* OutError = nullptr);
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
	/**
	 * Which behaviour, when HelperClass is a UDreamUIBehaviour: its position in the widget's component
	 * array. The same key FDreamWidgetPropertyBinding::BehaviourIndex uses, and for the same reason --
	 * an instanced sub-object and its authored copy share no name, and UE re-numbers the instance's
	 * FName on every preview rebuild. INDEX_NONE means "not recorded"; see HelperComponentName.
	 */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		int32 HelperComponentIndex = INDEX_NONE;
	/**
	 * LEGACY key, kept so assets saved before HelperComponentIndex existed still resolve. Consulted
	 * only when the index is INDEX_NONE, and the index is written back the first time it resolves.
	 */
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
	/**
	 * A `Struct` parameter's value, as exported text. Not in ParamBuffer: see the enum's Struct entry.
	 * Empty means "the struct's own defaults", which is what a freshly picked function should send.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FString StructValue;
	/**
	 * Which struct StructValue was written for. The type actually sent is always the one the target
	 * function declares TODAY; this is here so a mismatch is an error rather than a partial parse --
	 * two unrelated structs can both accept a text literal that names a member they happen to share,
	 * and quietly sending half a value is worse than sending the defaults and saying so.
	 */
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		TObjectPtr<UScriptStruct> StructValueType = nullptr;
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
	/**
	 * Is CacheFunction still callable on Target? False when the function was garbage collected, when its
	 * owning class was replaced by a blueprint recompile, or when Target was re-resolved to another class.
	 */
	bool IsCacheFunctionValidFor(const UObject* Target) const;
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
	/**
	 * Route this event to InHandlerObject's InFunctionName, for as long as that object lives.
	 *
	 * The counterpart of FMulticastDelegateProperty::AddDelegate, and it exists so that ONE authoring
	 * mechanism covers every event in the plugin: `EventName -> Handler` (a `.dui` line, or the Events
	 * section of the details panel) is resolved by the compiler and bound by
	 * UDreamUserWidget::BindEventBindings, which needs a way to attach to the events that are an
	 * FDreamUIEventDelegate rather than a dynamic multicast delegate.
	 *
	 * The entry it appends is runtime-only: TargetObject is Transient, and nothing else it sets is
	 * saved with the asset, so a routed event adds nothing to what gets serialized. Bound entries are
	 * indistinguishable from authored ones at fire time, which is the point -- the legacy list is the
	 * mechanism, and `->` is the way it is spelled.
	 */
	void AddRuntimeRoute(UObject* InHandlerObject, FName InFunctionName);
	/** The value this event fires with; the handler of a `->` route has to accept exactly this. */
	EDreamUIEventDelegateParameterType GetNativeParameterType()const { return SupportParameterType; }
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
	/**
	 * Editor: the value a `Struct` binding sends, as exported text plus the struct it was written for.
	 * The programmatic counterpart of the panel's struct editor, and the same pair it writes -- the
	 * type travels with the text so a signature change is reported instead of half-parsed.
	 */
	void SetStructParameterValue(int32 InBindingIndex, UScriptStruct* InStruct, const FString& InExportedText);
	/** True when any binding already targets InTargetComponent's InFunctionName. */
	bool HasFunctionBinding(UDreamUIBehaviour* InTargetComponent, FName InFunctionName)const;
	/** Function name of the first binding targeting InTargetComponent, or NAME_None (for "reuse the existing handler"). */
	FName FindFunctionBoundToComponent(UDreamUIBehaviour* InTargetComponent)const;
#endif
};
