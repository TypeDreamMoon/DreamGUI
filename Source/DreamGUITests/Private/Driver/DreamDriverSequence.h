// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverLocators.h"

class FAutomationTestBase;
class UDreamCanvas;
class UDreamEventSystem;
class UDreamPointerEventData;
class UDreamDriverInputModule;
class UDreamScreenSpaceRaycaster;
class UDreamUIManagerWorldSubsystem;
class UDreamWidget;
class UWorld;
enum class EDreamUIMouseButtonType : uint8;
enum class EDreamUINavigationDirection : uint8;

/**
 * Everything a driver step needs in order to happen: the world it happens in, the pieces that carry
 * input through it, and the tree it is aimed at.
 *
 * The pointers are raw and not kept alive from here. What keeps them alive is what would keep them
 * alive in a game: actors belong to the world, widgets are held by the UI manager's registered-widget
 * list, and the rig that built all of it outlives every context it hands out. A second owner here
 * would only disagree with those.
 */
struct FDreamDriverContext
{
	UWorld* World = nullptr;
	UDreamEventSystem* EventSystem = nullptr;
	UDreamDriverInputModule* InputModule = nullptr;
	UDreamUIManagerWorldSubsystem* Manager = nullptr;
	UDreamScreenSpaceRaycaster* Raycaster = nullptr;
	UDreamWidget* Root = nullptr;
	UDreamCanvas* RootCanvas = nullptr;

	/** The test currently running, so a step that fails can say so where a report will show it. Optional. */
	FAutomationTestBase* CurrentTest = nullptr;

	/**
	 * How long a pumped frame is. Fixed rather than measured, because a headless pump has no frame
	 * rate to measure -- and because timeouts are expressed in seconds while the only thing that
	 * advances here is frames, so the conversion between the two has to be a constant.
	 */
	float FrameSeconds = 1.0f / 60.0f;

	/** Whether this context has the pieces a step needs. A half-built rig should fail loudly, not act. */
	bool IsUsable() const;

	/**
	 * One frame of the headless pump.
	 *
	 * See the implementation for what it calls and why; in short: advance the world clock, tick the
	 * event system (which is what reaches the input module), then tick the UI manager (which is
	 * layout, transforms and clip rectangles). Not called under the engine pump -- there the engine's
	 * own frame is the pump, and calling this as well would run everything twice.
	 */
	void PumpOneFrame(float InDeltaSeconds);

	/** PumpOneFrame InFrameCount times at FrameSeconds each. */
	void PumpFrames(int32 InFrameCount);

	/** The pointer's own state, which is where hover, press and drag are readable from. */
	UDreamPointerEventData* GetPointerEventData(int32 InPointerID = 0) const;

	/** The one widget this locator finds under the root, or null when it finds none or several. */
	UDreamWidget* FindOne(const FDreamLocatorRef& InLocator) const;
};

/** What a step says about itself after being given a frame. */
enum class EDreamDriverStepResult : uint8
{
	/** Finished. The executor moves on to the next step. */
	Done,
	/** Not finished. The executor gives it another frame. */
	Again,
	/** Cannot finish. The executor reports and stops; the sequence has failed. */
	Failed,
};

/**
 * One thing a sequence does, executed at most once per frame.
 *
 * Five kinds exist and no more: input, wait N frames, wait for a condition, run a lambda (which is
 * where assertions live), and fail outright. The two pumps differ only in what gives a step its
 * frame, which is why there is one step list rather than two.
 */
class IDreamDriverStep
{
public:
	virtual ~IDreamDriverStep() = default;

	/** InDeltaSeconds is the length of the frame this step is being given, for steps that measure time. */
	virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) = 0;

	/** What this step was trying to do, for the error a failure produces. */
	virtual FString Describe() const = 0;

	/** Why the last Execute returned Failed. Empty unless it did. */
	virtual FString GetFailureReason() const { return FString(); }
};

using FDreamDriverStepRef = TSharedRef<IDreamDriverStep>;

/**
 * A list of steps, built by chaining, run by one of two pumps.
 *
 * Perform runs it headlessly and synchronously: the step list is walked here and now, with this
 * object's own PumpOneFrame between steps that asked for another frame. That keeps a driver test the
 * same shape as the 1115 tests already in this module -- set up, act, assert, return.
 *
 * PerformLatent hands the same list to the automation framework as a latent command, one step per
 * ENGINE frame. That is the only way to drive anything the engine itself has to advance: the designer
 * viewport, a Blueprint recompile settling, a render graph. Nothing in the list knows which pump it
 * is under.
 */
