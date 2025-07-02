// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "LexWidgetActor.generated.h"

class ULexWidget;
UCLASS(ClassGroup = LGUI, HideCategories=(Rendering, Replication, Collision, HLOD, Physics, Networking, Input, Actor, Navigation, LevelInstance, Cooking))
class LGUI_API ALexWidgetActor : public AActor
{
	GENERATED_BODY()
	
public:	
	ALexWidgetActor();

#if WITH_EDITOR
	static AActor* FirstTemporarilyHiddenActor;
	virtual void SetIsTemporarilyHiddenInEditor(bool bIsHidden) override;
#endif
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		virtual ULexWidget* GetLexWidget()const { return LexWidget;}
private:
	UPROPERTY(Category = "LGUI", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ULexWidget> LexWidget;
};
