// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/DreamPointerEventData.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDownUpInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerEnterExitInterface.h"
#include "DreamInputPipelineTestTypes.generated.h"

/**
 * Keeps the books on every pointer event its widget is told -- how many of each, and whose -- and runs a test's
 * action from inside the handler of one of them, once.
 *
 * The books are what the pipeline's promises are checked against: every Enter answered by one Exit, every Down
 * by one Up, whatever the action did to the pointers in the middle of the event.
 */
UCLASS()
class UDreamPointerLedger : public UDreamUIBehaviour
	, public IDreamPointerEnterExitInterface
	, public IDreamPointerDownUpInterface
	, public IDreamPointerClickInterface
	, public IDreamPointerDragInterface
{
	GENERATED_BODY()

public:
	int32 Enter = 0;
	int32 Exit = 0;
	int32 Down = 0;
	int32 Up = 0;
	int32 Click = 0;
	int32 BeginDrag = 0;
	int32 Drag = 0;
	int32 EndDrag = 0;
	int32 LastPointerID = INDEX_NONE;
	int32 LastUserIndex = INDEX_NONE;

	/** The event the action runs from inside, when there is one. */
	bool bHasActOn = false;
	EDreamUIPointerEventType ActOn = EDreamUIPointerEventType::Enter;
	TFunction<void(UDreamPointerEventData*)> Action;
	bool bActed = false;
	/** What every handler answers: whether the event goes on up the hierarchy. */
	bool bAllowBubble = false;

	void ActFrom(EDreamUIPointerEventType InEvent, TFunction<void(UDreamPointerEventData*)> InAction)
	{
		bHasActOn = true;
		ActOn = InEvent;
		Action = MoveTemp(InAction);
		bActed = false;
	}

	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData) override { return Note(Enter, EDreamUIPointerEventType::Enter, EventData); }
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData) override { return Note(Exit, EDreamUIPointerEventType::Exit, EventData); }
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData) override { return Note(Down, EDreamUIPointerEventType::Down, EventData); }
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData) override { return Note(Up, EDreamUIPointerEventType::Up, EventData); }
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData) override { return Note(Click, EDreamUIPointerEventType::Click, EventData); }
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData) override { return Note(BeginDrag, EDreamUIPointerEventType::BeginDrag, EventData); }
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData) override { return Note(Drag, EDreamUIPointerEventType::Drag, EventData); }
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData) override { return Note(EndDrag, EDreamUIPointerEventType::EndDrag, EventData); }

private:
	bool Note(int32& InCount, EDreamUIPointerEventType InEvent, UDreamPointerEventData* InEventData)
	{
		++InCount;
		if (InEventData != nullptr)
		{
			LastPointerID = InEventData->PointerID;
			LastUserIndex = InEventData->UserIndex;
		}
		if (bHasActOn && !bActed && InEvent == ActOn && Action)
		{
			bActed = true;
			Action(InEventData);
		}
		return bAllowBubble;
	}
};
