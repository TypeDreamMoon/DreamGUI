// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIBPLibrary.h"

#include "LexUIDelegateHandleWrapper.h"
#include "Framework/Application/SlateApplication.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexScreenUISubsystem.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexVisual.h"
#include "Event/LexScreenSpaceRaycaster.h"
#include "PrefabSystem/LexUIPrefab.h"

#include LEXUIPREFAB_SERIALIZER_NEWEST_INCLUDE

namespace LexUICreateLocal
{
	/**
	 * The half of creation that both verbs share. Registration is not optional and is not deferred:
	 * it is the widget's only GC anchor, ULexWidget::OnAttachedToParent gates anchor recomputation
	 * on it, and the screen subsystem refuses unregistered widgets. Not being on screen comes from
	 * the active flag instead, which is what both the render path and the behaviour lifecycle read.
	 */
	ULexWidget* RegisterAndPark(UWorld* World, ULexWidget* Widget)
	{
		if (!IsValid(Widget))
		{
			return nullptr;
		}
		Widget->OnRegister();
		if (World && World->HasBegunPlay() && !Widget->HasBegunPlay())
		{
			// Awake runs, OnEnable does not -- ULexUIBehaviour::BeginPlay gates the latter on the
			// active flag. That is the same division UMG draws between NativeOnInitialized at
			// CreateWidget time and Construct on add.
			Widget->BeginPlay();
		}
		if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(World))
		{
			LexUIManager->ParkWidget(Widget);
		}
		else
		{
			// No manager means no anchor and no teardown; the widget would work until it silently
			// vanished. Better to say so than to hand back something that dies unpredictably.
			UE_LOG(LGUI, Error, TEXT("[%s].%d No LexUI manager for this world, so the widget cannot be created. World: %s")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, World ? *World->GetPathName() : TEXT("null"));
			Widget->DestroyWidget();
			return nullptr;
		}
		return Widget;
	}
}

ULexWidget* ULexUIBPLibrary::ConstructWidget(UObject* WorldContextObject, const FString& DisplayName,
	TSubclassOf<ULexVisual> VisualClass)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	// Outer is the world, not the caller: GetTypedOuter<UWorld>() has to resolve or the widget never
	// finds a manager. Outering to a GameInstance -- the habit UMG teaches -- would fail that test.
	ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
	Widget->SetDisplayName(DisplayName.IsEmpty() ? TEXT("Widget") : DisplayName);
	if (UClass* ResolvedVisualClass = VisualClass.Get();
		ResolvedVisualClass != nullptr && !ResolvedVisualClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Widget->CreateNewVisual(ResolvedVisualClass);
	}
	return LexUICreateLocal::RegisterAndPark(World, Widget);
}

ULexWidget* ULexUIBPLibrary::CreateWidgetFromPrefab(UObject* WorldContextObject, ULexUIPrefab* InPrefab)
{
	if (!IsValid(InPrefab))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab is not valid."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	// A null parent is what makes this "create" rather than "load into"; LoadPrefab only requires a
	// world. The loader registers and begins play on the tree itself, so parking is all that is left
	// -- and it has to happen before the caller ever sees the pointer, because a prefab root with a
	// canvas is a render root from the instant it exists.
	ULexWidget* Root = InPrefab->LoadPrefab(World, nullptr, [](ULexWidget*) {}, false);
	if (!IsValid(Root))
	{
		return nullptr;
	}
	return LexUICreateLocal::RegisterAndPark(World, Root);
}

ULexWidget* ULexUIBPLibrary::GetOrCreateScreenSpaceUIRoot(UObject* WorldContextObject)
{
	if (ULexScreenUISubsystem* ScreenUI = ULexScreenUISubsystem::GetLexScreenUISubsystem(WorldContextObject))
	{
		return ScreenUI->GetOrCreateScreenRoot();
	}
	return nullptr;
}

