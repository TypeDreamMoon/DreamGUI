// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUITooltip.h"

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
	// Game and PIE only: a designer preview world hovering its own authoring surface must not grow
	// bubbles, and an editor world has Slate tooltips.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDreamUITooltipSubsystem::Deinitialize()
{
	if (UDreamEventSystem* EventSystem = SubscribedEventSystem.Get())
	{
		EventSystem->GetInputEvent().RemoveAll(this);
	}
	SubscribedEventSystem.Reset();
	DestroyTooltipWidgets();
	Super::Deinitialize();
}

TStatId UDreamUITooltipSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamUITooltipSubsystem, STATGROUP_Tickables);
}

void UDreamUITooltipSubsystem::EnsureSubscribed()
{
	if (SubscribedEventSystem.IsValid())
	{
		return;
	}
	UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(GetWorld(), 0);
	if (!IsValid(EventSystem))
	{
		return;
	}
	EventSystem->GetInputEvent().AddUObject(this, &UDreamUITooltipSubsystem::HandleInputEvent);
	SubscribedEventSystem = EventSystem;
}

void UDreamUITooltipSubsystem::HandleInputEvent(UDreamBaseEventData* InEventData)
{
	UDreamPointerEventData* PointerEvent = Cast<UDreamPointerEventData>(InEventData);
	if (!IsValid(PointerEvent))
	{
		return;
	}
	LastPointerEvent = PointerEvent;

	// Navigation arms a tooltip too, and used to do the opposite: any non-pointer event hid whatever
	// was showing and cleared the candidate, so a gamepad player could never see one at all. The two
	// arrive through the same enter/exit path -- that is what makes the confirm button press whatever
	// navigation landed on -- so the only difference worth keeping is WHERE the bubble goes: a pointer
	// position is meaningless in navigation mode, and the bubble is placed against the focused widget.
	const bool bIsNavigation = PointerEvent->InputType == EDreamUIPointerInputType::Navigation;

	switch (PointerEvent->EventType)
	{
	case EDreamUIPointerEventType::Enter:
	case EDreamUIPointerEventType::Exit:
	{
		UDreamWidget* NewCandidate = DreamUITooltipPolicy::ResolveTooltipSource(PointerEvent->EnterWidget);
		if (NewCandidate != Candidate.Get() || bIsNavigation != bArmedByNavigation)
		{
			Candidate = NewCandidate;
			bArmedByNavigation = bIsNavigation;
			HoverSeconds = 0.0f;
			// A press suppresses only the CURRENT target; moving to a new one re-arms.
			bSuppressed = false;
			if (ShownFor.IsValid() && ShownFor.Get() != NewCandidate)
			{
				HideTooltip();
				Candidate = NewCandidate;
				bArmedByNavigation = bIsNavigation;
			}
		}
		break;
	}
	case EDreamUIPointerEventType::Down:
	case EDreamUIPointerEventType::BeginDrag:
		// Standard tooltip behaviour everywhere: interacting with the thing dismisses its bubble.
		bSuppressed = true;
		HideTooltip();
		break;
	default:
		break;
	}
}

void UDreamUITooltipSubsystem::Tick(float DeltaTime)
{
	EnsureSubscribed();

	// The bubble's lifetime is the HOLDER's, not the source's. Asking ShownFor.IsValid() here left a
	// bubble whose source had been destroyed -- a recycled list row, a closed screen -- unreachable
	// by both branches: this one was skipped because the weak pointer had gone stale, and the dwell
	// branch below returned early on the equally-stale Candidate. Nothing called HideTooltip, so the
	// bubble stayed parked on the screen root until the next tooltip's ShowFor happened to clear it.
	if (IsValid(TooltipHolder))
	{
		UDreamWidget* Source = ShownFor.Get();
		if (Source == nullptr || Candidate.Get() != Source)
		{
			HideTooltip();
			return;
		}
		// Fonts load and lay out asynchronously; the size the bubble opened with may have been a
		// guess. Re-measuring every frame is one text layout read for the single visible bubble,
		// and it settles to a no-op the moment the numbers agree.
		SizeBubbleToText();
		UpdateTooltipPosition();
		return;
	}

	UDreamWidget* CandidateWidget = Candidate.Get();
	if (CandidateWidget == nullptr || bSuppressed)
	{
		HoverSeconds = 0.0f;
		return;
	}
	HoverSeconds += DeltaTime;
	if (HoverSeconds >= UDreamGUISettings::Get()->TooltipDelaySeconds)
	{
		ShowFor(CandidateWidget);
	}
}

void UDreamUITooltipSubsystem::HideTooltip()
{
	ShownFor.Reset();
	TooltipHost.Reset();
	HoverSeconds = 0.0f;
	DestroyTooltipWidgets();
}

