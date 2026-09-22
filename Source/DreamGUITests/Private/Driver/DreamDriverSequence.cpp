// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverSequence.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverProjection.h"

DEFINE_LOG_CATEGORY_STATIC(LogDreamDriver, Log, All);

bool FDreamDriverContext::IsUsable() const
{
	return World != nullptr
		&& IsValid(EventSystem)
		&& IsValid(InputModule)
		&& IsValid(Manager)
		&& IsValid(Root);
}

void FDreamDriverContext::PumpOneFrame(float InDeltaSeconds)
{
	if (World != nullptr)
	{
		/*
		 * THE CLOCK FIRST, and it is not decoration.
		 *
		 * The pointer pipeline asks the WORLD what time it is, not the pump what the delta was:
		 * UDreamPointerInputModule fires a long press when GetTimeSeconds() minus the press time has
		 * reached the threshold, decides a click continues a run when the gap since the last click is
		 * short enough, and schedules navigation repeat against the same clock. A world made with
		 * UWorld::CreateWorld and never ticked has that clock frozen at zero, which makes every second
		 * click a double click and every press exactly as old as the one before it. Advancing it is
		 * what makes a pumped frame a frame rather than a repetition of the same instant.
		 *
		 * All four clocks, because which one a caller reads is not this pump's business: the paused
		 * and unpaused ones diverge only under a pause a headless fixture has not got, and leaving
		 * three of them at zero would be a difference waiting to be discovered by whatever reads them
		 * next.
		 */
		World->TimeSeconds += InDeltaSeconds;
		World->UnpausedTimeSeconds += InDeltaSeconds;
		World->RealTimeSeconds += InDeltaSeconds;
		World->AudioTimeSeconds += InDeltaSeconds;
		World->DeltaTimeSeconds = InDeltaSeconds;
		World->DeltaRealTimeSeconds = InDeltaSeconds;
	}

	if (IsValid(EventSystem))
	{
		/*
		 * The event system's own tick, not the input module's ProcessInput directly.
		 *
		 * TickComponent is what an engine frame calls, and it is where the event system's
		 * raycast-enabled switch is honoured -- going straight to ProcessInput would mean a test
		 * could not turn input off the way a game can, and would be asserting against a pipeline
		 * one step shorter than the real one. From here it is ProcessInput, the line trace through
		 * every registered raycaster, and dispatch.
		 */
		// Called through the BASE pointer because UDreamEventSystem narrows TickComponent to protected
		// while UActorComponent declares it public, and access is checked against the static type.
		// This is not a way around the intent -- the engine itself calls it through the base, and this
		// pump exists precisely to be that caller. Dispatch is still virtual, so the override above is
		// what runs; only the name lookup moves.
		static_cast<UActorComponent*>(EventSystem)->TickComponent(InDeltaSeconds, LEVELTICK_All, nullptr);
	}

	if (IsValid(Manager))
	{
		/*
		 * Layout, widget transforms and clip rectangles -- everything the NEXT frame's hit test reads.
		 *
		 * Called explicitly rather than left to the world, because nothing ticks a world built with
		 * UWorld::CreateWorld, and UDreamUIManagerWorldSubsystem would not run even if something did:
		 * it is a UTickableWorldSubsystem whose IsTickableInEditor is false and whose Tick is gated
		 * behind bShouldTickInEditor. TickDreamUI rather than Tick is also what every layout test in
		 * this module already calls, so the pump and they agree on what a frame of UI is.
		 */
		Manager->TickDreamUI(InDeltaSeconds);
	}
}

void FDreamDriverContext::PumpFrames(int32 InFrameCount)
{
	for (int32 FrameIndex = 0; FrameIndex < InFrameCount; ++FrameIndex)
	{
		PumpOneFrame(FrameSeconds);
	}
}

UDreamPointerEventData* FDreamDriverContext::GetPointerEventData(int32 InPointerID) const
{
	if (!IsValid(EventSystem))
	{
		return nullptr;
	}
	// Not created on demand: asking where the pointer is should not bring a pointer into existence,
	// and a test that reads one before any input has happened wants to see that there is none.
	return EventSystem->GetPointerEventData(InPointerID, false);
}

