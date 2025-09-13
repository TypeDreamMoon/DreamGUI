// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/InputModule/LexPointerInputModule.h"
#include "LexTouchInputModule.generated.h"

/** 
 * for mobile multi touch input
 */
UCLASS(ClassGroup = LGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class LGUI_API ULexTouchInputModule : public ULexPointerInputModule
{
	GENERATED_BODY()

public:
	virtual void ProcessInput()override;
	/** input for touch press and release */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void InputTouchTrigger(bool inTouchPress, int inTouchID, const FVector& inTouchPointPosition);
	/** input for touch point moved */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void InputTouchMoved(int inTouchID, const FVector& inTouchPointPosition);
	/** input for scroll */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void InputScroll(const FVector2D& inAxisValue);
};