ULexWidget* ULexUIBPLibrary::LoadPrefabToScreen(UObject* WorldContextObject, ULexUIPrefab* InPrefab, const FLexUIPrefab_LoadPrefabCallback& InCallbackBeforeAwake, int32 SortOrder)
{
	if (!IsValid(InPrefab))
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d InPrefab is not valid."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}

	ULexScreenUISubsystem* ScreenUI = ULexScreenUISubsystem::GetLexScreenUISubsystem(WorldContextObject);
	ULexWidget* ScreenRoot = ScreenUI ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!ScreenRoot)
	{
		return nullptr;
	}

	ULexWidget* Page = InPrefab->LoadPrefab(WorldContextObject, ScreenRoot, InCallbackBeforeAwake, true);
	if (Page)
	{
		ScreenUI->AddToViewport(Page, SortOrder);
	}
	return Page;
}

ULexWidget* ULexUIBPLibrary::AddPrefabToViewport(UObject* WorldContextObject, ULexUIPrefab* InPrefab, int32 SortOrder)
{
	return LoadPrefabToScreen(WorldContextObject, InPrefab, FLexUIPrefab_LoadPrefabCallback(), SortOrder);
}

void ULexUIBPLibrary::RemoveFromViewport(UObject* WorldContextObject, ULexWidget* InRoot)
{
	if (ULexScreenUISubsystem* ScreenUI = ULexScreenUISubsystem::GetLexScreenUISubsystem(WorldContextObject))
	{
		ScreenUI->RemoveFromViewport(InRoot);
	}
}

bool ULexUIBPLibrary::IsInViewport(UObject* WorldContextObject, ULexWidget* InRoot)
{
	if (const ULexScreenUISubsystem* ScreenUI = ULexScreenUISubsystem::GetLexScreenUISubsystem(WorldContextObject))
	{
		return ScreenUI->IsInViewport(InRoot);
	}
	return false;
}

ULexWidget* ULexUIBPLibrary::DuplicateWidget(UObject* WorldContextObject, ULexWidget* Target, ULexWidget* Parent)
{
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::DuplicateWidget(World, Parent->GetOuter(), Target, Parent);
	}
	return nullptr;
}
void ULexUIBPLibrary::PrepareDuplicateData(ULexWidget* Target, FLexUIDuplicateDataContainer& DataContainer)
{
	DataContainer.bIsValid = LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::PrepareDataForDuplicate(Target, DataContainer.DuplicateData);
}
ULexWidget* ULexUIBPLibrary::DuplicateWidgetWithPreparedData(UObject* WorldContextObject, FLexUIDuplicateDataContainer& Data, ULexWidget* Parent)
{
	if (Data.bIsValid)
	{
		if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
		{
			return LEXUIPREFAB_SERIALIZER_NEWEST_NAMESPACE::WidgetSerializer::DuplicateWidgetWithPreparedData(World, Parent->GetOuter(), Data.DuplicateData, Parent);
		}
	}
	return nullptr;
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

UActorComponent* ULexUIBPLibrary::LexUICompRef_GetComponent(const FLexUIComponentReference& InLexUIComponentReference, TSubclassOf<UActorComponent> InComponentType)
{
	auto comp = InLexUIComponentReference.GetComponent();
	if (comp == nullptr)
	{
		UE_LOG(LGUI, Error, TEXT("[ULGUIBPLibrary::GetComponent]Target actor:%s dont have this kind of component:%s"), *(InLexUIComponentReference.GetActor()->GetPathName()), *(InComponentType->GetPathName()));
		return nullptr;
	}
	if (comp->GetClass() != InComponentType)
	{
		UE_LOG(LGUI, Error, TEXT("[ULGUIBPLibrary::GetComponent]InComponentType must be the same as InLGUIComponentReference's component type!"));
		return nullptr;
	}
	return comp;
}

AActor* ULexUIBPLibrary::LexUICompRef_GetActor(const FLexUIComponentReference& InLexUIComponentReference)
{
	return InLexUIComponentReference.GetActor();
}

void ULexUIBPLibrary::K2_LexUICompRef_GetComponent(const FLexUIComponentReference& InLexUICompRef, UActorComponent*& OutResult)
{
	OutResult = InLexUICompRef.GetComponent();
}


#pragma region LTween

void ULexUIBPLibrary::LexUIExecuteControllerInputAxis(FKey inputKey, float value)
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
IMPLEMENT_EVENTDELEGATE_BP(Asset, UObject*);
IMPLEMENT_EVENTDELEGATE_BP(LexWidget, ULexWidget*);
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
