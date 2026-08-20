// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/InputModule/DreamBaseInputModule.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerInputModule.generated.h"

class UDreamBaseRaycaster;
class UDreamEventSystem;

UCLASS(Abstract)
class DREAMGUI_API UDreamPointerInputModule : public UDreamBaseInputModule
{
	GENERATED_BODY()

public:
	static void ProcessPointerEvent(UDreamEventSystem* eventSystem, UDreamPointerEventData* pointerEventData, bool pointerHitAnything, const FDreamUIHitResultContainer& hitResult, bool& OutIsHitSomething, FDreamUIHitResult& OutHitResult);
protected:
	
	bool LineTrace(UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& OutDreamHitResult);
	TArray<FDreamUIHitResultContainer> MultiHitResult;//temp array for hit result
	/** Push the hovered widget's Cursor to the player controller. See DreamPointerPolicy. */
	static void ApplyHoverCursor(class UDreamPointerEventData* EventData);
	static void ProcessPointerEnterExit(UDreamEventSystem* eventSystem, UDreamPointerEventData* pointerEventData, UDreamWidget* oldObj, UDreamWidget* newObj);
	/** find a common root actor of two actors. return nullptr if no common root */
	static UDreamWidget* FindCommonRoot(UDreamWidget* A, UDreamWidget* B);

	bool Navigate(EDreamUINavigationDirection InDirection, UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& hitResult);
	void ProcessInputForNavigation();
	void ProcessInputForNavigation(UDreamPointerEventData* InPointerEventData);
	void ClearEventByID(int pointerID);
	static bool CanHandleInterface(UDreamWidget* targetComp, UClass* targetInterfaceClass);
	static UDreamWidget* GetEventHandle(UDreamWidget* targetComp, UClass* targetInterfaceClass);
	static void DeselectIfSelectionChanged(UDreamEventSystem* eventSystem, UDreamWidget* currentPressed, UDreamBaseEventData* EventData);
public:
	virtual void ClearEvent()override;
};