UDreamWidget* FDreamDriverContext::FindOne(const FDreamLocatorRef& InLocator) const
{
	if (!IsValid(Root))
	{
		return nullptr;
	}
	TArray<UDreamWidget*> Found;
	InLocator->Locate(Root, Found);
	// Exactly one. Several is as much a failure as none: a driver that silently took the first of
	// three would act on whichever the tree happened to sort first, and the test would pass or fail
	// on child order rather than on the thing it is about.
	return Found.Num() == 1 ? Found[0] : nullptr;
}

namespace DreamDriverSequenceLocal
{
	/** A pixel worked out when the step runs, rather than when the sequence was written. */
	using FDreamPixelResolver = TFunction<TOptional<FVector2D>(FDreamDriverContext&)>;

	void ReportStepFailure(const FDreamDriverContext& InContext, const FDreamDriverStepRef& InStep)
	{
		const FString Reason = InStep->GetFailureReason();
		const FString Message = FString::Printf(TEXT("Driver step %s failed: %s"),
			*InStep->Describe(),
			Reason.IsEmpty() ? TEXT("no reason given") : *Reason);

		// The context's test if it has one, otherwise whatever the framework says is running. The
		// second is what makes a failure inside a latent command land on the right test: by then the
		// test body has long returned, and the framework is the only thing that still knows.
		FAutomationTestBase* ReportingTest = InContext.CurrentTest != nullptr
			? InContext.CurrentTest
			: FAutomationTestFramework::Get().GetCurrentTest();
		if (ReportingTest != nullptr)
		{
			ReportingTest->AddError(Message);
		}
		else
		{
			UE_LOG(LogDreamDriver, Error, TEXT("%s"), *Message);
		}
	}

	/**
	 * A step that does something once and then spends one frame letting the pipeline see it.
	 *
	 * Every input entry point in UDreamStandaloneInputModule queues rather than dispatches, so the
	 * effect of a press is not observable until a frame has been pumped. Returning Again once is how
	 * a step says "the frame after mine belongs to me".
	 */
	class FDreamInputStep : public IDreamDriverStep
	{
	public:
		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (bHasApplied)
			{
				return EDreamDriverStepResult::Done;
			}
			if (!InContext.IsUsable())
			{
				FailureReason = TEXT("the driver context is missing a world, event system, input module, UI manager or root widget");
				return EDreamDriverStepResult::Failed;
			}
			if (!Apply(InContext))
			{
				return EDreamDriverStepResult::Failed;
			}
			bHasApplied = true;
			return EDreamDriverStepResult::Again;
		}

		virtual FString GetFailureReason() const override { return FailureReason; }

	protected:
		/** Returns false having set FailureReason. InDeltaSeconds is deliberately not offered: input is instantaneous. */
		virtual bool Apply(FDreamDriverContext& InContext) = 0;

		FString FailureReason;

