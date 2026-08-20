// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamUIComponentReference.h"
#include "DreamGUI.h"

FDreamUIComponentReference::FDreamUIComponentReference(TSubclassOf<UActorComponent> InCompClass)
{
	HelperClass = InCompClass;
}
FDreamUIComponentReference::FDreamUIComponentReference(UActorComponent* InComp, TSubclassOf<UActorComponent> InCompClass)
{
	TargetComp = InComp;
	HelperClass = InCompClass;
	HelperActor = TargetComp->GetOwner();
	HelperComponentName = TargetComp->GetFName();
}
FDreamUIComponentReference::FDreamUIComponentReference(UActorComponent* InComp)
{
	TargetComp = InComp;
	HelperClass = InComp->GetClass();
	HelperActor = TargetComp->GetOwner();
	HelperComponentName = TargetComp->GetFName();
}
FDreamUIComponentReference::FDreamUIComponentReference()
{
	
}

bool FDreamUIComponentReference::CheckTargetObject()const
{
	if (IsValid(TargetComp))
	{
		return true;
	}
	else
	{
		if (IsValid(HelperActor))
		{
			if (IsValid(HelperClass))
			{
				TArray<UActorComponent*> Components;
				HelperActor->GetComponents(HelperClass, Components);
				if (Components.Num() == 1)
				{
					TargetComp = Components[0];
				}
				else if (Components.Num() > 1)
				{
					if (!HelperComponentName.IsNone())
					{
						for (auto& Comp : Components)
						{
							if (Comp->GetFName() == HelperComponentName)
							{
								TargetComp = Comp;
								return true;
							}
						}
						FString ActorName =
#if WITH_EDITOR
							HelperActor->GetActorLabel();
#else
							HelperActor->GetPathName();
#endif
						UE_LOG(DreamGUI, Error, TEXT("[%s].%d Can't find component of name '%s' on actor '%s'"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *HelperComponentName.ToString(), *ActorName);
					}
				}
			}
		}

		return IsValid(TargetComp);
	}
}
AActor* FDreamUIComponentReference::GetActor()const
{
	return HelperActor;
}

bool FDreamUIComponentReference::IsValidComponentReference()const
{
	return CheckTargetObject();
}

