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

	// v1 is pointer-only; the focus-driven gamepad tooltip is a later, separate arming path.
	if (PointerEvent->InputType != EDreamUIPointerInputType::Pointer)
	{
		HideTooltip();
		Candidate.Reset();
		return;
	}

	switch (PointerEvent->EventType)
	{
	case EDreamUIPointerEventType::Enter:
	case EDreamUIPointerEventType::Exit:
	{
		UDreamWidget* NewCandidate = DreamUITooltipPolicy::ResolveTooltipSource(PointerEvent->EnterWidget);
		if (NewCandidate != Candidate.Get())
		{
			Candidate = NewCandidate;
			HoverSeconds = 0.0f;
			// A press suppresses only the CURRENT target; moving to a new one re-arms.
			bSuppressed = false;
			if (ShownFor.IsValid() && ShownFor.Get() != NewCandidate)
			{
				HideTooltip();
				Candidate = NewCandidate;
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

	if (ShownFor.IsValid())
	{
		if (!Candidate.IsValid() || Candidate.Get() != ShownFor.Get())
		{
			HideTooltip();
			return;
		}
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
	HoverSeconds = 0.0f;
	DestroyTooltipWidgets();
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
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!IsValid(ScreenRoot))
	{
		return;
	}

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
		TooltipHolder->SetParentBeforeRegister(ScreenRoot);
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
		TooltipHolder->SetParentBeforeRegister(ScreenRoot);
		RegisterDreamWidgetHierarchy(TooltipHolder);

		// Measure AFTER registration so the text has a live layout to answer from: preferred width
		// unwrapped, clamped to the max, and the height asked at that width.
		const float MaxTextWidth = FMath::Max(50.0f, Settings->TooltipMaxWidth - 2.0f * TooltipPadding);
		const float TextWidth = FMath::Min(BubbleText->GetPreferredWidth(), MaxTextWidth);
		TextWidget->SetWidth(TextWidth);
		const float TextHeight = BubbleText->GetPreferredHeight();
		TextWidget->SetHeight(TextHeight);
		TextWidget->SetAnchoredPosition(FVector2D::ZeroVector);

		TooltipHolder->SetSizeDelta(FVector2D(TextWidth + 2.0f * TooltipPadding, TextHeight + 2.0f * TooltipPadding));
	}

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

	ShownFor = InSource;
	UpdateTooltipPosition();
}

void UDreamUITooltipSubsystem::UpdateTooltipPosition()
{
	UDreamPointerEventData* PointerEvent = LastPointerEvent.Get();
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(GetWorld());
	UDreamWidget* ScreenRoot = IsValid(ScreenUI) ? ScreenUI->GetOrCreateScreenRoot() : nullptr;
	if (!IsValid(TooltipHolder) || !IsValid(ScreenRoot) || PointerEvent == nullptr)
	{
		return;
	}
	UDreamCanvas* RootCanvas = ScreenRoot->GetComponent<UDreamCanvas>();
	if (!IsValid(RootCanvas))
	{
		return;
	}

	FVector2D PointerInCanvas = FVector2D::ZeroVector;
	if (!RootCanvas->ConvertPositionFromViewportToCanvas(FVector2D(PointerEvent->PointerPosition.X, PointerEvent->PointerPosition.Y), PointerInCanvas))
	{
		return;
	}

	const FVector2D HalfCanvas(ScreenRoot->GetWidth() * 0.5f, ScreenRoot->GetHeight() * 0.5f);
	// The conversion answers in the canvas's BOTTOM-LEFT origin (x right, y up from the corner);
	// anchored positions -- and the policy's ±HalfCanvas bounds -- are CENTER-origin. The
	// walkthrough caught the bubble parked in a corner: this shift was missing, so a corner-origin
	// number was fed to a center-origin consumer.
	PointerInCanvas -= HalfCanvas;
	const FVector2D TopLeft = DreamUITooltipPolicy::ComputeTooltipTopLeft(
		-HalfCanvas, HalfCanvas,
		FVector2D(TooltipHolder->GetWidth(), TooltipHolder->GetHeight()),
		PointerInCanvas, UDreamGUISettings::Get()->TooltipOffset);
	TooltipHolder->SetAnchoredPosition(TopLeft);
}
