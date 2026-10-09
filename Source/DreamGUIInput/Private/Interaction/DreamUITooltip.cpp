// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUITooltip.h"
#include "Core/DreamUIManager.h"

#include "Core/DreamGUISettings.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamBaseEventData.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamPointerEventData.h"
#include "DreamGUI.h"
#include "Engine/World.h"

namespace
{
	// Above the screen stack's band (base 1000, step 10) with generous headroom, inside the int16
	// clamp the canvas sort order lives under. Nothing pushed later may cover a tooltip.
	constexpr int32 TooltipSortOrder = 30000;
	constexpr float TooltipPadding = 10.0f;
	const FColor TooltipBackgroundColor(15, 15, 18, 235);
	const FColor TooltipTextColor(240, 240, 240, 255);
}

UDreamWidget* DreamUITooltipPolicy::ResolveTooltipSource(UDreamWidget* InEnterWidget)
{
	for (UDreamWidget* Widget = InEnterWidget; IsValid(Widget); Widget = Widget->GetParent())
	{
		if (!Widget->GetToolTipText().IsEmpty())
		{
			return Widget;
		}
		// The authored tooltip-widget class counts as an offer exactly as the text does. Without this
		// a widget that named a tooltip class and no text was walked straight past, and the bubble of
		// whichever ancestor happened to carry text showed instead.
		if (Widget->GetToolTipWidgetClass() != nullptr)
		{
			return Widget;
		}
		if (Widget->GetClass()->ImplementsInterface(UDreamUITooltipSourceInterface::StaticClass()))
		{
			return Widget;
		}
		for (UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (IsValid(Component) && Component->GetClass()->ImplementsInterface(UDreamUITooltipSourceInterface::StaticClass()))
			{
				return Widget;
			}
		}
	}
	return nullptr;
}

UDreamWidget* DreamUITooltipPolicy::ResolveTooltipHost(UDreamWidget* InSource, UDreamWidget* InScreenRoot)
{
	if (!IsValid(InSource))
	{
		return InScreenRoot;
	}
	// Screen-space and render-target UI both live under a screen root, and the screen root is where a
	// bubble has to go for them: it is above every page, and a pointer position means something there.
	if (InSource->IsScreenSpaceOverlayUI() || InSource->IsRenderTargetUI())
	{
		return InScreenRoot;
	}
	// World space: the bubble belongs on the same canvas as the thing it describes. Hosting it on the
	// screen overlay instead put a panel's tooltip on the player's HUD, positioned from a pointer
	// position a world-space raycaster had no reason to fill in.
	UDreamCanvas* RootCanvas = InSource->GetRootCanvas();
	UDreamWidget* CanvasWidget = IsValid(RootCanvas) ? RootCanvas->GetWidget() : nullptr;
	return IsValid(CanvasWidget) ? CanvasWidget : InScreenRoot;
}

FVector2D DreamUITooltipPolicy::ComputeTooltipTopLeft(const FVector2D& InCanvasMin, const FVector2D& InCanvasMax,
	const FVector2D& InBubbleSize, const FVector2D& InPointer, const FVector2D& InOffset)
{
	// Preferred: pivot at pointer + offset, bubble extending right (+X) and down (-Y) of its pivot.
	FVector2D TopLeft = InPointer + InOffset;

	// Flip, not slide, when the preferred side runs out: a bubble that slides stays under the
	// pointer and gets hovered through; one that flips lands on the other side of it.
	if (TopLeft.X + InBubbleSize.X > InCanvasMax.X)
	{
		TopLeft.X = InPointer.X - InOffset.X - InBubbleSize.X;
	}
	if (TopLeft.Y - InBubbleSize.Y < InCanvasMin.Y)
	{
		TopLeft.Y = InPointer.Y - InOffset.Y + InBubbleSize.Y;
	}

	// And clamp outright for the bubble bigger than the space on either side.
	TopLeft.X = FMath::Clamp(TopLeft.X, InCanvasMin.X, FMath::Max(InCanvasMin.X, InCanvasMax.X - InBubbleSize.X));
	TopLeft.Y = FMath::Clamp(TopLeft.Y, FMath::Min(InCanvasMax.Y, InCanvasMin.Y + InBubbleSize.Y), InCanvasMax.Y);
	return TopLeft;
}