	private:
		bool bHasApplied = false;
	};

	class FDreamMoveStep : public FDreamInputStep
	{
	public:
		FDreamMoveStep(FDreamPixelResolver InResolver, const FString& InDescription)
			: Resolver(MoveTemp(InResolver))
			, Description(InDescription)
		{
		}

		virtual FString Describe() const override { return FString::Printf(TEXT("MoveTo(%s)"), *Description); }

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			const TOptional<FVector2D> Pixel = Resolver(InContext);
			if (!Pixel.IsSet())
			{
				FailureReason = FString::Printf(TEXT("could not work out a viewport pixel for %s"), *Description);
				return false;
			}
			InContext.InputModule->MoveTo(Pixel.GetValue());
			return true;
		}

	private:
		FDreamPixelResolver Resolver;
		FString Description;
	};

	class FDreamTriggerStep : public FDreamInputStep
	{
	public:
		FDreamTriggerStep(EDreamUIMouseButtonType InButton, bool bInPress)
			: Button(InButton)
			, bPress(bInPress)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("%s(button %d)"), bPress ? TEXT("Press") : TEXT("Release"), (int32)Button);
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			if (bPress)
			{
				InContext.InputModule->Press(Button);
			}
			else
			{
				InContext.InputModule->Release(Button);
			}
			return true;
		}

	private:
		EDreamUIMouseButtonType Button;
		bool bPress;
	};

	class FDreamScrollStep : public FDreamInputStep
	{
	public:
		explicit FDreamScrollStep(const FVector2D& InAxisValue)
			: AxisValue(InAxisValue)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("ScrollBy(%s)"), *AxisValue.ToString());
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			InContext.InputModule->Scroll(AxisValue);
			return true;
		}

	private:
		FVector2D AxisValue;
	};

	class FDreamNavigateStep : public FDreamInputStep
	{
	public:
		FDreamNavigateStep(EDreamUINavigationDirection InDirection, bool bInPressOrRelease)
			: Direction(InDirection)
			, bPressOrRelease(bInPressOrRelease)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("Navigate(direction %d, %s)"), (int32)Direction,
				bPressOrRelease ? TEXT("press") : TEXT("release"));
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			InContext.InputModule->Navigate(Direction, bPressOrRelease, 0);
			return true;
		}

	private:
		EDreamUINavigationDirection Direction;
		bool bPressOrRelease;
	};

	class FDreamNavigationTriggerStep : public FDreamInputStep
	{
	public:
		explicit FDreamNavigationTriggerStep(bool bInTriggerPress)
			: bTriggerPress(bInTriggerPress)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("NavigationTrigger(%s)"), bTriggerPress ? TEXT("press") : TEXT("release"));
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			InContext.InputModule->NavigationTrigger(bTriggerPress, 0);
			return true;
		}

	private:
		bool bTriggerPress;
	};

	/**
	 * The first move of a drag: far enough from the press to be a drag rather than a twitch.
	 *
	 * The distance is read from the raycaster rather than assumed, because the threshold is authored
	 * in canvas units and compared in viewport pixels -- UDreamScreenSpaceRaycaster::ShouldStartDrag
	 * measures against GetScaledDragThresholdSquare, which is the authored value times the canvas
	 * scale. Hard-coding a number here would work at scale 1 and silently stop starting drags on the
	 * first fixture that scales its canvas.
	 */
	class FDreamDragCrossThresholdStep : public FDreamInputStep
	{
	public:
		FDreamDragCrossThresholdStep(FDreamPixelResolver InTargetResolver, const FString& InDescription)
			: TargetResolver(MoveTemp(InTargetResolver))
			, Description(InDescription)
		{
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("DragCrossThreshold(towards %s)"), *Description);
		}

	protected:
		virtual bool Apply(FDreamDriverContext& InContext) override
		{
			const TOptional<FVector2D> Target = TargetResolver(InContext);
			if (!Target.IsSet())
			{
				FailureReason = FString::Printf(TEXT("could not work out a viewport pixel for %s"), *Description);
				return false;
			}
			const UDreamPointerEventData* EventData = InContext.GetPointerEventData(0);
			if (EventData == nullptr)
			{
				FailureReason = TEXT("no pointer has been pressed, so there is nothing to drag from");
				return false;
			}
			const FVector2D PressPixel(EventData->PressPointerPosition.X, EventData->PressPointerPosition.Y);

			double ThresholdPixels = 0.0;
			if (IsValid(InContext.Raycaster))
			{
				ThresholdPixels = FMath::Sqrt((double)InContext.Raycaster->GetScaledDragThresholdSquare());
			}

			FVector2D Direction = Target.GetValue() - PressPixel;
			if (Direction.IsNearlyZero())
			{
				// Dragging onto the thing being dragged has no direction of its own, and a drag that
				// never moved is not one. Rightward is as good as any and is at least reproducible.
				Direction = FVector2D(1.0, 0.0);
			}
			// Past the threshold rather than onto it: the comparison is strictly greater than, so
			// landing exactly on it is still a press.
			const FVector2D Next = PressPixel + Direction.GetSafeNormal() * (ThresholdPixels + 2.0);
			InContext.InputModule->MoveTo(Next);
			return true;
		}

	private:
		FDreamPixelResolver TargetResolver;
		FString Description;
	};

	class FDreamWaitFramesStep : public IDreamDriverStep
	{
	public:
		explicit FDreamWaitFramesStep(int32 InFrameCount)
			: RemainingFrames(FMath::Max(InFrameCount, 0))
			, RequestedFrames(FMath::Max(InFrameCount, 0))
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (RemainingFrames <= 0)
			{
				return EDreamDriverStepResult::Done;
			}
			--RemainingFrames;
			return EDreamDriverStepResult::Again;
		}

		virtual FString Describe() const override { return FString::Printf(TEXT("WaitFrames(%d)"), RequestedFrames); }

	private:
		int32 RemainingFrames;
		int32 RequestedFrames;
	};

	class FDreamWaitDelegateStep : public IDreamDriverStep
	{
	public:
		FDreamWaitDelegateStep(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription)
			: WaitDelegate(InWaitDelegate)
			, Timeout(InTimeout.Timespan)
			, Description(InDescription)
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (!WaitDelegate.IsBound())
			{
				FailureReason = TEXT("the wait delegate is not bound to anything");
				return EDreamDriverStepResult::Failed;
			}
			const FDriverWaitResponse Response = WaitDelegate.Execute(Elapsed);
			if (Response.State == FDriverWaitResponse::EState::PASSED)
			{
				return EDreamDriverStepResult::Done;
			}
			if (Response.State == FDriverWaitResponse::EState::FAILED)
			{
				FailureReason = FString::Printf(TEXT("%s did not happen; the wait gave up after %.3f seconds"),
					Description.IsEmpty() ? TEXT("the awaited condition") : *Description, Elapsed.GetTotalSeconds());
				return EDreamDriverStepResult::Failed;
			}
			// Response.NextWait is deliberately ignored: it is the engine driver's way of asking not
			// to be polled so hard, and re-evaluating every frame is never less correct than polling
			// less often. A headless pump also has no clock to sleep against.
			Elapsed += FTimespan::FromSeconds(InDeltaSeconds);
			if (Elapsed >= Timeout)
			{
				FailureReason = FString::Printf(TEXT("%s did not happen within %.3f seconds"),
					Description.IsEmpty() ? TEXT("the awaited condition") : *Description, Timeout.GetTotalSeconds());
				return EDreamDriverStepResult::Failed;
			}
			return EDreamDriverStepResult::Again;
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("Wait(%s, timeout %.3f seconds)"),
				Description.IsEmpty() ? TEXT("a condition") : *Description, Timeout.GetTotalSeconds());
		}

		virtual FString GetFailureReason() const override { return FailureReason; }

	private:
		FDriverWaitDelegate WaitDelegate;
		FTimespan Timeout;
		FString Description;
		FTimespan Elapsed = FTimespan::Zero();
		FString FailureReason;
	};

	class FDreamThenStep : public IDreamDriverStep
	{
	public:
		explicit FDreamThenStep(TFunction<void(FDreamDriverContext&)> InAction)
			: Action(MoveTemp(InAction))
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			if (!Action)
			{
				FailureReason = TEXT("the action is empty");
				return EDreamDriverStepResult::Failed;
			}
			Action(InContext);
			// Costs no frame. An assertion is a reading of the state the previous step produced, and
			// making it wait a frame would mean reading one frame later than the step it is about.
			return EDreamDriverStepResult::Done;
		}

		virtual FString Describe() const override { return TEXT("Then(...)"); }
		virtual FString GetFailureReason() const override { return FailureReason; }

	private:
		TFunction<void(FDreamDriverContext&)> Action;
		FString FailureReason;
	};

	class FDreamFailStep : public IDreamDriverStep
	{
	public:
		explicit FDreamFailStep(const FString& InReason)
			: Reason(InReason)
		{
		}

		virtual EDreamDriverStepResult Execute(FDreamDriverContext& InContext, float InDeltaSeconds) override
		{
			return EDreamDriverStepResult::Failed;
		}

		virtual FString Describe() const override { return TEXT("Fail(...)"); }
		virtual FString GetFailureReason() const override { return Reason; }

	private:
		FString Reason;
	};

	FDreamPixelResolver MakeLocatorCentreResolver(const FDreamLocatorRef& InLocator)
	{
		return [InLocator](FDreamDriverContext& InContext) -> TOptional<FVector2D>
		{
			UDreamWidget* Widget = InContext.FindOne(InLocator);
			if (Widget == nullptr)
			{
				return TOptional<FVector2D>();
			}
			return FDreamDriverProjection::WidgetCentrePixel(Widget);
		};
	}

	/**
	 * Runs a latent sequence one step per engine frame.
	 *
	 * The context is held by pointer and the rig that owns it has to outlive the command. That is not
	 * a rule this class can enforce -- a latent command outlives the test body that queued it, which
	 * is the whole point -- so a rig driven latently belongs to the test fixture, not to RunTest's
	 * stack frame.
	 */
	class FDreamDriverSequenceLatentCommand : public IAutomationLatentCommand
	{
	public:
		FDreamDriverSequenceLatentCommand(FDreamDriverContext* InContext, TArray<FDreamDriverStepRef> InSteps)
			: Context(InContext)
			, Steps(MoveTemp(InSteps))
		{
		}

		virtual bool Update() override
		{
			if (Context == nullptr)
			{
				if (FAutomationTestBase* ReportingTest = FAutomationTestFramework::Get().GetCurrentTest())
				{
					ReportingTest->AddError(TEXT("A driver sequence was performed without a context."));
				}
				return true;
			}
			// The engine's own frame time, not the headless pump's constant: under this pump the
			// frames are real ones, and a wait's timeout should be measured in the seconds that
			// actually passed.
			const float DeltaSeconds = FApp::GetDeltaTime();
			while (Steps.IsValidIndex(StepIndex))
			{
				const EDreamDriverStepResult Result = Steps[StepIndex]->Execute(*Context, DeltaSeconds);
				if (Result == EDreamDriverStepResult::Failed)
				{
					ReportStepFailure(*Context, Steps[StepIndex]);
					return true;
				}
				if (Result == EDreamDriverStepResult::Again)
				{
					// This engine frame IS the pump; nothing is advanced from here.
					return false;
				}
				++StepIndex;
			}
			return true;
		}

	private:
		FDreamDriverContext* Context = nullptr;
		TArray<FDreamDriverStepRef> Steps;
		int32 StepIndex = 0;
	};
}

