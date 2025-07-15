// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Actor/LexWidgetActor.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexVisualPostProcess.h"
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
					if (GetLexWidget()->IsVisibleForRender())
					{
						bShouldNotify = true;
					}
					GetLexWidget()->SetWidgetVisibility(ESlateVisibility::Collapsed);
				}
				else
				{
					if (!GetLexWidget()->IsVisibleForRender())
					{
						bShouldNotify = true;
					}
					GetLexWidget()->SetWidgetVisibility(ESlateVisibility::Visible);
				}
				if (bShouldNotify)
				{
					FLexUIUtils::NotifyPropertyChanged(GetLexWidget(), ULexWidget::GetPropertyName_WidgetVisibility());
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