UDreamUITooltipSubsystem* UDreamUITooltipSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = IsValid(WorldContextObject) ? WorldContextObject->GetWorld() : nullptr;
	return IsValid(World) ? World->GetSubsystem<UDreamUITooltipSubsystem>() : nullptr;
}

bool UDreamUITooltipSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

bool UDreamUITooltipSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// Game and PIE only: a designer preview world hovering its own authoring surface must not grow bubbles, and an
	// editor world has Slate tooltips.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDreamUITooltipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	DreamUI::EnrolWorldService(Collection, *this, *this);
	// Every player's events, from the moment the world has input: no player to find first, and nothing to lose when
	// an event system is replaced.
	if (UDreamUIInputSubsystem* Input = Collection.InitializeDependency<UDreamUIInputSubsystem>())
	{
		Input->GetOnInputEvent().AddUObject(this, &UDreamUITooltipSubsystem::HandleInputEvent);
		InputSubsystem = Input;
	}
}

void UDreamUITooltipSubsystem::Deinitialize()
{
	// Passive: the world's teardown has taken this service down already (TeardownForWorld), unless the world had
	// no manager to take it.
	if (!bTornDownForWorld && GetWorld() != nullptr)
	{
		TeardownForWorld(*GetWorld());
	}
	Super::Deinitialize();
}

void UDreamUITooltipSubsystem::TeardownForWorld(UWorld& InWorld)
{
	if (bTornDownForWorld)
	{
		return;
	}
	bTornDownForWorld = true;
	if (UDreamUIInputSubsystem* Input = InputSubsystem.Get())
	{
		Input->GetOnInputEvent().RemoveAll(this);
	}
	InputSubsystem.Reset();
	for (TPair<int32, FDreamUITooltipUserState>& Pair : UserStates)
	{
		DestroyTooltipWidgets(Pair.Value);
	}
	UserStates.Reset();
}

TStatId UDreamUITooltipSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamUITooltipSubsystem, STATGROUP_Tickables);
}

UDreamWidget* UDreamUITooltipSubsystem::GetShownForUser(int32 InUserIndex) const
{
	const FDreamUITooltipUserState* State = UserStates.Find(InUserIndex);
	return State != nullptr ? State->ShownFor.Get() : nullptr;
}

UDreamWidget* UDreamUITooltipSubsystem::GetBubbleForUser(int32 InUserIndex) const
{
	const FDreamUITooltipUserState* State = UserStates.Find(InUserIndex);
	return State != nullptr ? State->TooltipHolder.Get() : nullptr;
}

