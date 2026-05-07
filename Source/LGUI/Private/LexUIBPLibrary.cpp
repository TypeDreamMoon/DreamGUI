// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIBPLibrary.h"

#include "LexUIDelegateHandleWrapper.h"
#include "Utils/LexUIUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "LGUI.h"
#include "Core/Actor/LexWidgetContainer.h"
#include "Core/Components/LexCanvas.h"
#include "Event/LexScreenSpaceRaycaster.h"
#include "PrefabSystem/LexUIPrefab.h"
#include LGUIPREFAB_SERIALIZER_NEWEST_INCLUDE

void ULexUIBPLibrary::DestroyActorWithHierarchy(AActor* Target, bool WithHierarchy)
{
	FLexUIUtils::DestroyActorWithHierarchy(Target, WithHierarchy);
}
AActor* ULexUIBPLibrary::LoadPrefab(UObject* WorldContextObject, ULexUIPrefab* InPrefab, USceneComponent* InParent, const FLexUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake, bool SetRelativeTransformToIdentity)
{
	if (!IsValid(InPrefab))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab not valid"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	return InPrefab->LoadPrefab(WorldContextObject, InParent, InCallbackBeforeAwake, SetRelativeTransformToIdentity);
}
AActor* ULexUIBPLibrary::LoadPrefabWithTransform(UObject* WorldContextObject, ULexUIPrefab* InPrefab, USceneComponent* InParent, FVector Location, FRotator Rotation, FVector Scale, const FLexUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake)
{
	if (!IsValid(InPrefab))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab not valid"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	return InPrefab->LoadPrefabWithTransform(WorldContextObject, InParent, Location, Rotation, Scale, InCallbackBeforeAwake);
}
AActor* ULexUIBPLibrary::LoadPrefabWithTransform(UObject* WorldContextObject, ULexUIPrefab* InPrefab, USceneComponent* InParent, FVector Location, FQuat Rotation, FVector Scale, const TFunction<void(AActor*)>& InCallbackBeforeAwake)
{
	if (!IsValid(InPrefab))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab not valid"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	return InPrefab->LoadPrefabWithTransform(WorldContextObject, InParent, Location, Rotation, Scale, InCallbackBeforeAwake);
}
AActor* ULexUIBPLibrary::LoadPrefabWithReplacement(UObject* WorldContextObject, ULexUIPrefab* InPrefab, USceneComponent* InParent, const TMap<UObject*, UObject*>& InReplaceAssetMap, const TMap<UClass*, UClass*>& InReplaceClassMap, const FLexUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake)
{
	if (!IsValid(InPrefab))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab not valid"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	return InPrefab->LoadPrefabWithReplacement(WorldContextObject, InParent, InReplaceAssetMap, InReplaceClassMap, InCallbackBeforeAwake);
}

AActor* ULexUIBPLibrary::DuplicateActor(AActor* Target, USceneComponent* Parent)
{
	return LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::DuplicateActor(Target, Parent);
}
void ULexUIBPLibrary::PrepareDuplicateData(AActor* Target, FLexUIDuplicateDataContainer& DataContainer)
{
	DataContainer.bIsValid = LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::PrepareDataForDuplicate(Target, DataContainer.DuplicateData);
}
AActor* ULexUIBPLibrary::DuplicateActorWithPreparedData(FLexUIDuplicateDataContainer& Data, USceneComponent* Parent)
{
	if (Data.bIsValid)
	{
		return LGUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::ActorSerializer::DuplicateActorWithPreparedData(Data.DuplicateData, Parent);
	}
	else
	{
		return nullptr;
	}
}

