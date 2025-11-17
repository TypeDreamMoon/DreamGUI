// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexWidget.h"
#if WITH_EDITOR
#include "PrefabSystem/LGUIPrefabManager.h"
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
	if (!ULGUIPrefabWorldSubsystem::IsLGUIPrefabSystemProcessingActor(this))
	{
		WidgetConstruct();
	}
}

void ALexWidgetActor::Destroyed()
{
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
}

#if WITH_EDITOR
AActor* ALexWidgetActor::FirstTemporarilyHiddenActor = nullptr;
void ALexWidgetActor::SetIsTemporarilyHiddenInEditor(bool bIsHidden)
{
	if (ULGUIPrefabWorldSubsystem::IsLGUIPrefabSystemProcessingActor(this))//when deserialize from prefab, no need to set it because everything is done when serialize it
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
					GetLexWidget()->SetWidgetActive(false);
				}
				else
				{
					if (!GetLexWidget()->GetWidgetActiveInHierarchy())
					{
						bShouldNotify = true;
					}
					GetLexWidget()->SetWidgetActive(true);
				}
				if (bShouldNotify)
				{
					FLexUIUtils::NotifyPropertyChanged(GetLexWidget(), ULexWidget::GetPropertyName_WidgetActive());
				}
			}
			ULGUIPrefabManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)] {
				FirstTemporarilyHiddenActor = nullptr;
				if (WeakThis.IsValid())
				{
					WeakThis->GetLexWidget()->SetIsTemporarilyHiddenInEditor_Recursive_By_RenderVisibility();//restore Temporary hidden state by UI item's IsUIActive state.
				}
				});
		}
	}

	Super::SetIsTemporarilyHiddenInEditor(bIsHidden);
}
#endif

void ALexWidgetActor::Awake_Implementation()
{
	WidgetConstruct();
}

void ALexWidgetActor::WidgetConstruct()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveWidgetConstruct();
	}
}