void UDreamUITooltipSubsystem::HandleInputEvent(UDreamBaseEventData* InEventData)
{
	UDreamPointerEventData* PointerEvent = Cast<UDreamPointerEventData>(InEventData);
	if (!IsValid(PointerEvent) || bTornDownForWorld)
	{
		return;
	}
	// The player whose pointer it is: one player's hover never moves or hides another's bubble.
	const int32 UserIndex = PointerEvent->UserIndex;
	FDreamUITooltipUserState& State = UserStates.FindOrAdd(UserIndex);

	// A finger is not a cursor. Slate asks for tooltips at each user's cursor alone (FSlateUser::UpdateTooltip,
	// at GetCursorPosition, the cursor's pointer index), and a touch is a pointer of its own: it never arms a
	// bubble, never takes one over and never carries one. Its press is still a press of the player's -- Slate
	// holds the cursor's tooltip shut while one is down -- so a bubble already up goes, and its dwell starts
	// again under whatever pointer it follows.
	if (PointerEvent->InputType == EDreamUIPointerInputType::Pointer && DreamUIPointerIds::IsTouch(PointerEvent->PointerID))
	{
		if (PointerEvent->EventType == EDreamUIPointerEventType::Down || PointerEvent->EventType == EDreamUIPointerEventType::BeginDrag)
		{
			HideUserTooltip(State);
		}
		return;
	}

	switch (PointerEvent->EventType)
	{
	case EDreamUIPointerEventType::Enter:
	case EDreamUIPointerEventType::Exit:
	{
		// One tooltip per player, following one of the player's pointers: the one that last arrived at something with a
		// tooltip. Another pointer -- a second laser, a script's -- takes it over only by arriving at a tooltip of its
		// own; its moves over nothing leave the bubble under the first one alone. (A finger is none of them: above.)
		const bool bFollowed = !State.LastPointerEvent.IsValid() || State.LastPointerEvent.Get() == PointerEvent;
		const bool bTakesOver = !bFollowed && PointerEvent->EventType == EDreamUIPointerEventType::Enter
			&& DreamUITooltipPolicy::ResolveTooltipSource(PointerEvent->EnterWidget) != nullptr;
		if (bFollowed || bTakesOver)
		{
			State.LastPointerEvent = PointerEvent;
			// What the pointer is over is read once the frame's exits and enters are all out (RefreshCandidate). An
			// Exit goes out while EnterWidget still names the widget being left, and a pointer that left for nothing --
			// or for a parent it was already inside -- is sent no Enter afterwards to say where it went: read here, the
			// candidate stayed the widget it had left, and its bubble opened over empty space and stayed there.
			State.bCandidateStale = true;
		}
		break;
	}
	case EDreamUIPointerEventType::Down:
	case EDreamUIPointerEventType::BeginDrag:
		// A press in the frame its pointer arrived suppresses what it arrived at, not what it left.
		RefreshCandidate(State);
		// Standard tooltip behaviour everywhere: interacting with the thing dismisses its bubble.
		// Refresh can destroy custom content, whose callback can grow the per-user map.
		if (FDreamUITooltipUserState* CurrentState = UserStates.Find(UserIndex))
		{
			CurrentState->bSuppressed = true;
			HideUserTooltip(*CurrentState);
		}
		break;
	default:
		break;
	}
}

void UDreamUITooltipSubsystem::RefreshCandidate(FDreamUITooltipUserState& State)
{
	if (!State.bCandidateStale)
	{
		return;
	}
	State.bCandidateStale = false;
	const UDreamPointerEventData* PointerEvent = State.LastPointerEvent.Get();
	UDreamWidget* NewCandidate = PointerEvent != nullptr ? DreamUITooltipPolicy::ResolveTooltipSource(PointerEvent->EnterWidget) : nullptr;
	// Navigation arms a tooltip too. The two arrive through the same enter/exit path, so the only difference worth
	// keeping is WHERE the bubble goes: a pointer position is meaningless in navigation mode, and the bubble is placed
	// against the focused widget.
	const bool bIsNavigation = PointerEvent != nullptr && PointerEvent->InputType == EDreamUIPointerInputType::Navigation;
	if (NewCandidate != State.Candidate.Get() || bIsNavigation != State.bArmedByNavigation)
	{
		State.Candidate = NewCandidate;
		State.bArmedByNavigation = bIsNavigation;
		State.HoverSeconds = 0.0f;
		// A press suppresses only the CURRENT target; moving to a new one re-arms.
		State.bSuppressed = false;
		if (State.ShownFor.IsValid() && State.ShownFor.Get() != NewCandidate)
		{
			HideUserTooltip(State);
		}
	}
}

