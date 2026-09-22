// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverElement.h"

#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Misc/AutomationTest.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverProjection.h"

namespace DreamDriverElementLocal
{
	/**
	 * Say why an action could not happen, where a report will show it.
	 *
	 * An action on an element that is not there is a test failure and not a silent false: the return
	 * value tells the caller the gesture did not complete, but only the message says which element
	 * and which question found nothing.
	 */
	void ReportMissingElement(const FDreamDriverElement& InElement, const TCHAR* InAction)
	{
		const FString Message = FString::Printf(
			TEXT("Driver could not %s: no single widget answers %s."), InAction, *InElement.Describe());
		if (FAutomationTestBase* ReportingTest = FAutomationTestFramework::Get().GetCurrentTest())
		{
			ReportingTest->AddError(Message);
		}
	}
}

FDreamDriverElement::FDreamDriverElement(const TWeakPtr<FDreamDriver>& InDriver, const FDreamLocatorRef& InLocator)
	: Driver(InDriver)
	, Locator(InLocator)
{
}

UDreamWidget* FDreamDriverElement::GetWidget() const
{
	if (UDreamWidget* Cached = CachedWidget.Get(); IsValid(Cached))
	{
		return Cached;
	}
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	if (!PinnedDriver.IsValid())
	{
		return nullptr;
	}
	// Asked again exactly once, and the answer cached however it came out. Re-asking on every call
	// would let an ambiguous locator resolve to one widget for an action and another for the
	// assertion after it.
	UDreamWidget* Resolved = PinnedDriver->GetContext().FindOne(Locator);
	CachedWidget = Resolved;
	return Resolved;
}

bool FDreamDriverElement::Exists() const
{
	return GetWidget() != nullptr;
}

bool FDreamDriverElement::IsVisible() const
{
	const UDreamWidget* Widget = GetWidget();
	// In hierarchy, not the widget's own flag: a widget inside a hidden panel is not visible, and its
	// own flag says nothing about that.
	return Widget != nullptr && Widget->GetRenderVisibleInHierarchy();
}

bool FDreamDriverElement::IsInteractable() const
{
	const UDreamWidget* Widget = GetWidget();
	// The same predicate UDreamPointerInputModule::LineTrace uses to decide whether a hit counts, so
	// "interactable" here means exactly "input would reach it".
	return Widget != nullptr && Widget->GetInteractableInHierarchy();
}

bool FDreamDriverElement::IsHovered() const
{
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return false;
	}
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	if (!PinnedDriver.IsValid())
	{
		return false;
	}
	const UDreamPointerEventData* EventData = PinnedDriver->GetContext().GetPointerEventData(0);
	if (EventData == nullptr)
	{
		return false;
	}
	if (EventData->EnterWidget == Widget)
	{
		return true;
	}
	// The entered CHAIN counts too. A button whose label is the topmost hit is still the thing the
	// pointer is on, and it is the button that is sent the enter event; asking only about the leaf
	// would make "is the button hovered" false for every button with a child in front of it.
	return EventData->EnterWidgetStack.Contains(Widget);
}

bool FDreamDriverElement::IsPressed() const
{
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return false;
	}
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	if (!PinnedDriver.IsValid())
	{
		return false;
	}
	const UDreamPointerEventData* EventData = PinnedDriver->GetContext().GetPointerEventData(0);
	// Both halves: PressWidget survives the release long enough for the click to be dispatched from
	// it, so without the trigger state this would still say "pressed" after the button came up.
	return EventData != nullptr && EventData->bNowIsTriggerPressed && EventData->PressWidget == Widget;
}

bool FDreamDriverElement::IsSelected() const
{
	UDreamWidget* Widget = GetWidget();
	if (Widget == nullptr)
	{
		return false;
	}
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	if (!PinnedDriver.IsValid())
	{
		return false;
	}
	UDreamEventSystem* EventSystem = PinnedDriver->GetContext().EventSystem;
	return IsValid(EventSystem) && EventSystem->GetCurrentSelectedComponent(0) == Widget;
}

TOptional<FBox2D> FDreamDriverElement::GetPixelRect() const
{
	return FDreamDriverProjection::WidgetToPixelRect(GetWidget());
}

TOptional<FVector2D> FDreamDriverElement::GetCentrePixel() const
{
	return FDreamDriverProjection::WidgetCentrePixel(GetWidget());
}