UActorComponent* ULexUIBPLibrary::GetComponentInParent(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, bool IncludeSelf, AActor* InStopNode)
{
	if (!IsValid(InActor))
	{
		UE_LOG(LGUI, Error, TEXT("[ULGUIBPLibrary::GetComponentInParent]InActor is not valid!"));
		return nullptr;
	}
	AActor* parentActor = IncludeSelf ? InActor : InActor->GetAttachParentActor();
	while (parentActor != nullptr)
	{
		if (InStopNode != nullptr)
		{
			if (parentActor == InStopNode)return nullptr;
		}
		auto resultComp = parentActor->FindComponentByClass(ComponentClass);
		if (resultComp != nullptr)
		{
			return resultComp;
		}
		else
		{
			parentActor = parentActor->GetAttachParentActor();
		}
	}
	return nullptr;
}
TArray<UActorComponent*> ULexUIBPLibrary::GetComponentsInChildren(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, bool IncludeSelf, const TSet<AActor*>& InExcludeNode)
{
	TArray<UActorComponent*> result;
	if (!IsValid(InActor))
	{
		UE_LOG(LGUI, Error, TEXT("[ULGUIBPLibrary::GetComponentInParent]InActor is not valid!"));
		return result;
	}

	struct LOCAL
	{
		static void CollectComponentsInChildrenRecursive(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, TArray<UActorComponent*>& InOutArray, const TSet<AActor*>& InExcludeNode)
		{
			if (InExcludeNode.Contains(InActor))return;
			auto& components = InActor->GetComponents();
			for (UActorComponent* comp : components)
			{
				if (IsValid(comp) && comp->IsA(ComponentClass))
				{
					InOutArray.Add(comp);
				}
			}

			TArray<AActor*> childrenActors;
			InActor->GetAttachedActors(childrenActors);
			if (childrenActors.Num() > 0)
			{
				for (AActor* actor : childrenActors)
				{
					CollectComponentsInChildrenRecursive(actor, ComponentClass, InOutArray, InExcludeNode);
				}
			}
		}
	};
	if (IncludeSelf)
	{
		LOCAL::CollectComponentsInChildrenRecursive(InActor, ComponentClass, result, InExcludeNode);
	}
	else
	{
		TArray<AActor*> childrenActors;
		InActor->GetAttachedActors(childrenActors);
		if (childrenActors.Num() > 0)
		{
			for (AActor* actor : childrenActors)
			{
				LOCAL::CollectComponentsInChildrenRecursive(actor, ComponentClass, result, InExcludeNode);
			}
		}
	}
	return result;
}

UActorComponent* ULexUIBPLibrary::GetComponentInChildren(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, bool IncludeSelf, const TSet<AActor*>& InExcludeNode)
{
	if (!IsValid(InActor))
	{
		UE_LOG(LGUI, Error, TEXT("[ULGUIBPLibrary::GetComponentInChildren]InActor is not valid!"));
		return nullptr;
	}

	struct LOCAL
	{
		static UActorComponent* FindComponentInChildrenRecursive(AActor* InActor, TSubclassOf<UActorComponent> ComponentClass, const TSet<AActor*>& InExcludeNode)
		{
			if (InExcludeNode.Contains(InActor))return nullptr;
			if (auto comp = InActor->GetComponentByClass(ComponentClass))
			{
				if (IsValid(comp))
				{
					return comp;
				}
			}
			TArray<AActor*> childrenActors;
			InActor->GetAttachedActors(childrenActors);
			for (auto childActor : childrenActors)
			{
				auto comp = FindComponentInChildrenRecursive(childActor, ComponentClass, InExcludeNode);
				if (IsValid(comp))
				{
					return comp;
				}
			}
			return nullptr;
		}
	};

	UActorComponent* result = nullptr;
	if (IncludeSelf)
	{
		result = LOCAL::FindComponentInChildrenRecursive(InActor, ComponentClass, InExcludeNode);
	}
	else
	{
		TArray<AActor*> childrenActors;
		InActor->GetAttachedActors(childrenActors);
		if (childrenActors.Num() > 0)
		{
			for (AActor* actor : childrenActors)
			{
				result = LOCAL::FindComponentInChildrenRecursive(actor, ComponentClass, InExcludeNode);
				if (IsValid(result))
				{
					return result;
				}
			}
		}
	}
	return result;
}