FDreamDriverSequence::FDreamDriverSequence(FDreamDriverContext& InContext)
	: Context(&InContext)
{
}

FDreamDriverSequence& FDreamDriverSequence::Add(const FDreamDriverStepRef& InStep)
{
	Steps.Add(InStep);
	return *this;
}

FDreamDriverSequence& FDreamDriverSequence::MoveTo(const FDreamLocatorRef& InLocator)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamMoveStep>(MakeLocatorCentreResolver(InLocator), InLocator->Describe()));
}

FDreamDriverSequence& FDreamDriverSequence::MoveToPixel(const FVector2D& InPixel)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D TargetPixel = InPixel;
	return Add(MakeShared<FDreamMoveStep>(
		[TargetPixel](FDreamDriverContext&) -> TOptional<FVector2D> { return TargetPixel; },
		TargetPixel.ToString()));
}

FDreamDriverSequence& FDreamDriverSequence::MoveBy(const FVector2D& InPixelDelta)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D Delta = InPixelDelta;
	return Add(MakeShared<FDreamMoveStep>(
		[Delta](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
		{
			return InStepContext.InputModule->GetVirtualCursor() + Delta;
		},
		FString::Printf(TEXT("offset %s"), *Delta.ToString())));
}

FDreamDriverSequence& FDreamDriverSequence::Press(EDreamUIMouseButtonType InButton)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamTriggerStep>(InButton, true));
}

