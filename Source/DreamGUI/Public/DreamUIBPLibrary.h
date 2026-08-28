// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Core/DreamUserWidget.h"

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamUIComponentReference.h"
#include "DreamUIDelegateHandleWrapper.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamUIEventDelegate_PresetParameter.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include LEXUIPREFAB_SERIALIZER_NEWEST_INCLUDE
#include "DreamUIBPLibrary.generated.h"

namespace LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE
{
	struct FDuplicateWidgetDataContainer;
}

USTRUCT(BlueprintType)
struct FDreamUIDuplicateDataContainer
{
	GENERATED_BODY()
public:
	bool bIsValid = false;
	LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::FDuplicateWidgetDataContainer DuplicateData;
};

UCLASS()
class DREAMGUI_API UDreamUIBPLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Make a widget that exists but is not on screen -- the counterpart of UMG's
	 * WidgetTree::ConstructWidget<T>(). It draws nothing and its behaviours do not run until you
	 * hand it to AddChild or AddToViewport; until then it is held by the DreamUI manager, so it will
	 * not be collected out from under you while you configure it.
	 *
	 * VisualClass is what the widget looks like (DreamImage, DreamText, ...). Leave it null for a bare
	 * widget, which is what a pure container is. Give it a layout container afterwards to make it a
	 * panel -- in this fork "panel" is a subobject, not a separate class.
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "VisualClass"), Category = "DreamGUI|Create")
	static UDreamWidget* ConstructWidget(UObject* WorldContextObject, const FString& DisplayName,
		TSubclassOf<class UDreamVisual> VisualClass);

	/**
	 * Instantiate a prefab without putting it anywhere -- the counterpart of UMG's
	 * CreateWidget(WidgetBlueprintClass). Same not-on-screen state as ConstructWidget, including a
	 * prefab whose root carries its own UDreamCanvas, which would otherwise start rendering the
	 * moment it existed.
	 *
	 * The prefab's own logic lives on the root widget's behaviour components, so reach it with
	 * GetComponent rather than by casting the returned pointer.
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", UnsafeDuringActorConstruction = "true"), Category = "DreamGUI|Create")
	static UDreamWidget* CreateDreamWidgetOfClass(UObject* WorldContextObject, TSubclassOf<class UDreamUserWidget> InWidgetClass);

	/** Return the world's shared ScreenSpaceOverlay root, creating it on demand. */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "DreamGUI|Screen")
	static UDreamWidget* GetOrCreateScreenSpaceUIRoot(UObject* WorldContextObject);

	/** Load a prefab under the shared screen root and track it as a viewport page. */
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "InCallbackBeforeAwake,SortOrder", UnsafeDuringActorConstruction = "true", WorldContext = "WorldContextObject", AutoCreateRefTerm = "InCallbackBeforeAwake"), Category = "DreamGUI|Screen")
	static UDreamWidget* AddWidgetOfClassToScreen(UObject* WorldContextObject, TSubclassOf<class UDreamUserWidget> InWidgetClass, const FDreamUIWidgetCreatedCallback& InCallbackBeforeAlive, int32 SortOrder = 0);

	/** UMG-style CreateWidget + AddToViewport convenience node for a DreamUI prefab. */
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SortOrder", UnsafeDuringActorConstruction = "true", WorldContext = "WorldContextObject"), Category = "DreamGUI|Screen")
	static UDreamWidget* AddWidgetOfClassToViewport(UObject* WorldContextObject, TSubclassOf<class UDreamUserWidget> InWidgetClass, int32 SortOrder = 0);

	/** Destroy a tracked viewport page and remove it from the screen registry. */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "DreamGUI|Screen")
	static void RemoveFromViewport(UObject* WorldContextObject, UDreamWidget* InRoot);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject"), Category = "DreamGUI|Screen")
	static bool IsInViewport(UObject* WorldContextObject, UDreamWidget* InRoot);
	/**
	 * Put a root widget made with ConstructWidget into the world: the counterpart of AddToViewport
	 * for world-space UI, and what a prefab presenter does for the tree it loads. The root must carry
	 * a DreamCanvas (the canvas decides the render mode and owns the meshes); it is attached to
	 * InSceneComponent, which gives it its world transform, and leaves the not-yet-added state.
	 * Returns false, and changes nothing, when InRoot has a parent or no canvas.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|World")
	static bool AttachWidgetToSceneComponent(UDreamWidget* InRoot, USceneComponent* InSceneComponent);

	/**
	 * Duplicate actor and all it's children actors
	 * If duplicate same actor for multiple times, then use PrepareDuplicateData node to get data, and pass the data to DuplicateActorWithPreparedData.
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", UnsafeDuringActorConstruction = "true", ToolTip = "Duplicate actor with hierarchy"), Category = DreamGUI)
		static UDreamWidget* DuplicateWidget(UObject* WorldContextObject, UDreamWidget* Target, UDreamWidget* Parent);
	/**
	 * Optimized version of DuplicateActor node when you need to duplicate same actor for multiple times. Use the result data in DuplicateActorWithPreparedData node.
	 */
	UFUNCTION(BlueprintCallable, meta = (UnsafeDuringActorConstruction = "true"), Category = DreamGUI)
		static void PrepareDuplicateData(UDreamWidget* Target, FDreamUIDuplicateDataContainer& Data);
	/**
	 * Use this with PrepareDuplicateData node.
	 */
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject", UnsafeDuringActorConstruction = "true"), Category = DreamGUI)
		static UDreamWidget* DuplicateWidgetWithPreparedData(UObject* WorldContextObject, UPARAM(Ref) FDreamUIDuplicateDataContainer& Data, UDreamWidget* Parent);
	template<class T>
	static T* DuplicateWidgetT(T* Target, USceneComponent* Parent)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const AActor>::Value, "'T' template parameter to DuplicateActor must be derived from AActor");
		return (T*)UDreamUIBPLibrary::DuplicateWidget(Target, Parent);
	}

	/**
	 * Find the first component in parent and up parent hierarchy with type.
	 * @param IncludeSelf	Include actor self.
	 * @param InStopNode	If parent is InStopNode then break the search chain. Can be null to ignore it.
	 */
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (ComponentClass = "/Script/Engine.ActorComponent", DeterminesOutputType = "ComponentClass"))
		static UActorComponent* GetComponentInParent(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, bool IncludeSelf = true, AActor* InStopNode = nullptr);
	/**
	 * Find all compoents in children with type.
	 * @param InActor Root actor to start from.
	 * @param ComponentClass The component type that need to search.
	 * @param IncludeSelf true- also search component at InActor.
	 * @param InExcludeNode If any child actor is included in this InExcludeNode, will skip that child actor and all it's children.
	 */
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (ComponentClass = "/Script/Engine.ActorComponent", DeterminesOutputType = "ComponentClass", AutoCreateRefTerm="InExcludeNode"))
		static TArray<UActorComponent*> GetComponentsInChildren(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, bool IncludeSelf, const TSet<AActor*>& InExcludeNode);
	/**
	 * Find the first component in children with type.
	 * @param InActor Root actor to start from.
	 * @param ComponentClass The component type that need to search.
	 * @param IncludeSelf true- also search component at InActor.
	 * @param InExcludeNode If any child actor is included in this InExcludeNode, will skip that child actor and all it's children.
	 */
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (ComponentClass = "/Script/Engine.ActorComponent", DeterminesOutputType = "ComponentClass", AutoCreateRefTerm = "InExcludeNode"))
		static UActorComponent* GetComponentInChildren(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, bool IncludeSelf, const TSet<AActor*>& InExcludeNode);

