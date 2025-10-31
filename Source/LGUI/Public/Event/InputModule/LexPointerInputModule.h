// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/InputModule/LexBaseInputModule.h"
#include "Event/LexPointerEventData.h"
#include "Engine/HitResult.h"
#include "LexPointerInputModule.generated.h"

class ULexBaseRaycaster;
class ULexEventSystem;

UCLASS(Abstract)
class LGUI_API ULexPointerInputModule : public ULexBaseInputModule
{
	GENERATED_BODY()

public:
	static void ProcessPointerEvent(ULexEventSystem* eventSystem, ULexPointerEventData* pointerEventData, bool pointerHitAnything, const FLexUIHitResult& hitResult, bool& OutIsHitSomething, FHitResult& OutHitResult);
protected:
	
	bool LineTrace(ULexPointerEventData* InPointerEventData, FLexUIHitResult& OutLexHitResult);
	TArray<FLexUIHitResult> MultiHitResult;//temp array for hit result
	static void ProcessPointerEnterExit(ULexEventSystem* eventSystem, ULexPointerEventData* pointerEventData, USceneComponent* oldObj, USceneComponent* newObj, ELexUIEventFireType enterFireType);
	/** find a common root actor of two actors. return nullptr if no common root */
	static AActor* FindCommonRoot(AActor* actorA, AActor* actorB);

	bool Navigate(ELexUINavigationDirection direction, ULexPointerEventData* InPointerEventData, FLexUIHitResult& hitResult);
	void ProcessInputForNavigation();
	void ProcessInputForNavigation(ULexPointerEventData* InPointerEventData);
	void ClearEventByID(int pointerID);
	static bool CanHandleInterface(USceneComponent* targetComp, UClass* targetInterfaceClass, ELexUIEventFireType eventFireType);
	static USceneComponent* GetEventHandle(USceneComponent* targetComp, UClass* targetInterfaceClass, ELexUIEventFireType eventFireType);
	static void DeselectIfSelectionChanged(ULexEventSystem* eventSystem, USceneComponent* currentPressed, ULexBaseEventData* eventData);
public:
	virtual void ClearEvent()override;

	/** input for gamepad or keyboard navigation */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void InputNavigation(ELexUINavigationDirection direction, bool pressOrRelease, int pointerID);
	/** input for gamepad or keyboard press and release */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void InputTriggerForNavigation(bool triggerPress, int pointerID);
};