UActorComponent* ULexUIBPLibrary::LGUICompRef_GetComponent(const FLexUIComponentReference& InLGUIComponentReference, TSubclassOf<UActorComponent> InComponentType)
{
	auto comp = InLGUIComponentReference.GetComponent();
	if (comp == nullptr)
	{
		UE_LOG(LGUI, Error, TEXT("[ULGUIBPLibrary::GetComponent]Target actor:%s dont have this kind of component:%s"), *(InLGUIComponentReference.GetActor()->GetPathName()), *(InComponentType->GetPathName()));
		return nullptr;
	}
	if (comp->GetClass() != InComponentType)
	{
		UE_LOG(LGUI, Error, TEXT("[ULGUIBPLibrary::GetComponent]InComponentType must be the same as InLGUIComponentReference's component type!"));
		return nullptr;
	}
	return comp;
}

AActor* ULexUIBPLibrary::LGUICompRef_GetActor(const FLexUIComponentReference& InLGUIComponentReference)
{
	return InLGUIComponentReference.GetActor();
}

void ULexUIBPLibrary::K2_LGUICompRef_GetComponent(const FLexUIComponentReference& InLGUICompRef, UActorComponent*& OutResult)
{
	OutResult = InLGUICompRef.GetComponent();
}


#pragma region LTween

void ULexUIBPLibrary::LGUIExecuteControllerInputAxis(FKey inputKey, float value)
{
	if (inputKey.IsValid())
	{
		FPlatformUserId UserId = IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser();
		FInputDeviceId DeviceId = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		const FGamepadKeyNames::Type keyName = inputKey.GetFName();
		FSlateApplication::Get().OnControllerAnalog(keyName, UserId, DeviceId, value);
	}
}
void ULexUIBPLibrary::LGUIExecuteControllerInputAction(FKey inputKey, bool pressOrRelease)
{
	if (inputKey.IsValid())
	{
		FPlatformUserId UserId = IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser();
		FInputDeviceId DeviceId = IPlatformInputDeviceMapper::Get().GetDefaultInputDevice();
		const FGamepadKeyNames::Type keyName = inputKey.GetFName();
		if (pressOrRelease)
		{
			FSlateApplication::Get().OnControllerButtonPressed(keyName, UserId, DeviceId, false);
		}
		else
		{
			FSlateApplication::Get().OnControllerButtonReleased(keyName, UserId, DeviceId, false);
		}
	}
}
#pragma endregion

#pragma region EventDelegate
#define IMPLEMENT_EVENTDELEGATE_BP(EventDelegateParamType, ParamType)\
FLexUIDelegateHandleWrapper ULexUIBPLibrary::LexUIEventDelegate_##EventDelegateParamType##_Register(const FLexUIEventDelegate_##EventDelegateParamType& InEvent, FLexUIEventDelegate_##EventDelegateParamType##_DynamicDelegate InDelegate)\
{\
	auto delegateHandle = InEvent.Register([InDelegate](ParamType value) {\
		if (InDelegate.IsBound())\
		{\
			InDelegate.Execute(value);\
		}\
		});\
	return FLexUIDelegateHandleWrapper(delegateHandle);\
}\
void ULexUIBPLibrary::LexUIEventDelegate_##EventDelegateParamType##_Unregister(const FLexUIEventDelegate_##EventDelegateParamType& InEvent, const FLexUIDelegateHandleWrapper& InDelegateHandle)\
{\
	InEvent.Unregister(InDelegateHandle.DelegateHandle);\
}

FLexUIDelegateHandleWrapper ULexUIBPLibrary::LexUIEventDelegate_Empty_Register(const FLexUIEventDelegate_Empty& InEvent, FLexUIEventDelegate_Empty_DynamicDelegate InDelegate)
{
	auto delegateHandle = InEvent.Register([InDelegate]() {
		if (InDelegate.IsBound())
		{
			InDelegate.Execute();
		}
		});
	return FLexUIDelegateHandleWrapper(delegateHandle);
}
void ULexUIBPLibrary::LexUIEventDelegate_Empty_Unregister(const FLexUIEventDelegate_Empty& InEvent, const FLexUIDelegateHandleWrapper& InDelegateHandle)
{
	InEvent.Unregister(InDelegateHandle.DelegateHandle);
}

