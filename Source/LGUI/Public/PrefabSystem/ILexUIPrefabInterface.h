// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ILexUIPrefabInterface.generated.h"


/**
 * Interface for Actor or ActorComponent that loaded from LGUIPrefab
 */
UINTERFACE(Blueprintable, MinimalAPI)
class ULexUIPrefabInterface : public UInterface
{
	GENERATED_BODY()
};
/**
 * Interface for Actor or ActorComponent that loaded from LexUIPrefab
 */ 
class LGUI_API ILexUIPrefabInterface
{
	GENERATED_BODY()
public:
	/**
	 * Called when LexUIPrefab finish load. This is called later than BeginPlay.
	 *		Awake execute order in prefab: higher in hierarchy will execute earlier, so scripts on root actor will execute the first. Actor execute first, then execute on component.
	 *		And this Awake is execute later than LexUIBehaviour's Awake when in same prefab.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = LGUI)
		void Awake();
	/**
	 * Same as Awake but only execute in edit mode.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = LGUI, CallInEditor)
		void EditorAwake();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = LGUI)
	void OnPreSavePrefab();
};