class FDreamDriverSequence
{
public:
	explicit FDreamDriverSequence(FDreamDriverContext& InContext);

	/** Pointer moves. MoveTo aims at a located widget's centre; MoveBy and MoveToPixel take raw pixels. */
	FDreamDriverSequence& MoveTo(const FDreamLocatorRef& InLocator);
	FDreamDriverSequence& MoveToPixel(const FVector2D& InPixel);
	FDreamDriverSequence& MoveBy(const FVector2D& InPixelDelta);

	/** Trigger down and up where the pointer already is. */
	FDreamDriverSequence& Press(EDreamUIMouseButtonType InButton);
	FDreamDriverSequence& Release(EDreamUIMouseButtonType InButton);
	FDreamDriverSequence& Press();
	FDreamDriverSequence& Release();

	/** Move to the located widget's centre, then press and release there. */
	FDreamDriverSequence& Click(const FDreamLocatorRef& InLocator);
	FDreamDriverSequence& Click(const FDreamLocatorRef& InLocator, EDreamUIMouseButtonType InButton);

	/**
	 * Press on the first widget's centre, cross the raycaster's drag threshold on the first move, and
	 * release on the second's. Spread over frames because a drag that happened inside one frame is a
	 * teleport, and the press-to-drag decision is made between frames.
	 */
	FDreamDriverSequence& DragTo(const FDreamLocatorRef& InFrom, const FDreamLocatorRef& InTo);
	/** The same, ending at an offset from where the press began rather than at another widget. */
	FDreamDriverSequence& DragBy(const FDreamLocatorRef& InFrom, const FVector2D& InPixelDelta);

	/** A wheel turn, delivered to whatever the pointer is over on the next frame. */
	FDreamDriverSequence& ScrollBy(const FVector2D& InAxisValue);

	/** Gamepad or keyboard navigation: a direction pressed and released, or the accept button. */
	FDreamDriverSequence& Navigate(EDreamUINavigationDirection InDirection);
	FDreamDriverSequence& NavigationTrigger(bool bInTriggerPress);

	/** Let InFrameCount frames pass. */
	FDreamDriverSequence& WaitFrames(int32 InFrameCount);

	/**
	 * Give frames to a wait delegate until it passes, fails, or the timeout elapses. See FDreamUntil.
	 *
	 * InDescription is what the step calls itself when it gives up. A wait delegate carries no
	 * description of its own -- it is a bare function -- so without one a timeout can only report the
	 * number of seconds it waited, which tells a reader nothing about what never happened.
	 */
	FDreamDriverSequence& Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription);
	FDreamDriverSequence& Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout);
	FDreamDriverSequence& Wait(const FDriverWaitDelegate& InWaitDelegate);

	/** Run something in the middle of the sequence. This is where assertions go. */
	FDreamDriverSequence& Then(TFunction<void(FDreamDriverContext&)> InAction);

	/** Fail here, unconditionally. For a branch a test has decided must not be reached. */
	FDreamDriverSequence& Fail(const FString& InReason);

	/**
	 * Run the whole list now, pumping frames as steps ask for them. Returns false on the first
	 * failure, having reported it through the context's test if it has one.
	 */
	bool Perform();

	/**
	 * Hand the list to the automation framework, one step per engine frame. Returns immediately; the
	 * test body that called it should return true and let the framework finish the work.
	 */
	void PerformLatent();

	/** How many steps are queued. Mostly so a test can assert it built the sequence it meant to. */
	int32 Num() const { return Steps.Num(); }

	/**
	 * Push a step of a kind this class does not know about.
	 *
	 * Public because the five kinds above are the five the RUNTIME needs, and an adapter for something
	 * else -- a designer viewport, a Blueprint recompile -- is a step of its own rather than a reason
	 * to widen this class's vocabulary. Implement IDreamDriverStep and push it here.
	 */
	FDreamDriverSequence& Add(const FDreamDriverStepRef& InStep);

private:
	FDreamDriverContext* Context = nullptr;
	TArray<FDreamDriverStepRef> Steps;
};
