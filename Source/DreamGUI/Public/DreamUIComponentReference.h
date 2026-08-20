// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "DreamUIComponentReference.generated.h"

/**
 * For direct reference a ActorComponent
 */ 
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIComponentReference
{
	GENERATED_BODY()
	FDreamUIComponentReference(TSubclassOf<UActorComponent> InCompClass);
	FDreamUIComponentReference(UActorComponent* InComp, TSubclassOf<UActorComponent> InCompClass);
	FDreamUIComponentReference(UActorComponent* InComp);
	FDreamUIComponentReference();
protected:
	friend class FDreamUIComponentReferenceCustomization;
	/** Editor helper actor */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<AActor> HelperActor = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (AllowAbstract = "true"))
		TSubclassOf<UActorComponent> HelperClass;
	UPROPERTY(VisibleAnywhere, Category = "DreamGUI")
		FName HelperComponentName;

	UPROPERTY(EditAnywhere, Transient, Category = "DreamGUI")
		mutable TObjectPtr<UActorComponent> TargetComp = nullptr;

	bool CheckTargetObject()const;
public:
	AActor* GetActor()const;
	UActorComponent* GetComponent()const
	{
		if (CheckTargetObject())
		{
			return TargetComp;
		}
		return nullptr;
	}
	template<class T>
	T* GetComponent()const
	{
		if (CheckTargetObject())
		{
#if WITH_EDITOR
			static_assert(TPointerIsConvertibleFromTo<T, const UActorComponent>::Value, "'T' template parameter to GetComponent must be derived from UActorComponent");
			if (!TargetComp)
			{
				UE_LOG(LogTemp, Error, TEXT("[%s].%d TargetComp is null!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
				return nullptr;
			}
			if (!TargetComp->IsA(T::StaticClass()))
			{
				UE_LOG(LogTemp, Error, TEXT("[%s].%d Provided parameter T: '%s' must be parent of or equal to HelperClass: '%s'!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(T::StaticClass()->GetName()), *(HelperClass->GetName()));
				return nullptr;
			}
#endif
			return (T*)(TargetComp);
		}
		else
		{
			return nullptr;
		}
	}
	TSubclassOf<UActorComponent> GetComponentClass()const
	{
		return HelperClass;
	}
	bool IsValidComponentReference()const;
};