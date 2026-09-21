// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamUIPlayTweenComponent.generated.h"


UCLASS(ClassGroup = (DreamGUI), meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamUIPlayTweenComponent : public UDreamUIBehaviour
{
	GENERATED_BODY()
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bPlayOnStart = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Instanced)
		TObjectPtr<class UDreamUIPlayTween> PlayTween;

	virtual void Awake() override;
	virtual void OnDestroy() override;
public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		class UDreamUIPlayTween* GetPlayTween()const { return PlayTween; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void Play();
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void Stop();
};