bool FDreamDriverElement::Hover()
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("hover"));
		return false;
	}
	// Pinned to the widget already resolved, so the sequence cannot re-answer the locator differently.
	return PinnedDriver->Sequence().MoveTo(FDreamBy::Widget(Widget)).Perform();
}

bool FDreamDriverElement::MoveBy(const FVector2D& InPixelDelta)
{
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	if (!PinnedDriver.IsValid())
	{
		return false;
	}
	return PinnedDriver->Sequence().MoveBy(InPixelDelta).Perform();
}

bool FDreamDriverElement::Click(EDreamUIMouseButtonType InButton)
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("click"));
		return false;
	}
	return PinnedDriver->Sequence().Click(FDreamBy::Widget(Widget), InButton).Perform();
}

bool FDreamDriverElement::DoubleClick(EDreamUIMouseButtonType InButton)
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("double click"));
		return false;
	}
	// Two clicks in one sequence, which is six pumped frames apart at the pump's fixed frame length --
	// a tenth of the default double-click time. Whether the pipeline calls it a double click is the
	// pipeline's decision, made from the world clock the pump advances; nothing here asserts it.
	const FDreamLocatorRef PinnedLocator = FDreamBy::Widget(Widget);
	return PinnedDriver->Sequence()
		.Click(PinnedLocator, InButton)
		.Click(PinnedLocator, InButton)
		.Perform();
}

bool FDreamDriverElement::Press(EDreamUIMouseButtonType InButton)
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("press"));
		return false;
	}
	return PinnedDriver->Sequence().MoveTo(FDreamBy::Widget(Widget)).Press(InButton).Perform();
}

bool FDreamDriverElement::Release(EDreamUIMouseButtonType InButton)
{
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	if (!PinnedDriver.IsValid())
	{
		return false;
	}
	// Released where the pointer IS, not where this element is: a press that has been dragged away
	// ends where it ended, and moving back first would be a different gesture.
	return PinnedDriver->Sequence().Release(InButton).Perform();
}

bool FDreamDriverElement::DragTo(const TSharedRef<FDreamDriverElement>& InTarget)
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	UDreamWidget* TargetWidget = InTarget->GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("drag"));
		return false;
	}
	if (TargetWidget == nullptr)
	{
		ReportMissingElement(InTarget.Get(), TEXT("drag onto"));
		return false;
	}
	return PinnedDriver->Sequence()
		.DragTo(FDreamBy::Widget(Widget), FDreamBy::Widget(TargetWidget))
		.Perform();
}

bool FDreamDriverElement::DragBy(const FVector2D& InPixelDelta)
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("drag"));
		return false;
	}
	return PinnedDriver->Sequence().DragBy(FDreamBy::Widget(Widget), InPixelDelta).Perform();
}

bool FDreamDriverElement::ScrollBy(const FVector2D& InAxisValue)
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("scroll"));
		return false;
	}
	// Hovered first, and that is not politeness: InputScroll dispatches to the pointer's entered
	// widget, so a wheel turned without first being over anything goes nowhere.
	return PinnedDriver->Sequence()
		.MoveTo(FDreamBy::Widget(Widget))
		.ScrollBy(InAxisValue)
		.Perform();
}

bool FDreamDriverElement::Navigate(EDreamUINavigationDirection InDirection)
{
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	if (!PinnedDriver.IsValid())
	{
		return false;
	}
	return PinnedDriver->Sequence().Navigate(InDirection).Perform();
}

bool FDreamDriverElement::Select()
{
	using namespace DreamDriverElementLocal;
	const TSharedPtr<FDreamDriver> PinnedDriver = Driver.Pin();
	UDreamWidget* Widget = GetWidget();
	if (!PinnedDriver.IsValid() || Widget == nullptr)
	{
		ReportMissingElement(*this, TEXT("select"));
		return false;
	}
	UDreamEventSystem* EventSystem = PinnedDriver->GetContext().EventSystem;
	if (!IsValid(EventSystem))
	{
		return false;
	}
	// The production entry point, which creates the pointer's event data if this is the first thing
	// to touch it and dispatches the deselect/select pair the same way a press would.
	EventSystem->SetSelectComponentWithDefault(Widget);
	return true;
}
