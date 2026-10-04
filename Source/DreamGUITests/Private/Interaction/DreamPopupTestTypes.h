// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/Object.h"
#include "DreamPopupTestTypes.generated.h"

/**
 * Game code that runs when a widget loses the focus: what a test binds to UDreamWidget::OnFocusLost to close a popup in
 * the middle of the focus moving into it, the way a deselect handler in a real screen can.
 *
 * OnFocusLost is a dynamic multicast delegate, which binds only to a UFUNCTION on a UObject -- hence a class, and not a
 * lambda. The action runs once and is then forgotten, so the focus coming back to the widget later does nothing more.
 */
UCLASS()
class UDreamPopupFocusLostAction : public UObject
{
	GENERATED_BODY()

public:
	/** What losing the focus does, once. */
	TFunction<void()> Action;

	/** How often the widget lost the focus while this listened. */
	int32 FocusLostCount = 0;

	UFUNCTION()
	void HandleFocusLost(int32 InUserIndex, int32 InPointerId)
	{
		++FocusLostCount;
		if (Action)
		{
			// Taken out before it runs: the action moves the focus, and a second loss on the way must not run it again.
			const TFunction<void()> Once = MoveTemp(Action);
			Action = nullptr;
			Once();
		}
	}
};