void UDreamUITooltipSubsystem::ShowTooltipFor(UDreamWidget* InSource)
{
	if (!IsValid(InSource))
	{
		return;
	}
	// The candidate moves with the bubble, not just the bubble: the tick keeps a visible tooltip only
	// while the two agree, so showing one without arming the candidate would hide it again next frame.
	Candidate = InSource;
	// Anchored to the widget rather than to the pointer, because a caller asking for a specific
	// widget's help is not telling us anything about where a pointer is -- and the last pointer event
	// may be from somewhere else entirely, or from a previous screen.
	bArmedByNavigation = true;
	HoverSeconds = 0.0f;
	bSuppressed = false;
	ShowFor(InSource);
}

void UDreamUITooltipSubsystem::DestroyTooltipWidgets()
{
	if (IsValid(TooltipHolder))
	{
		TooltipHolder->DestroyWidget();
	}
	TooltipHolder = nullptr;
	BubbleText = nullptr;
	CustomTooltip = nullptr;
}

void UDreamUITooltipSubsystem::ShowFor(UDreamWidget* InSource)
{
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	// A tooltip belongs on the same screen as the widget it is about.
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRootForWidget(InSource) : nullptr;
	// ...and, for a world-space or render-target UI, on the same CANVAS. The screen overlay was the
	// only host this ever had, so a tooltip on a panel welded to a machine in the level landed on the
	// player's HUD instead -- at a position derived from a pointer position that a world-space
	// raycaster never had any reason to fill in meaningfully.
	UDreamWidget* Host = DreamUITooltipPolicy::ResolveTooltipHost(InSource, ScreenRoot);
	if (!IsValid(Host))
	{
		return;
	}
	TooltipHost = Host;

	// Which of the two content paths this source wants: a custom widget class beats the text.
	TSubclassOf<UDreamUserWidget> CustomClass = nullptr;
	if (InSource->GetClass()->ImplementsInterface(UDreamUITooltipSourceInterface::StaticClass()))
	{
		CustomClass = IDreamUITooltipSourceInterface::Execute_GetTooltipWidgetClass(InSource);
	}
	if (CustomClass == nullptr)
	{
		for (UDreamUIBehaviour* Component : InSource->GetAllComponents())
		{
			if (IsValid(Component) && Component->GetClass()->ImplementsInterface(UDreamUITooltipSourceInterface::StaticClass()))
			{
				CustomClass = IDreamUITooltipSourceInterface::Execute_GetTooltipWidgetClass(Component);
				if (CustomClass != nullptr)
				{
					break;
				}
			}
		}
	}
	if (CustomClass == nullptr && InSource->GetToolTipText().IsEmpty())
	{
		return;
	}

	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	DestroyTooltipWidgets();

	// The holder: its own canvas above the page band, raycast-disabled for the whole subtree so the
	// bubble can never sit between the pointer and the thing it describes.
	TooltipHolder = NewObject<UDreamWidget>(GetWorld(), NAME_None, RF_Transient);
	TooltipHolder->SetRaycastable(EDreamWidgetRaycastableType::Disabled);
	TooltipHolder->SetDisplayName(TEXT("DreamUITooltip"));
	TooltipHolder->SetPivot(FVector2D(0.0f, 1.0f));

	if (CustomClass != nullptr)
	{
		TooltipHolder->SetParentBeforeRegister(Host);
		RegisterDreamWidgetHierarchy(TooltipHolder);
		CustomTooltip = CreateDreamWidget(GetWorld(), CustomClass, TooltipHolder);
		if (IsValid(CustomTooltip))
		{
			// The holder adopts the content's authored size, so positioning has a real rect to clamp.
			TooltipHolder->SetSizeDelta(FVector2D(CustomTooltip->GetWidth(), CustomTooltip->GetHeight()));
			CustomTooltip->SetAnchoredPosition(FVector2D::ZeroVector);
		}
	}
	else
	{
		// The built-in bubble: a rect block behind a text, sized to the text's own preferred size,
		// wrapped at the settings' max width.
		UDreamWidget* TextWidget = NewObject<UDreamWidget>(GetWorld(), NAME_None, RF_Transient);
		TextWidget->SetDisplayName(TEXT("DreamUITooltipText"));
		BubbleText = TextWidget->CreateNewVisual<UDreamText>();
		BubbleText->SetText(InSource->GetToolTipText());
		BubbleText->SetFontSize(Settings->TooltipFontSize);
		BubbleText->SetColor(TooltipTextColor);

		UDreamRectBlock* Background = TooltipHolder->CreateNewVisual<UDreamRectBlock>();
		Background->SetColor(TooltipBackgroundColor);

		TextWidget->SetParentBeforeRegister(TooltipHolder);
		TooltipHolder->SetParentBeforeRegister(Host);
		RegisterDreamWidgetHierarchy(TooltipHolder);
	}

	// The sort canvas goes on BEFORE the text is measured: text layout early-outs without a render
	// canvas on the widget, and the first ship of this code measured first -- preferred width came
	// back zero and the bubble wrapped one character per line, a vertical strip of text.
	UDreamCanvas* Canvas = TooltipHolder->GetComponent<UDreamCanvas>();
	if (!IsValid(Canvas))
	{
		Canvas = Cast<UDreamCanvas>(TooltipHolder->AddComponent(UDreamCanvas::StaticClass()));
	}
	if (IsValid(Canvas))
	{
		Canvas->SetOverrideSorting(true);
		Canvas->SetSortOrder(TooltipSortOrder, /*PropagateToChildrenCanvas*/true);
	}

	if (IsValid(BubbleText))
	{
		SizeBubbleToText();
	}

	ShownFor = InSource;
	UpdateTooltipPosition();
}