FDreamDriverSequence& FDreamDriverSequence::Release(EDreamUIMouseButtonType InButton)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamTriggerStep>(InButton, false));
}

FDreamDriverSequence& FDreamDriverSequence::Press()
{
	return Press(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::Release()
{
	return Release(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::Click(const FDreamLocatorRef& InLocator)
{
	return Click(InLocator, EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::Click(const FDreamLocatorRef& InLocator, EDreamUIMouseButtonType InButton)
{
	// Move, frame, press, frame, release, frame. Three frames, because each of the three is queued
	// input and queued input is not observable until the frame after it was queued.
	MoveTo(InLocator);
	Press(InButton);
	return Release(InButton);
}

FDreamDriverSequence& FDreamDriverSequence::DragTo(const FDreamLocatorRef& InFrom, const FDreamLocatorRef& InTo)
{
	using namespace DreamDriverSequenceLocal;
	MoveTo(InFrom);
	Press(EDreamUIMouseButtonType::Left);
	Add(MakeShared<FDreamDragCrossThresholdStep>(MakeLocatorCentreResolver(InTo), InTo->Describe()));
	// A midpoint before the destination, so the drag is carried by at least three separate moves on
	// three separate frames. A drag delivered in one jump never produces an OnPointerDrag between its
	// ends, and a list that scrolls by accumulated delta would see one impulse instead of a motion.
	Add(MakeShared<FDreamMoveStep>(
		[InTo](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
		{
			UDreamWidget* Widget = InStepContext.FindOne(InTo);
			if (Widget == nullptr)
			{
				return TOptional<FVector2D>();
			}
			const TOptional<FVector2D> TargetPixel = FDreamDriverProjection::WidgetCentrePixel(Widget);
			if (!TargetPixel.IsSet())
			{
				return TOptional<FVector2D>();
			}
			return (InStepContext.InputModule->GetVirtualCursor() + TargetPixel.GetValue()) * 0.5;
		},
		FString::Printf(TEXT("halfway to %s"), *InTo->Describe())));
	MoveTo(InTo);
	// One frame resting on the target before letting go, so whatever the drop lands on is what the
	// pointer was already over rather than what it arrived at in the same frame.
	WaitFrames(1);
	return Release(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::DragBy(const FDreamLocatorRef& InFrom, const FVector2D& InPixelDelta)
{
	using namespace DreamDriverSequenceLocal;
	const FVector2D Delta = InPixelDelta;
	// Resolved from the PRESS position rather than from wherever the cursor has got to, so every one
	// of the moves below aims at the same destination.
	FDreamPixelResolver DestinationResolver = [Delta](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
	{
		const UDreamPointerEventData* EventData = InStepContext.GetPointerEventData(0);
		if (EventData == nullptr)
		{
			return TOptional<FVector2D>();
		}
		return FVector2D(EventData->PressPointerPosition.X, EventData->PressPointerPosition.Y) + Delta;
	};

	MoveTo(InFrom);
	Press(EDreamUIMouseButtonType::Left);
	Add(MakeShared<FDreamDragCrossThresholdStep>(DestinationResolver, Delta.ToString()));
	Add(MakeShared<FDreamMoveStep>(
		[DestinationResolver](FDreamDriverContext& InStepContext) -> TOptional<FVector2D>
		{
			const TOptional<FVector2D> Destination = DestinationResolver(InStepContext);
			if (!Destination.IsSet())
			{
				return TOptional<FVector2D>();
			}
			return (InStepContext.InputModule->GetVirtualCursor() + Destination.GetValue()) * 0.5;
		},
		FString::Printf(TEXT("halfway to offset %s"), *Delta.ToString())));
	Add(MakeShared<FDreamMoveStep>(DestinationResolver, FString::Printf(TEXT("offset %s"), *Delta.ToString())));
	WaitFrames(1);
	return Release(EDreamUIMouseButtonType::Left);
}

FDreamDriverSequence& FDreamDriverSequence::ScrollBy(const FVector2D& InAxisValue)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamScrollStep>(InAxisValue));
}

FDreamDriverSequence& FDreamDriverSequence::Navigate(EDreamUINavigationDirection InDirection)
{
	using namespace DreamDriverSequenceLocal;
	// Pressed, given a frame to move focus, then released. A direction that was never released stays
	// held, and the repeat timer would move focus again on the frame after.
	Add(MakeShared<FDreamNavigateStep>(InDirection, true));
	return Add(MakeShared<FDreamNavigateStep>(InDirection, false));
}

FDreamDriverSequence& FDreamDriverSequence::NavigationTrigger(bool bInTriggerPress)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamNavigationTriggerStep>(bInTriggerPress));
}

FDreamDriverSequence& FDreamDriverSequence::WaitFrames(int32 InFrameCount)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamWaitFramesStep>(InFrameCount));
}

FDreamDriverSequence& FDreamDriverSequence::Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamWaitDelegateStep>(InWaitDelegate, InTimeout, InDescription));
}

FDreamDriverSequence& FDreamDriverSequence::Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout)
{
	return Wait(InWaitDelegate, InTimeout, FString());
}

FDreamDriverSequence& FDreamDriverSequence::Wait(const FDriverWaitDelegate& InWaitDelegate)
{
	// Five seconds is not a meaningful limit, it is a backstop: the delegates FDreamUntil builds
	// carry their own timeout and fail on their own, and this only catches a hand-rolled one that
	// never does.
	return Wait(InWaitDelegate, FWaitTimeout::InSeconds(5.0), FString());
}

FDreamDriverSequence& FDreamDriverSequence::Then(TFunction<void(FDreamDriverContext&)> InAction)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamThenStep>(MoveTemp(InAction)));
}