void UDreamUITooltipSubsystem::Tick(float DeltaTime)
{
	// The UI clock: real time, so a tooltip in a slowed-down game waits as long as it does at full speed, and one in
	// a paused game's menu still appears.
	const float RealDeltaSeconds = DreamUIInputClock::GetUIDeltaSeconds(this, DeltaTime);
	TArray<int32> UserIndices;
	UserStates.GetKeys(UserIndices);
	for (const int32 UserIndex : UserIndices)
	{
		if (FDreamUITooltipUserState* State = UserStates.Find(UserIndex))
		{
			RefreshCandidate(*State);
		}
		if (FDreamUITooltipUserState* State = UserStates.Find(UserIndex))
		{
			TickUser(UserIndex, *State, RealDeltaSeconds);
		}
	}
}

void UDreamUITooltipSubsystem::TickUser(int32 InUserIndex, FDreamUITooltipUserState& State, float InDeltaSeconds)
{
	// The bubble's lifetime is the HOLDER's, not the source's: a bubble whose source was destroyed -- a recycled
	// list row, a closed screen -- is hidden here rather than left parked on the screen root.
	if (IsValid(State.TooltipHolder))
	{
		UDreamWidget* Source = State.ShownFor.Get();
		if (Source == nullptr || State.Candidate.Get() != Source)
		{
			HideUserTooltip(State);
			return;
		}
		// Fonts load and lay out asynchronously; the size the bubble opened with may have been a guess.
		SizeBubbleToText(State);
		UpdateTooltipPosition(State);
		return;
	}

	UDreamWidget* CandidateWidget = State.Candidate.Get();
	if (CandidateWidget == nullptr || State.bSuppressed)
	{
		State.HoverSeconds = 0.0f;
		return;
	}
	State.HoverSeconds += InDeltaSeconds;
	if (State.HoverSeconds >= UDreamGUISettings::Get()->TooltipDelaySeconds)
	{
		ShowFor(InUserIndex, CandidateWidget);
	}
}

void UDreamUITooltipSubsystem::HideTooltip()
{
	TArray<int32> UserIndices;
	UserStates.GetKeys(UserIndices);
	for (const int32 UserIndex : UserIndices)
	{
		HideTooltipForUser(UserIndex);
	}
}

void UDreamUITooltipSubsystem::HideTooltipForUser(int32 InUserIndex)
{
	if (FDreamUITooltipUserState* State = UserStates.Find(InUserIndex))
	{
		HideUserTooltip(*State);
	}
}

void UDreamUITooltipSubsystem::HideUserTooltip(FDreamUITooltipUserState& State)
{
	++State.OperationSerial;
	State.ShownFor.Reset();
	State.TooltipHost.Reset();
	State.HoverSeconds = 0.0f;
	DestroyTooltipWidgets(State);
}

void UDreamUITooltipSubsystem::ShowTooltipFor(UDreamWidget* InSource)
{
	if (!IsValid(InSource) || bTornDownForWorld)
	{
		return;
	}
	const int32 UserIndex = InSource->GetOwningPlayerIndex();
	FDreamUITooltipUserState& State = UserStates.FindOrAdd(UserIndex);
	// The candidate moves with the bubble, not just the bubble: the tick keeps a visible tooltip only while the two
	// agree, so showing one without arming the candidate would hide it again next frame.
	State.Candidate = InSource;
	// Anchored to the widget rather than to the pointer: a caller asking for a specific widget's help is not telling
	// us anything about where a pointer is.
	State.bArmedByNavigation = true;
	State.HoverSeconds = 0.0f;
	State.bSuppressed = false;
	// Asked for by name, over whatever the pointer's enters and exits this frame said: the next of those re-arms.
	State.bCandidateStale = false;
	ShowFor(UserIndex, InSource);
}

void UDreamUITooltipSubsystem::DestroyTooltipWidgets(FDreamUITooltipUserState& State)
{
	UDreamWidget* Holder = State.TooltipHolder;
	State.TooltipHolder = nullptr;
	State.BubbleText = nullptr;
	State.CustomTooltip = nullptr;
	// Detach before running Destruct: it may show another tooltip, and that new holder
	// belongs to the new operation. State itself may also move when another user is added.
	if (IsValid(Holder))Holder->DestroyWidget();
}

