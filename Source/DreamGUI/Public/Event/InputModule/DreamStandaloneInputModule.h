// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/DreamPointerEventData.h"
#include "DreamStandaloneInputModule.generated.h"

/**
 * Common standalone platform input, or mouse input
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamStandaloneInputModule : public UDreamPointerInputModule
{
	GENERATED_BODY()

public:
	virtual void ProcessInput()override;
	/** input for mouse press and release */
	UFUNCTION(BlueprintCallable, Category = DreamGUI, meta = (AdvancedDisplay = "inMouseButtonType"))
		void InputTrigger(const FVector& InMousePosition, bool InTriggerPress, EDreamUIMouseButtonType InMouseButtonType = EDreamUIMouseButtonType::Left);
	/**
	 * input for scroll
	 * @param	InAxisValue		Use a 2d vector for scroll value. For mouse scroll just fill X&Y with mouse scroll value; For touchpad input use X for horizontal and Y for vertical.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void InputScroll(const FVector2D& InAxisValue);
	/**
	 * see "bOverrideMousePosition" property
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void InputMouseMove(const FVector& InMousePosition);
	/** input for touch press and release */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputTouchTrigger(bool InTouchPress, int InTouchID, const FVector& InTouchPointPosition);
	/** input for touch point moved */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputTouchMoved(int InTouchID, const FVector& InTouchPointPosition);
	/** get current mouse position, return (0,0) if mouse position is not valid */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void GetMousePosition(FVector2D& OutMousePos)const;

	/** input for gamepad or keyboard navigation */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputNavigation(EDreamUINavigationDirection InDirection, bool InPressOrRelease, int InPointerID);
	/** input for gamepad or keyboard press and release */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void InputTriggerForNavigation(bool InTriggerPress, int InPointerID);
protected:
	void CommonInputTrigger(const FVector& InPointerPosition, bool InTriggerPress, int InPointerID, EDreamUIMouseButtonType InMouseButtonType = EDreamUIMouseButtonType::Left);
	struct StandaloneInputData
	{
		bool bTriggerPress;
		float PressTime;
		float ReleaseTime;
		int PointerID;
		EDreamUIMouseButtonType MouseButtonType;
		FVector PointerPosition;
	};
	TArray<StandaloneInputData> StandaloneInputDataArray;//collect input data into array in input event, and process these input data in ProcessInput. This can solve the condition: multiple mouse button input in one frame
};