void UDreamUITooltipSubsystem::SizeBubbleToText()
{
	UDreamWidget* TextWidget = IsValid(BubbleText) ? BubbleText->GetWidget() : nullptr;
	if (!IsValid(TextWidget) || !IsValid(TooltipHolder))
	{
		return;
	}
	// Preferred width unwrapped, clamped to the max, and the height asked at that width. A
	// not-ready measurement (font still loading, no canvas yet) answers near zero; the bubble
	// takes the full width for that frame rather than a one-character column, and the tick's
	// re-measure shrinks it the moment the text can answer.
	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	const float MaxTextWidth = FMath::Max(50.0f, Settings->TooltipMaxWidth - 2.0f * TooltipPadding);
	const float Preferred = BubbleText->GetPreferredWidth();
	const float TextWidth = Preferred > 1.0f ? FMath::Min(Preferred, MaxTextWidth) : MaxTextWidth;
	TextWidget->SetWidth(TextWidth);
	// Clamped because "not ready" is spelled as a negative, and SetHeight takes what it is given:
	// a bubble sized -1 tall is no better than one sized 0 and looks like arithmetic gone wrong.
	const float TextHeight = FMath::Max(BubbleText->GetPreferredHeight(), 0.0f);
	TextWidget->SetHeight(TextHeight);
	TextWidget->SetAnchoredPosition(FVector2D::ZeroVector);

	TooltipHolder->SetSizeDelta(FVector2D(TextWidth + 2.0f * TooltipPadding, TextHeight + 2.0f * TooltipPadding));
}

void UDreamUITooltipSubsystem::UpdateTooltipPosition()
{
	UDreamWidget* Host = TooltipHost.Get();
	UDreamWidget* Source = ShownFor.Get();
	if (!IsValid(TooltipHolder) || !IsValid(Host))
	{
		return;
	}

	// Where the bubble points, in the HOST's own local 2D space -- which is the space anchored
	// positions and the policy's bounds are both already in.
	FVector2D AnchorInHost = FVector2D::ZeroVector;
	if (!ResolveTooltipAnchor(Host, Source, AnchorInHost))
	{
		return;
	}

	const FVector2D HalfCanvas(Host->GetWidth() * 0.5f, Host->GetHeight() * 0.5f);
	const FVector2D TopLeft = DreamUITooltipPolicy::ComputeTooltipTopLeft(
		-HalfCanvas, HalfCanvas,
		FVector2D(TooltipHolder->GetWidth(), TooltipHolder->GetHeight()),
		AnchorInHost, UDreamGUISettings::Get()->TooltipOffset);
	TooltipHolder->SetAnchoredPosition(TopLeft);
}

bool UDreamUITooltipSubsystem::ResolveTooltipAnchor(UDreamWidget* InHost, UDreamWidget* InSource, FVector2D& OutAnchor) const
{
	UDreamPointerEventData* PointerEvent = LastPointerEvent.Get();
	// A pointer position is only an answer when a pointer put it there AND the host is the screen
	// overlay that position is measured in. Navigation has no pointer, and a world-space canvas has
	// no relationship to viewport pixels at all; both fall through to the source widget's own rect,
	// which is the thing the bubble is about and is expressed in the host's space by construction.
	const bool bPointerIsMeaningful = PointerEvent != nullptr
		&& !bArmedByNavigation
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
			// The conversion answers in the canvas's BOTTOM-LEFT origin (x right, y up from the
			// corner); anchored positions -- and the policy's bounds -- are CENTER-origin. The
			// walkthrough caught the bubble parked in a corner: this shift was missing, so a
			// corner-origin number was fed to a center-origin consumer.
			OutAnchor = PointerInCanvas - FVector2D(InHost->GetWidth() * 0.5f, InHost->GetHeight() * 0.5f);
			return true;
		}
	}

	if (!IsValid(InSource))
	{
		return false;
	}
	// The source's bottom-right corner, carried into the host's space through world space, so the
	// bubble hangs off the focused widget exactly the way it hangs off a pointer. Rotation and scale
	// on anything in between are handled by the transforms rather than assumed away.
	const FVector2D Centre = InSource->GetLocalSpaceCenter();
	const FVector2D Half(InSource->GetWidth() * 0.5f, InSource->GetHeight() * 0.5f);
	const FVector2D CornerLocal = Centre + FVector2D(Half.X, -Half.Y);
	const FVector CornerWorld = InSource->GetWorldTransform().TransformPosition(FVector(0.0f, CornerLocal.X, CornerLocal.Y));
	const FVector CornerInHost = InHost->GetWorldTransform().InverseTransformPosition(CornerWorld);
	OutAnchor = FVector2D(CornerInHost.Y, CornerInHost.Z);
	return true;
}