void UDreamUITooltipSubsystem::ShowFor(int32 InUserIndex, UDreamWidget* InSource)
{
	FDreamUITooltipUserState* State = UserStates.Find(InUserIndex);
	if (State == nullptr || bTornDownForWorld || !IsValid(InSource))return;
	const uint64 OperationSerial = ++State->OperationSerial;
	const TWeakObjectPtr<UDreamWidget> Source(InSource);
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	// A tooltip belongs on the same screen as the widget it is about, and -- for a world-space UI -- on the same canvas.
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRootForWidget(InSource) : nullptr;
	UDreamWidget* Host = DreamUITooltipPolicy::ResolveTooltipHost(InSource, ScreenRoot);
	if (!IsValid(Host))
	{
		return;
	}
	const TWeakObjectPtr<UDreamWidget> WeakHost(Host);
	auto FindCurrentState = [this, InUserIndex, OperationSerial, Source, WeakHost]() -> FDreamUITooltipUserState*
	{
		if (bTornDownForWorld || !Source.IsValid() || !WeakHost.IsValid())return nullptr;
		FDreamUITooltipUserState* Current = UserStates.Find(InUserIndex);
		return Current != nullptr && Current->OperationSerial == OperationSerial ? Current : nullptr;
	};
	if (FindCurrentState() == nullptr)return;

	// Which of the two content paths this source wants: a custom widget class beats the text.
	TSubclassOf<UDreamUserWidget> CustomClass = nullptr;
	if (InSource->GetClass()->ImplementsInterface(UDreamUITooltipSourceInterface::StaticClass()))
	{
		CustomClass = IDreamUITooltipSourceInterface::Execute_GetTooltipWidgetClass(InSource);
		if (FindCurrentState() == nullptr)return;
	}
	if (CustomClass == nullptr)
	{
		TArray<TWeakObjectPtr<UDreamUIBehaviour>> Components;
		for (UDreamUIBehaviour* Component : InSource->GetAllComponents())Components.Add(Component);
		for (const TWeakObjectPtr<UDreamUIBehaviour>& WeakComponent : Components)
		{
			UDreamUIBehaviour* Component = WeakComponent.Get();
			if (IsValid(Component) && Component->GetClass()->ImplementsInterface(UDreamUITooltipSourceInterface::StaticClass()))
			{
				CustomClass = IDreamUITooltipSourceInterface::Execute_GetTooltipWidgetClass(Component);
				if (FindCurrentState() == nullptr)return;
				if (CustomClass != nullptr)
				{
					break;
				}
			}
		}
	}
	if (CustomClass == nullptr)
	{
		// The authored answer, after the two interface paths: an implementor that computes a class at runtime is being
		// specific and should win over a class typed into the details panel.
		CustomClass = InSource->GetToolTipWidgetClass();
	}
	if (CustomClass == nullptr && InSource->GetToolTipText().IsEmpty())
	{
		return;
	}

	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	State = FindCurrentState();
	if (State == nullptr)return;
	DestroyTooltipWidgets(*State);
	State = FindCurrentState();
	if (State == nullptr)return;
	State->TooltipHost = Host;

	// The holder: its own canvas above the page band, raycast-disabled for the whole subtree so the bubble can never
	// sit between the pointer and the thing it describes.
	UDreamWidget* Holder = NewObject<UDreamWidget>(this, NAME_None, RF_Transient);
	const TWeakObjectPtr<UDreamWidget> PendingHolder(Holder);
	State->TooltipHolder = Holder;
	Holder->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
	Holder->SetDisplayName(TEXT("DreamUITooltip"));
	Holder->SetPivot(FVector2D(0.0f, 1.0f));

	if (CustomClass != nullptr)
	{
		Holder->SetParentBeforeRegister(Host);
		RegisterDreamWidgetHierarchy(Holder);
		UDreamUserWidget* CustomTooltip = CreateDreamWidget(GetWorld(), CustomClass, Holder);
		State = FindCurrentState();
		if (State == nullptr || !PendingHolder.IsValid())
		{
			// Initialize runs before the factory parents its instance. If it cancels this show,
			// the factory can finish with an unparented, registered widget after Holder has gone.
			// Dispose of that abandoned instance unless user code explicitly gave it another parent.
			if (IsValid(CustomTooltip)
				&& (!IsValid(CustomTooltip->GetParent()) || CustomTooltip->GetParent() == Holder))
			{
				CustomTooltip->DestroyWidget();
			}
			return;
		}
		State->CustomTooltip = CustomTooltip;
		if (IsValid(CustomTooltip))
		{
			// The holder adopts the content's authored size, so positioning has a real rect to clamp.
			const TWeakObjectPtr<UDreamUserWidget> PendingCustom(CustomTooltip);
			Holder->SetSizeDelta(FVector2D(CustomTooltip->GetWidth(), CustomTooltip->GetHeight()));
			State = FindCurrentState();
			if (State == nullptr || !PendingHolder.IsValid() || !PendingCustom.IsValid())return;
			CustomTooltip->SetAnchoredPosition(FVector2D::ZeroVector);
			State = FindCurrentState();
			if (State == nullptr || !PendingHolder.IsValid())return;
		}
	}
	else
	{
		// The built-in bubble: a rect block behind a text, sized to the text's own preferred size, wrapped at the
		// settings' max width.
		UDreamWidget* TextWidget = NewObject<UDreamWidget>(this, NAME_None, RF_Transient);
		TextWidget->SetDisplayName(TEXT("DreamUITooltipText"));
		State->BubbleText = TextWidget->CreateNewVisual<UDreamText>();
		State->BubbleText->SetText(InSource->GetToolTipText());
		State->BubbleText->SetFontSize(Settings->TooltipFontSize);
		State->BubbleText->SetColor(TooltipTextColor);

		UDreamRectBlock* Background = Holder->CreateNewVisual<UDreamRectBlock>();
		Background->SetColor(TooltipBackgroundColor);

		TextWidget->SetParentBeforeRegister(Holder);
		Holder->SetParentBeforeRegister(Host);
		RegisterDreamWidgetHierarchy(Holder);
	}

	// The sort canvas goes on BEFORE the text is measured: text layout early-outs without a render canvas on the
	// widget, and measured first the bubble wrapped one character per line.
	UDreamCanvas* Canvas = Holder->GetComponent<UDreamCanvas>();
	if (!IsValid(Canvas))
	{
		Canvas = Cast<UDreamCanvas>(Holder->AddComponent(UDreamCanvas::StaticClass()));
	}
	if (IsValid(Canvas))
	{
		Canvas->SetOverrideSorting(true);
		Canvas->SetSortOrder(TooltipSortOrder, /*PropagateToChildrenCanvas*/true);
	}

	if (IsValid(State->BubbleText))
	{
		SizeBubbleToText(*State);
	}

	State->ShownFor = InSource;
	UpdateTooltipPosition(*State);
}

