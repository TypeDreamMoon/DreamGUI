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
		RootUIComp->AddLGUILifeCycleUIBehaviourComponent(this);
	}
}
void ULGUILifeCycleUIBehaviour::OnUnregister()
{
	Super::OnUnregister();
	if (RootUIComp.IsValid())
	{
		RootUIComp->RemoveLGUILifeCycleUIBehaviourComponent(this);
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

void ULGUILifeCycleUIBehaviour::OnUIActiveInHierachy(bool activeOrInactive) 
{ 
	auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
	if (PrefabManager && PrefabManager->IsPrefabSystemProcessingActor(this->GetOwner()))
	{
		return;
	}

	if (activeOrInactive)
	{
		if (!bIsAwakeCalled)
		{
#if WITH_EDITOR
			if (!this->GetWorld()->IsGameWorld())//edit mode
			{

			}
			else
#endif
			{
				Call_Awake();
			}
		}
	}
	SetActiveStateForEnableAndDisable(activeOrInactive);
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIActiveInHierarchy(activeOrInactive);
	}
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

void ULGUILifeCycleUIBehaviour::OnUIDimensionsChanged(bool PivotChanged, bool widthChanged, bool heightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIDimensionsChanged(PivotChanged, widthChanged, heightChanged);
	}
}
void ULGUILifeCycleUIBehaviour::OnUIChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
	}
}
void ULGUILifeCycleUIBehaviour::OnUIChildAcitveInHierarchy(ULexWidget* Child, bool ativeOrInactive)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIChildAcitveInHierarchy(Child, ativeOrInactive);
	}
}
void ULGUILifeCycleUIBehaviour::OnUIAttachmentChanged()
{ 
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIAttachmentChanged();
	}
}
void ULGUILifeCycleUIBehaviour::OnUIChildAttachmentChanged(ULexWidget* Child, bool attachOrDetach) 
{ 
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIChildAttachmentChanged(Child, attachOrDetach);
	}
}
void ULGUILifeCycleUIBehaviour::OnUIInteractionStateChanged(bool interactableOrNot)
{ 
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIInteractionStateChanged(interactableOrNot);
	}
}
void ULGUILifeCycleUIBehaviour::OnUIChildHierarchyIndexChanged(ULexWidget* Child)
{ 
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnUIChildHierarchyIndexChanged(Child);
	}
}


void ULGUILifeCycleUIBehaviour::Call_OnUIDimensionsChanged(bool PivotChanged, bool widthChanged, bool heightChanged)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnUIDimensionsChanged(PivotChanged, widthChanged, heightChanged);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnUIDimensionsChanged(PivotChanged, widthChanged, heightChanged);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::Call_OnUIDimensionsChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnUIDimensionsChanged(PivotChanged, widthChanged, heightChanged);
				}};
		}
	}
}
void ULGUILifeCycleUIBehaviour::Call_OnUIChildDimensionsChanged(ULexWidget* Child, bool Pivot, bool WidthChanged, bool HeightChanged)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnUIChildDimensionsChanged(Child, Pivot, WidthChanged, HeightChanged);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnUIChildDimensionsChanged(Child, Pivot, WidthChanged, HeightChanged);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			auto ChildPtr = MakeWeakObjectPtr(Child);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::Call_OnUIChildDimensionsChanged] = [=]() {
				if (ThisPtr.IsValid() && ChildPtr.IsValid())
				{
					ThisPtr->OnUIChildDimensionsChanged(ChildPtr.Get(), Pivot, WidthChanged, HeightChanged);
				}};
		}
	}
}
void ULGUILifeCycleUIBehaviour::Call_OnUIChildAcitveInHierarchy(ULexWidget* child, bool ativeOrInactive)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnUIChildAcitveInHierarchy(child, ativeOrInactive);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnUIChildAcitveInHierarchy(child, ativeOrInactive);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			auto ChildPtr = MakeWeakObjectPtr(child);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::Call_OnUIChildAcitveInHierarchy] = [=]() {
				if (ThisPtr.IsValid() && ChildPtr.IsValid())
				{
					ThisPtr->OnUIChildAcitveInHierarchy(ChildPtr.Get(), ativeOrInactive);
				}};
		}
	}
}
void ULGUILifeCycleUIBehaviour::Call_OnUIAttachmentChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnUIAttachmentChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnUIAttachmentChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::Call_OnUIAttachmentChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnUIAttachmentChanged();
				}};
		}
	}
}
void ULGUILifeCycleUIBehaviour::Call_OnUIChildAttachmentChanged(ULexWidget* child, bool attachOrDetach)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnUIChildAttachmentChanged(child, attachOrDetach);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnUIChildAttachmentChanged(child, attachOrDetach);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			auto ChildPtr = MakeWeakObjectPtr(child);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::Call_OnUIChildAttachmentChanged] = [=]() {
				if (ThisPtr.IsValid() && ChildPtr.IsValid())
				{
					ThisPtr->OnUIChildAttachmentChanged(ChildPtr.Get(), attachOrDetach);
				}};
		}
	}
}
void ULGUILifeCycleUIBehaviour::Call_OnUIInteractionStateChanged(bool interactableOrNot)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnUIInteractionStateChanged(interactableOrNot);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnUIInteractionStateChanged(interactableOrNot);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::Call_OnUIInteractionStateChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnUIInteractionStateChanged(interactableOrNot);
				}};
		}
	}
}
void ULGUILifeCycleUIBehaviour::Call_OnUIChildHierarchyIndexChanged(ULexWidget* child)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnUIChildHierarchyIndexChanged(child);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnUIChildHierarchyIndexChanged(child);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			auto ChildPtr = MakeWeakObjectPtr(child);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::Call_OnUIChildHierarchyIndexChanged] = [=]() {
				if (ThisPtr.IsValid() && ChildPtr.IsValid())
				{
					ThisPtr->OnUIChildHierarchyIndexChanged(ChildPtr.Get());
				}};
		}
	}
}
