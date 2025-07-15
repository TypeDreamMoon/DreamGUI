// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LGUILifeCycleUIBehaviour.h"
#include "LGUI.h"
#include "Core/LGUIManager.h"
#include "PrefabSystem/LGUIPrefabManager.h"

ULGUILifeCycleUIBehaviour::ULGUILifeCycleUIBehaviour()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	CallbacksBeforeAwake.SetNumZeroed((int)ECallbackFunctionType::COUNT);
}

void ULGUILifeCycleUIBehaviour::OnRegister()
{
	Super::OnRegister();
	if (CheckRootUIComponent())
	{
		RootUIComp->GetIsEnabledChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnIsEnabledChanged);
		RootUIComp->GetTransformChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnTransformChanged);
		RootUIComp->GetDimensionChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnDimensionsChanged);
		RootUIComp->GetAttachmentChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnAttachmentChanged);
		RootUIComp->GetSiblingIndexChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnSiblingIndexChanged);
		RootUIComp->GetRenderVisibilityChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnRenderVisibilityChanged);
		RootUIComp->GetLayoutVisibilityChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnLayoutVisibilityChanged);
		RootUIComp->GetHitTestVisibilityChangedEvent().AddUObject(this, &ULGUILifeCycleUIBehaviour::Call_OnHitTestVisibilityChanged);
	}
}
void ULGUILifeCycleUIBehaviour::OnUnregister()
{
	Super::OnUnregister();
	if (RootUIComp.IsValid())
	{
		RootUIComp->GetIsEnabledChangedEvent().RemoveAll(this);
		RootUIComp->GetTransformChangedEvent().RemoveAll(this);
		RootUIComp->GetDimensionChangedEvent().RemoveAll(this);
		RootUIComp->GetAttachmentChangedEvent().RemoveAll(this);
		RootUIComp->GetSiblingIndexChangedEvent().RemoveAll(this);
		RootUIComp->GetRenderVisibilityChangedEvent().RemoveAll(this);
		RootUIComp->GetLayoutVisibilityChangedEvent().RemoveAll(this);
		RootUIComp->GetHitTestVisibilityChangedEvent().RemoveAll(this);
	}
}
#if WITH_EDITOR
void ULGUILifeCycleUIBehaviour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	CheckRootUIComponent();
}
#endif

bool ULGUILifeCycleUIBehaviour::CheckRootUIComponent() const
{
	if (RootUIComp.IsValid())return true;
	if (this->GetWorld() == nullptr)return false;
	if (auto Owner = GetOwner())
	{
		RootUIComp = Cast<ULexWidget>(Owner->GetRootComponent());
		if(RootUIComp.IsValid())return true;
	}
	UE_LOG(LGUI, Warning, TEXT("[%s].%d LGUILifeCycleUIBehaviour must attach to a UI actor!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	return false;
}

ULexWidget* ULGUILifeCycleUIBehaviour::GetRootUIComponent() const
{
	if (CheckRootUIComponent())
	{
		return RootUIComp.Get();
	}
	return nullptr;
}

void ULGUILifeCycleUIBehaviour::OnIsEnabledChanged(bool IsEnabled) 
{ 
	auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
	if (PrefabManager && PrefabManager->IsPrefabSystemProcessingActor(this->GetOwner()))
	{
		return;
	}
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnIsEnabledChanged(IsEnabled);
	}
}

void ULGUILifeCycleUIBehaviour::OnTransformChanged()
{
}

void ULGUILifeCycleUIBehaviour::Call_Awake()
{
	for (auto& CallbackFunc : CallbacksBeforeAwake)
	{
		if (CallbackFunc != nullptr)
		{
			CallbackFunc();
		}
	}
	CallbacksBeforeAwake.Empty();
	Super::Call_Awake();
}

void ULGUILifeCycleUIBehaviour::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	}
}

void ULGUILifeCycleUIBehaviour::OnChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged,
	bool HeightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
	}
}

void ULGUILifeCycleUIBehaviour::OnAttachmentChanged()
{ 
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnAttachmentChanged();
	}
}

void ULGUILifeCycleUIBehaviour::OnSiblingIndexChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnSiblingIndexChanged();
	}
}

void ULGUILifeCycleUIBehaviour::OnRenderVisibilityChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnRenderVisibilityChanged();
	}
}

void ULGUILifeCycleUIBehaviour::OnLayoutVisibilityChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnLayoutVisibilityChanged();
	}
}

void ULGUILifeCycleUIBehaviour::OnHitTestVisibilityChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnHitTestVisibilityChanged();
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnIsEnabledChanged(bool IsEnabled)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnIsEnabledChanged(IsEnabled);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnIsEnabledChanged(IsEnabled);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnIsEnabledChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnIsEnabledChanged(IsEnabled);
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnTransformChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnTransformChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnTransformChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnTransformChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnTransformChanged();
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnDimensionsChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged,
	bool HeightChanged)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnChildDimensionsChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnAttachmentChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnAttachmentChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnAttachmentChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnAttachmentChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnAttachmentChanged();
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnSiblingIndexChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnSiblingIndexChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnSiblingIndexChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnSiblingIndexChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnSiblingIndexChanged();
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnRenderVisibilityChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnRenderVisibilityChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnRenderVisibilityChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnRenderVisibilityChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnRenderVisibilityChanged();
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnLayoutVisibilityChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnLayoutVisibilityChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnLayoutVisibilityChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnLayoutVisibilityChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnLayoutVisibilityChanged();
				}};
		}
	}
}

void ULGUILifeCycleUIBehaviour::Call_OnHitTestVisibilityChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnHitTestVisibilityChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnHitTestVisibilityChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnHitTestVisibilityChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnHitTestVisibilityChanged();
				}};
		}
	}
}