public:
#pragma region EventDelegate
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteEmpty(const FDreamUIEventDelegate& InEvent) { InEvent.FireEvent(); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteBool(const FDreamUIEventDelegate& InEvent, const bool& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteFloat(const FDreamUIEventDelegate& InEvent, const float& InParameter) { InEvent.FireEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamGUIEventDelegateExecuteDouble(const FDreamGUIEventDelegate& InEvent, const double& InParameter) { InEvent.FireEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamGUIEventDelegateExecuteInt8(const FDreamGUIEventDelegate& InEvent, const int8& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteUInt8(const FDreamUIEventDelegate& InEvent, const uint8& InParameter) { InEvent.FireEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamGUIEventDelegateExecuteInt16(const FDreamGUIEventDelegate& InEvent, const int16& InParameter) { InEvent.FireEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamGUIEventDelegateExecuteUInt16(const FDreamGUIEventDelegate& InEvent, const uint16& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteInt32(const FDreamUIEventDelegate& InEvent, const int32& InParameter) { InEvent.FireEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamGUIEventDelegateExecuteUInt32(const FDreamGUIEventDelegate& InEvent, const uint32& InParameter) { InEvent.FireEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamGUIEventDelegateExecuteInt64(const FDreamGUIEventDelegate& InEvent, const int64& InParameter) { InEvent.FireEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamGUIEventDelegateExecuteUInt64(const FDreamGUIEventDelegate& InEvent, const uint64& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteVector2(const FDreamUIEventDelegate& InEvent, const FVector2D& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteVector3(const FDreamUIEventDelegate& InEvent, const FVector& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteVector4(const FDreamUIEventDelegate& InEvent, const FVector4& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteColor(const FDreamUIEventDelegate& InEvent, const FColor& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteLinearColor(const FDreamUIEventDelegate& InEvent, const FLinearColor& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteQuaternion(const FDreamUIEventDelegate& InEvent, const FQuat& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteString(const FDreamUIEventDelegate& InEvent, const FString& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteObject(const FDreamUIEventDelegate& InEvent, UObject* InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteActor(const FDreamUIEventDelegate& InEvent, AActor* InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteClass(const FDreamUIEventDelegate& InEvent, UClass* InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecutePointerEvent(const FDreamUIEventDelegate& InEvent, UDreamPointerEventData* InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteRotator(const FDreamUIEventDelegate& InEvent, const FRotator& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteText(const FDreamUIEventDelegate& InEvent, const FText& InParameter) { InEvent.FireEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)static void DreamUIEventDelegateExecuteName(const FDreamUIEventDelegate& InEvent, const FName& InParameter) { InEvent.FireEvent(InParameter); }


	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Empty_Execute(const FDreamUIEventDelegate_Empty& InEvent) { InEvent(); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Empty_Register(const FDreamUIEventDelegate_Empty& InEvent, FDreamUIEventDelegate_Empty_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Empty_Unregister(const FDreamUIEventDelegate_Empty& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Bool_Execute(const FDreamUIEventDelegate_Bool& InEvent, bool InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Bool_Register(const FDreamUIEventDelegate_Bool& InEvent, FDreamUIEventDelegate_Bool_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Bool_Unregister(const FDreamUIEventDelegate_Bool& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Float_Execute(const FDreamUIEventDelegate_Float& InEvent, float InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Float_Register(const FDreamUIEventDelegate_Float& InEvent, FDreamUIEventDelegate_Float_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Float_Unregister(const FDreamUIEventDelegate_Float& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Double_Execute(const FDreamUIEventDelegate_Double& InEvent, double InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Double_Register(const FDreamUIEventDelegate_Double& InEvent, FDreamUIEventDelegate_Double_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Double_Unregister(const FDreamUIEventDelegate_Double& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
	//	static void DreamUIEventDelegate_Int8_Execute(const FDreamUIEventDelegate_Int8& InEvent, int8 InParameter) { InEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
	//	static FDreamGUIDelegateHandleWrapper DreamUIEventDelegate_Int8_Register(const FDreamUIEventDelegate_Int8& InEvent, FDreamUIEventDelegate_Int8_DynamicDelegate InDelegate);
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
	//	static void DreamUIEventDelegate_Int8_Unregister(const FDreamUIEventDelegate_Int8& InEvent, const FDreamGUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_UInt8_Execute(const FDreamUIEventDelegate_UInt8& InEvent, uint8 InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_UInt8_Register(const FDreamUIEventDelegate_UInt8& InEvent, FDreamUIEventDelegate_UInt8_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_UInt8_Unregister(const FDreamUIEventDelegate_UInt8& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
	//	static void DreamUIEventDelegate_Int16_Execute(const FDreamUIEventDelegate_Int16& InEvent, int16 InParameter) { InEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
	//	static FDreamGUIDelegateHandleWrapper DreamUIEventDelegate_Int16_Register(const FDreamUIEventDelegate_Int16& InEvent, FDreamUIEventDelegate_Int16_DynamicDelegate InDelegate);
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
	//	static void DreamUIEventDelegate_Int16_Unregister(const FDreamUIEventDelegate_Int16& InEvent, const FDreamGUIDelegateHandleWrapper& InDelegateHandle);

	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
	//	static void DreamUIEventDelegate_UInt16_Execute(const FDreamUIEventDelegate_UInt16& InEvent, uint16 InParameter) { InEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
	//	static FDreamGUIDelegateHandleWrapper DreamUIEventDelegate_UInt16_Register(const FDreamUIEventDelegate_UInt16& InEvent, FDreamUIEventDelegate_UInt16_DynamicDelegate InDelegate);
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
	//	static void DreamUIEventDelegate_UInt16_Unregister(const FDreamUIEventDelegate_UInt16& InEvent, const FDreamGUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Int32_Execute(const FDreamUIEventDelegate_Int32& InEvent, int32 InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Int32_Register(const FDreamUIEventDelegate_Int32& InEvent, FDreamUIEventDelegate_Int32_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Int32_Unregister(const FDreamUIEventDelegate_Int32& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
	//	static void DreamUIEventDelegate_UInt32_Execute(const FDreamUIEventDelegate_UInt32& InEvent, uint32 InParameter) { InEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
	//	static FDreamGUIDelegateHandleWrapper DreamUIEventDelegate_UInt32_Register(const FDreamUIEventDelegate_UInt32& InEvent, FDreamUIEventDelegate_UInt32_DynamicDelegate InDelegate);
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
	//	static void DreamUIEventDelegate_UInt32_Unregister(const FDreamUIEventDelegate_UInt32& InEvent, const FDreamGUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Int64_Execute(const FDreamUIEventDelegate_Int64& InEvent, int64 InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Int64_Register(const FDreamUIEventDelegate_Int64& InEvent, FDreamUIEventDelegate_Int64_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Int64_Unregister(const FDreamUIEventDelegate_Int64& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
	//	static void DreamUIEventDelegate_UInt64_Execute(const FDreamUIEventDelegate_UInt64& InEvent, uint64 InParameter) { InEvent(InParameter); }
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
	//	static FDreamGUIDelegateHandleWrapper DreamUIEventDelegate_UInt64_Register(const FDreamUIEventDelegate_UInt64& InEvent, FDreamUIEventDelegate_UInt64_DynamicDelegate InDelegate);
	//UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
	//	static void DreamUIEventDelegate_UInt64_Unregister(const FDreamUIEventDelegate_UInt64& InEvent, const FDreamGUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Vector2_Execute(const FDreamUIEventDelegate_Vector2& InEvent, FVector2D InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Vector2_Register(const FDreamUIEventDelegate_Vector2& InEvent, FDreamUIEventDelegate_Vector2_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Vector2_Unregister(const FDreamUIEventDelegate_Vector2& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Vector3_Execute(const FDreamUIEventDelegate_Vector3& InEvent, FVector InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Vector3_Register(const FDreamUIEventDelegate_Vector3& InEvent, FDreamUIEventDelegate_Vector3_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Vector3_Unregister(const FDreamUIEventDelegate_Vector3& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Vector4_Execute(const FDreamUIEventDelegate_Vector4& InEvent, FVector4 InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Vector4_Register(const FDreamUIEventDelegate_Vector4& InEvent, FDreamUIEventDelegate_Vector4_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Vector4_Unregister(const FDreamUIEventDelegate_Vector4& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Color_Execute(const FDreamUIEventDelegate_Color& InEvent, FColor InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Color_Register(const FDreamUIEventDelegate_Color& InEvent, FDreamUIEventDelegate_Color_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Color_Unregister(const FDreamUIEventDelegate_Color& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_LinearColor_Execute(const FDreamUIEventDelegate_LinearColor& InEvent, FLinearColor InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_LinearColor_Register(const FDreamUIEventDelegate_LinearColor& InEvent, FDreamUIEventDelegate_LinearColor_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_LinearColor_Unregister(const FDreamUIEventDelegate_LinearColor& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Quaternion_Execute(const FDreamUIEventDelegate_Quaternion& InEvent, FQuat InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Quaternion_Register(const FDreamUIEventDelegate_Quaternion& InEvent, FDreamUIEventDelegate_Quaternion_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Quaternion_Unregister(const FDreamUIEventDelegate_Quaternion& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_String_Execute(const FDreamUIEventDelegate_String& InEvent, FString InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_String_Register(const FDreamUIEventDelegate_String& InEvent, FDreamUIEventDelegate_String_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_String_Unregister(const FDreamUIEventDelegate_String& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Object_Execute(const FDreamUIEventDelegate_Asset& InEvent, UObject* InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Asset_Register(const FDreamUIEventDelegate_Asset& InEvent, FDreamUIEventDelegate_Asset_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Asset_Unregister(const FDreamUIEventDelegate_Asset& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_DreamWidget_Execute(const FDreamUIEventDelegate_DreamWidget& InEvent, UDreamWidget* InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_DreamWidget_Register(const FDreamUIEventDelegate_DreamWidget& InEvent, FDreamUIEventDelegate_DreamWidget_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_DreamWidget_Unregister(const FDreamUIEventDelegate_DreamWidget& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_PointerEvent_Execute(const FDreamUIEventDelegate_PointerEvent& InEvent, UDreamPointerEventData* InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_PointerEvent_Register(const FDreamUIEventDelegate_PointerEvent& InEvent, FDreamUIEventDelegate_PointerEvent_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_PointerEvent_Unregister(const FDreamUIEventDelegate_PointerEvent& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Class_Execute(const FDreamUIEventDelegate_Class& InEvent, UClass* InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Class_Register(const FDreamUIEventDelegate_Class& InEvent, FDreamUIEventDelegate_Class_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Class_Unregister(const FDreamUIEventDelegate_Class& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Rotator_Execute(const FDreamUIEventDelegate_Rotator& InEvent, FRotator InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Rotator_Register(const FDreamUIEventDelegate_Rotator& InEvent, FDreamUIEventDelegate_Rotator_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Rotator_Unregister(const FDreamUIEventDelegate_Rotator& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Text_Execute(const FDreamUIEventDelegate_Text& InEvent, FText InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Text_Register(const FDreamUIEventDelegate_Text& InEvent, FDreamUIEventDelegate_Text_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Text_Unregister(const FDreamUIEventDelegate_Text& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);

	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Execute"))
		static void DreamUIEventDelegate_Name_Execute(const FDreamUIEventDelegate_Name& InEvent, FName InParameter) { InEvent(InParameter); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Register"))
		static FDreamUIDelegateHandleWrapper DreamUIEventDelegate_Name_Register(const FDreamUIEventDelegate_Name& InEvent, FDreamUIEventDelegate_Name_DynamicDelegate InDelegate);
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (DisplayName = "Unregister"))
		static void DreamUIEventDelegate_Name_Unregister(const FDreamUIEventDelegate_Name& InEvent, const FDreamUIDelegateHandleWrapper& InDelegateHandle);
#pragma endregion EventDelegate

	/** InComponentType must be the same as InDreamUIComponentReference's component type */
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (DisplayName = "Get Component", CompactNodeTitle = "Component", BlueprintAutocast, DeterminesOutputType = "InComponentType"))
		static UActorComponent* DreamUICompRef_GetComponent(const FDreamUIComponentReference& InDreamUIComponentReference, TSubclassOf<UActorComponent> InComponentType);
	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (DisplayName = "Get Actor", CompactNodeTitle = "Actor", BlueprintAutocast))
		static AActor* DreamUICompRef_GetActor(const FDreamUIComponentReference& InDreamUIComponentReference);

	UFUNCTION(BlueprintPure, Category = DreamGUI, meta = (DisplayName = "Get", CompactNodeTitle = ".", BlueprintInternalUseOnly = "true"))
		static void K2_DreamUICompRef_GetComponent(const FDreamUIComponentReference& InDreamUICompRef, UActorComponent*& OutResult);

#pragma region DreamTween

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void DreamUIExecuteControllerInputAxis(FKey inputKey, float value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		static void DreamGUIExecuteControllerInputAction(FKey inputKey, bool pressOrRelease);

#pragma endregion
	UFUNCTION(BlueprintPure, Category = DreamGUI)
		static void GetSpriteSize(const FDreamUISpriteInfo& SpriteInfo, int32& width, int32& height);
	UFUNCTION(BlueprintPure, Category = DreamGUI)
		static void GetSpriteBorderSize(const FDreamUISpriteInfo& SpriteInfo, int32& borderLeft, int32& borderRight, int32& borderTop, int32& borderBottom);
	UFUNCTION(BlueprintPure, Category = DreamGUI)
		static void GetSpriteUV(const FDreamUISpriteInfo& SpriteInfo, float& UV0X, float& UV0Y, float& UV3X, float& UV3Y);
	UFUNCTION(BlueprintPure, Category = DreamGUI)
		static void GetSpriteBorderUV(const FDreamUISpriteInfo& SpriteInfo, float& borderUV0X, float& borderUV0Y, float& borderUV3X, float& borderUV3Y);
};
