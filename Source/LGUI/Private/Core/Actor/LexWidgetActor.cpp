// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Actor/LexWidgetActor.h"

#include "Core/LexUIManager.h"
#include "Core/Components/LexWidget.h"
#if WITH_EDITOR
#include "PrefabSystem/LexUIPrefabManager.h"
#include "Utils/LexUIUtils.h"
#endif

ALexWidgetActor::ALexWidgetActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	LexWidget = CreateDefaultSubobject<ULexWidget>(TEXT("LexWidget"));
	RootComponent = LexWidget;
}

void ALexWidgetActor::BeginPlay()
{
	Super::BeginPlay();
}

void ALexWidgetActor::BeginDestroy()
{
	Super::BeginDestroy();
}

#if WITH_EDITOR
#include "PrefabSystem/LexUIPrefabHelperObject.h"
bool ALexWidgetActor::bIsSetCanNotifyAttachmentWhenDestroy = false;
#endif
void ALexWidgetActor::Destroyed()
{
#if WITH_EDITOR
	ULexUIPrefabHelperObject* PrefabHelperObject = nullptr;
	if (!bIsSetCanNotifyAttachmentWhenDestroy)
	{
		bIsSetCanNotifyAttachmentWhenDestroy = true;
		ULexUIManagerObject::AddOneShotTickFunction([=]()
		{
			bIsSetCanNotifyAttachmentWhenDestroy = false;
		}, 1);
		PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisActor(this);
		if (PrefabHelperObject != nullptr)
		{
			PrefabHelperObject->SetCanNotifyAttachment(false);
			PrefabHelperObject->Modify();
			PrefabHelperObject->SetAnythingDirty();
		}
	}
#endif
	
	Super::Destroyed();
	TArray<AActor*> AttachedActors;
	GetAttachedActors(AttachedActors);
	for (auto Child : AttachedActors)
	{
		if (IsValid(Child))
		{
			Child->Destroy();
		}
	}

#if WITH_EDITOR
	if (PrefabHelperObject != nullptr)
	{
		PrefabHelperObject->SetCanNotifyAttachment(true);
	}
#endif
}

#if WITH_EDITOR
AActor* ALexWidgetActor::FirstTemporarilyHiddenActor = nullptr;
void ALexWidgetActor::SetIsTemporarilyHiddenInEditor(bool bIsHidden)
{
	if (ULexUIPrefabWorldSubsystem::IsLexUIPrefabSystemProcessingActor(this))//when deserialize from prefab, no need to set it because everything is done when serialize it
	{

	}
	else
	{
		if (FirstTemporarilyHiddenActor == nullptr)
		{
			//when click on editor outliner's eye button, only need to set first UI Actor (the editing one)
			FirstTemporarilyHiddenActor = this;
			if (IsTemporarilyHiddenInEditor() != bIsHidden)
			{
				bool bShouldNotify = false;
				if (bIsHidden)
				{
					if (GetLexWidget()->GetWidgetActiveInHierarchy())
					{
						bShouldNotify = true;
					}
				}
				else
				{
					if (!GetLexWidget()->GetWidgetActiveInHierarchy())
					{
						bShouldNotify = true;
					}
				}
				if (bShouldNotify)
				{
					FLexUIUtils::ChangePropertyWithNotify(GetLexWidget(), ULexWidget::GetPropertyName_WidgetActive(), [=, this]()
					{
						GetLexWidget()->SetWidgetActive(!bIsHidden);
					});
				}
				else
				{
					GetLexWidget()->SetWidgetActive(!bIsHidden);
				}
			}
			ULexUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)] {
				FirstTemporarilyHiddenActor = nullptr;
				if (WeakThis.IsValid())
				{
					WeakThis->GetLexWidget()->SetIsTemporarilyHiddenInEditor_Recursive_By_WidgetActive();//restore Temporary hidden state by LexWidget's WidgetActive state.
				}
				});
		}
	}

	Super::SetIsTemporarilyHiddenInEditor(bIsHidden);
}
#endif