void UDreamUITooltipSubsystem::SizeBubbleToText(FDreamUITooltipUserState& State)
{
	UDreamWidget* TextWidget = IsValid(State.BubbleText) ? State.BubbleText->GetWidget() : nullptr;
	if (!IsValid(TextWidget) || !IsValid(State.TooltipHolder))
	{
		return;
	}
	// Preferred width unwrapped, clamped to the max, and the height asked at that width. A not-ready measurement
	// answers near zero; the bubble takes the full width for that frame rather than a one-character column.
	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	const float MaxTextWidth = FMath::Max(50.0f, Settings->TooltipMaxWidth - 2.0f * TooltipPadding);
	const float Preferred = State.BubbleText->GetPreferredWidth();
	const float TextWidth = Preferred > 1.0f ? FMath::Min(Preferred, MaxTextWidth) : MaxTextWidth;
	TextWidget->SetWidth(TextWidth);
	// Clamped because "not ready" is spelled as a negative.
	const float TextHeight = FMath::Max(State.BubbleText->GetPreferredHeight(), 0.0f);
	TextWidget->SetHeight(TextHeight);
	TextWidget->SetAnchoredPosition(FVector2D::ZeroVector);

	State.TooltipHolder->SetSizeDelta(FVector2D(TextWidth + 2.0f * TooltipPadding, TextHeight + 2.0f * TooltipPadding));
}