FDreamDriverSequence& FDreamDriverSequence::Fail(const FString& InReason)
{
	using namespace DreamDriverSequenceLocal;
	return Add(MakeShared<FDreamFailStep>(InReason));
}

bool FDreamDriverSequence::Perform()
{
	using namespace DreamDriverSequenceLocal;
	if (Context == nullptr)
	{
		if (FAutomationTestBase* ReportingTest = FAutomationTestFramework::Get().GetCurrentTest())
		{
			ReportingTest->AddError(TEXT("A driver sequence was performed without a context."));
		}
		return false;
	}

	// A step that never finishes would otherwise hang the whole suite rather than fail one test.
	// Generous, because a Wait's own timeout should be what stops it in the ordinary case; this is
	// the backstop for a step whose timeout is longer than anyone meant.
	constexpr int32 MaxFramesPerStep = 100000;

	for (const FDreamDriverStepRef& Step : Steps)
	{
		int32 FramesSpent = 0;
		for (;;)
		{
			const EDreamDriverStepResult Result = Step->Execute(*Context, Context->FrameSeconds);
			if (Result == EDreamDriverStepResult::Done)
			{
				break;
			}
			if (Result == EDreamDriverStepResult::Failed)
			{
				ReportStepFailure(*Context, Step);
				return false;
			}
			if (++FramesSpent > MaxFramesPerStep)
			{
				if (FAutomationTestBase* ReportingTest = Context->CurrentTest != nullptr
					? Context->CurrentTest
					: FAutomationTestFramework::Get().GetCurrentTest())
				{
					ReportingTest->AddError(FString::Printf(
						TEXT("Driver step %s never finished: still asking for frames after %d of them."),
						*Step->Describe(), MaxFramesPerStep));
				}
				return false;
			}
			Context->PumpOneFrame(Context->FrameSeconds);
		}
	}
	return true;
}

void FDreamDriverSequence::PerformLatent()
{
	using namespace DreamDriverSequenceLocal;
	FAutomationTestFramework::Get().EnqueueLatentCommand(
		MakeShared<FDreamDriverSequenceLatentCommand>(Context, MoveTemp(Steps)));
}