IMPLEMENT_EVENTDELEGATE_BP(Bool, bool);
IMPLEMENT_EVENTDELEGATE_BP(Float, float);
IMPLEMENT_EVENTDELEGATE_BP(Double, double);
//IMPLEMENT_EVENTDELEGATE_BP(Int8, int8);
IMPLEMENT_EVENTDELEGATE_BP(UInt8, uint8);
//IMPLEMENT_EVENTDELEGATE_BP(Int16, int16);
//IMPLEMENT_EVENTDELEGATE_BP(UInt16, uint16);
IMPLEMENT_EVENTDELEGATE_BP(Int32, int32);
//IMPLEMENT_EVENTDELEGATE_BP(UInt32, uint32);
IMPLEMENT_EVENTDELEGATE_BP(Int64, int64);
//IMPLEMENT_EVENTDELEGATE_BP(UInt64, uint64);
IMPLEMENT_EVENTDELEGATE_BP(Vector2, FVector2D);
IMPLEMENT_EVENTDELEGATE_BP(Vector3, FVector);
IMPLEMENT_EVENTDELEGATE_BP(Vector4, FVector4);
IMPLEMENT_EVENTDELEGATE_BP(Color, FColor);
IMPLEMENT_EVENTDELEGATE_BP(LinearColor, FLinearColor);
IMPLEMENT_EVENTDELEGATE_BP(Quaternion, FQuat);
IMPLEMENT_EVENTDELEGATE_BP(String, FString);
IMPLEMENT_EVENTDELEGATE_BP(Object, UObject*);
IMPLEMENT_EVENTDELEGATE_BP(Actor, AActor*);
IMPLEMENT_EVENTDELEGATE_BP(PointerEvent, ULexPointerEventData*);
IMPLEMENT_EVENTDELEGATE_BP(Class, UClass*);
IMPLEMENT_EVENTDELEGATE_BP(Rotator, FRotator);
IMPLEMENT_EVENTDELEGATE_BP(Text, FText);
IMPLEMENT_EVENTDELEGATE_BP(Name, FName);

#pragma endregion

void ULexUIBPLibrary::GetSpriteSize(const FLexUISpriteInfo& SpriteInfo, int32& width, int32& height)
{
	width = SpriteInfo.Width;
	height = SpriteInfo.Height;
}
void ULexUIBPLibrary::GetSpriteBorderSize(const FLexUISpriteInfo& SpriteInfo, int32& borderLeft, int32& borderRight, int32& borderTop, int32& borderBottom)
{
	borderLeft = SpriteInfo.Border.Left;
	borderRight = SpriteInfo.Border.Right;
	borderTop = SpriteInfo.Border.Top;
	borderBottom = SpriteInfo.Border.Bottom;
}
void ULexUIBPLibrary::GetSpriteUV(const FLexUISpriteInfo& SpriteInfo, float& UV0X, float& UV0Y, float& UV3X, float& UV3Y)
{
	UV0X = SpriteInfo.MinUV.X;
	UV0Y = SpriteInfo.MaxUV.Y;
	UV3X = SpriteInfo.MaxUV.X;
	UV3Y = SpriteInfo.MinUV.Y;
}
void ULexUIBPLibrary::GetSpriteBorderUV(const FLexUISpriteInfo& SpriteInfo, float& borderUV0X, float& borderUV0Y, float& borderUV3X, float& borderUV3Y)
{
	borderUV0X = SpriteInfo.MinUV.X;
	borderUV0Y = SpriteInfo.MaxUV.Y;
	borderUV3X = SpriteInfo.MaxUV.X;
	borderUV3Y = SpriteInfo.MinUV.Y;
}