void UDreamUITooltipSubsystem::UpdateTooltipPosition(FDreamUITooltipUserState& State)
{
	UDreamWidget* Host = State.TooltipHost.Get();
	UDreamWidget* Source = State.ShownFor.Get();
	if (!IsValid(State.TooltipHolder) || !IsValid(Host))
	{
		return;
	}

	// Where the bubble points, in the HOST's own local 2D space -- the space anchored positions and the policy's
	// bounds are both already in.
	FVector2D AnchorInHost = FVector2D::ZeroVector;
	if (!ResolveTooltipAnchor(State, Host, Source, AnchorInHost))
	{
		return;
	}

	const FVector2D HalfCanvas(Host->GetWidth() * 0.5f, Host->GetHeight() * 0.5f);
	const FVector2D TopLeft = DreamUITooltipPolicy::ComputeTooltipTopLeft(
		-HalfCanvas, HalfCanvas,
		FVector2D(State.TooltipHolder->GetWidth(), State.TooltipHolder->GetHeight()),
		AnchorInHost, UDreamGUISettings::Get()->TooltipOffset);
	State.TooltipHolder->SetAnchoredPosition(TopLeft);
}

bool UDreamUITooltipSubsystem::ResolveTooltipAnchor(const FDreamUITooltipUserState& State, UDreamWidget* InHost, UDreamWidget* InSource, FVector2D& OutAnchor) const
{
	UDreamPointerEventData* PointerEvent = State.LastPointerEvent.Get();
	// A pointer position is only an answer when a pointer put it there AND the host is the screen overlay that
	// position is measured in. Navigation has no pointer, and a world-space canvas has no relationship to viewport
	// pixels at all; both fall through to the source widget's own rect.
	const bool bPointerIsMeaningful = PointerEvent != nullptr
		&& !State.bArmedByNavigation
		&& PointerEvent->InputType == EDreamUIPointerInputType::Pointer
		&& !InHost->IsWorldSpaceUI();
	if (bPointerIsMeaningful)
	{
		UDreamCanvas* HostCanvas = InHost->GetComponent<UDreamCanvas>();
		FVector2D PointerInCanvas = FVector2D::ZeroVector;
		if (IsValid(HostCanvas)
			&& HostCanvas->ConvertPositionFromViewportToCanvas(
				FVector2D(PointerEvent->PointerPosition.X, PointerEvent->PointerPosition.Y), PointerInCanvas))
		{
			// The conversion answers in the canvas's BOTTOM-LEFT origin; anchored positions -- and the policy's
			// bounds -- are CENTER-origin.
			OutAnchor = PointerInCanvas - FVector2D(InHost->GetWidth() * 0.5f, InHost->GetHeight() * 0.5f);
			return true;
		}
	}

	if (!IsValid(InSource))
	{
		return false;
	}
	// The source's bottom-right corner, carried into the host's space through world space, so the bubble hangs off
	// the focused widget exactly the way it hangs off a pointer.
	const FVector2D Centre = InSource->GetLocalSpaceCenter();
	const FVector2D Half(InSource->GetWidth() * 0.5f, InSource->GetHeight() * 0.5f);
	const FVector2D CornerLocal = Centre + FVector2D(Half.X, -Half.Y);
	const FVector CornerWorld = InSource->GetWorldTransform().TransformPosition(FVector(0.0f, CornerLocal.X, CornerLocal.Y));
	const FVector CornerInHost = InHost->GetWorldTransform().InverseTransformPosition(CornerWorld);
	OutAnchor = FVector2D(CornerInHost.Y, CornerInHost.Z);
	return true;
}
