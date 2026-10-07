// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/Designer/DreamSlatePanelDriver.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Layout/Children.h"
#include "Layout/Geometry.h"
#include "Layout/WidgetPath.h"
#include "Widgets/SWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#include "Driver/DreamDriverKeys.h"

namespace DreamTests
{
	namespace DreamSlatePanelLocal
	{
		/** The cursor user's pointer event, as FSlateApplication::OnMouseMove and its siblings build one from a platform message. */
		FPointerEvent MakePointerEvent(const FVector2D& InScreenPosition, const FVector2D& InLastScreenPosition,
			const TSet<FKey>& InPressedButtons, const FKey& InEffectingButton)
		{
			return FPointerEvent(
				FSlateApplicationBase::CursorPointerIndex,
				InScreenPosition,
				InLastScreenPosition,
				InPressedButtons,
				InEffectingButton,
				/*WheelDelta*/ 0.0f,
				FModifierKeysState());
		}

		TSet<FKey> LeftButtonHeld()
		{
			TSet<FKey> Held;
			Held.Add(EKeys::LeftMouseButton);
			return Held;
		}

		/**
		 * The widget a hit test at InPoint would end on, InWidget or inside it: at every level the last-arranged child --
		 * the one drawn on top -- whose rect holds the point, and in it the same again, down to the leafmost one that takes
		 * hits; null when nothing there does. Slate's hit test answers the leafmost widget under the pointer and routes the
		 * event up from it (FHittestGrid::GetBubblePath), so a press aimed at a menu entry is the button's inside it, as a
		 * hand's is; routed from the entry's outer block instead, it went up from there and never reached the button.
		 */
		TSharedPtr<SWidget> DeepestHitUnder(const TSharedRef<SWidget>& InWidget, const FGeometry& InGeometry, const FVector2D& InPoint)
		{
			const EVisibility Visibility = InWidget->GetVisibility();
			const FVector2D LocalSize = InGeometry.GetLocalSize();
			if (!Visibility.IsVisible() || LocalSize.X <= 0.0 || LocalSize.Y <= 0.0 || !InGeometry.IsUnderLocation(InPoint))
			{
				return nullptr;
			}
			if (Visibility.AreChildrenHitTestVisible())
			{
				FArrangedChildren Arranged(EVisibility::Visible);
				InWidget->ArrangeChildren(InGeometry, Arranged);
				for (int32 Index = Arranged.Num() - 1; Index >= 0; --Index)
				{
					if (const TSharedPtr<SWidget> Hit = DeepestHitUnder(Arranged[Index].Widget, Arranged[Index].Geometry, InPoint))
					{
						return Hit;
					}
				}
			}
			return Visibility.IsHitTestVisible() ? TSharedPtr<SWidget>(InWidget) : nullptr;
		}

		/** InWidget's deepest hit-testable widget at InPoint, or InWidget itself when nothing in it takes hits there. */
		TSharedRef<SWidget> HitTargetAt(const TSharedRef<SWidget>& InWidget, const FVector2D& InPoint)
		{
			const TSharedPtr<SWidget> Hit = DeepestHitUnder(InWidget, InWidget->GetPaintSpaceGeometry(), InPoint);
			return Hit.IsValid() ? Hit.ToSharedRef() : InWidget;
		}

		/** The path Slate would route along to InWidget, or false with the reason. */
		bool PathTo(const TSharedRef<SWidget>& InWidget, FWidgetPath& OutPath, FString& OutWhyNot)
		{
			if (!FSlateApplication::Get().FindPathToWidget(InWidget, OutPath) || !OutPath.IsValid())
			{
				OutWhyNot = FString::Printf(TEXT("Slate has no path to %s: it is in no window, or something above it is not visible"),
					*DreamSlatePanel::Describe(InWidget));
				return false;
			}
			return true;
		}

		/**
		 * InPath as Slate routes a pointer event along one: with the pointer's position at every widget on it, which
		 * FEventRouter's policies read for each widget they visit (FWidgetPath::GetVirtualPointerPosition). A path from
		 * FindPathToWidget has none of those positions -- its array is empty -- and routing a pointer event along it reads
		 * past the end of it, which asserts. Built the way Slate builds the paths it keeps between events
		 * (FWeakWidgetPath::ToWidgetPath with the event, as for the widgets last under the cursor), and again for each
		 * event, from the widgets as they are by then: the event before may have changed them -- a click that opened a
		 * menu, a press that closed one. A path cut short since keeps what is left of it, as Slate's do.
		 */
		FWidgetPath RoutablePath(const FWidgetPath& InPath, const FPointerEvent& InEvent)
		{
			return FWeakWidgetPath(InPath).ToWidgetPath(FWeakWidgetPath::EInterruptedPathHandling::Truncate, &InEvent);
		}

		/** A move, by InRoute: to whatever is under the pointer, or along InPath. */
		void Move(EDreamSlateRoute InRoute, const FWidgetPath& InPath, const FPointerEvent& InEvent)
		{
			FSlateApplication& Slate = FSlateApplication::Get();
			if (InRoute == EDreamSlateRoute::HitTest)
			{
				Slate.ProcessMouseMoveEvent(InEvent);
			}
			else
			{
				Slate.RoutePointerMoveEvent(RoutablePath(InPath, InEvent), InEvent, /*bIsSynthetic*/ false);
			}
		}

		void Down(EDreamSlateRoute InRoute, const FWidgetPath& InPath, const FPointerEvent& InEvent)
		{
			FSlateApplication& Slate = FSlateApplication::Get();
			if (InRoute == EDreamSlateRoute::HitTest)
			{
				// No platform window: the one thing Slate does with it is ask the platform to capture the desk's mouse.
				Slate.ProcessMouseButtonDownEvent(nullptr, InEvent);
			}
			else
			{
				Slate.RoutePointerDownEvent(RoutablePath(InPath, InEvent), InEvent);
			}
		}

		void Up(EDreamSlateRoute InRoute, const FWidgetPath& InPath, const FPointerEvent& InEvent)
		{
			FSlateApplication& Slate = FSlateApplication::Get();
			if (InRoute == EDreamSlateRoute::HitTest)
			{
				Slate.ProcessMouseButtonUpEvent(InEvent);
			}
			else
			{
				// A captured pointer is let go of to its captor whatever path is handed in (RoutePointerUpEvent asks the user's
				// capture first), and a drag and drop ends on the path's widgets.
				Slate.RoutePointerUpEvent(RoutablePath(InPath, InEvent), InEvent);
			}
		}

		/** DragOnto and DragOntoAt: the drag, ending at InToPoint, which is InTo's middle or a point inside it. */
		bool DragToPoint(const TSharedRef<SWidget>& InFrom, const TSharedRef<SWidget>& InTo, const TOptional<FVector2D>& InToPoint,
			EDreamSlateRoute InRoute, int32 InSteps, FString& OutWhyNot)
		{
			FSlateApplication& Slate = FSlateApplication::Get();
			const TOptional<FVector2D> From = DreamSlatePanel::CentreOf(InFrom);
			const TOptional<FVector2D>& To = InToPoint;
			if (!From.IsSet() || !To.IsSet())
			{
				OutWhyNot = FString::Printf(TEXT("the drag needs both ends painted: from %s, to %s"), *DreamSlatePanel::Describe(InFrom), *DreamSlatePanel::Describe(InTo));
				return false;
			}
			FWidgetPath FromPath;
			FWidgetPath ToPath;
			// Each end's path down to what a hit test there would end on, as the events of a hand go.
			if (InRoute == EDreamSlateRoute::WidgetPath
				&& (!PathTo(HitTargetAt(InFrom, From.GetValue()), FromPath, OutWhyNot) || !PathTo(HitTargetAt(InTo, To.GetValue()), ToPath, OutWhyNot)))
			{
				return false;
			}

			Move(InRoute, FromPath, MakePointerEvent(From.GetValue(), From.GetValue(), TSet<FKey>(), EKeys::Invalid));
			Down(InRoute, FromPath, MakePointerEvent(From.GetValue(), From.GetValue(), LeftButtonHeld(), EKeys::LeftMouseButton));

			// Past the trigger distance on the first move, with room to spare: Slate measures it in its own units, and a move
			// that only reaches it is still a press.
			FVector2D Direction = To.GetValue() - From.GetValue();
			Direction = Direction.IsNearlyZero() ? FVector2D(0.0, 1.0) : Direction.GetSafeNormal();
			const FVector2D Detected = From.GetValue() + Direction * (Slate.GetDragTriggerDistance() * 2.0 + 4.0);
			Move(InRoute, FromPath, MakePointerEvent(Detected, From.GetValue(), LeftButtonHeld(), EKeys::Invalid));
			if (!Slate.IsDragDropping())
			{
				Up(InRoute, FromPath, MakePointerEvent(Detected, Detected, TSet<FKey>(), EKeys::LeftMouseButton));
				OutWhyNot = FString::Printf(TEXT("Slate detected no drag from %s after a move of %.1f units"), *DreamSlatePanel::Describe(InFrom),
					(Detected - From.GetValue()).Size());
				return false;
			}

			// Over to the target, a step at a time, the last step on the point asked for -- its middle, where a tree row takes a
			// drop onto itself, unless the caller named another.
			FVector2D Last = Detected;
			const int32 Steps = FMath::Max(InSteps, 1);
			for (int32 Step = 1; Step <= Steps; ++Step)
			{
				const FVector2D Next = FMath::Lerp(Detected, To.GetValue(), static_cast<double>(Step) / Steps);
				Move(InRoute, ToPath, MakePointerEvent(Next, Last, LeftButtonHeld(), EKeys::Invalid));
				Last = Next;
			}
			Up(InRoute, ToPath, MakePointerEvent(To.GetValue(), To.GetValue(), TSet<FKey>(), EKeys::LeftMouseButton));
			if (Slate.IsDragDropping())
			{
				// Not left hanging for the next test: a drag and drop Slate still holds swallows every press after it.
				Slate.CancelDragDrop();
				OutWhyNot = TEXT("the release did not end the drag and drop; it was cancelled");
				return false;
			}
			return true;
		}

		void CollectInto(const TSharedRef<SWidget>& InWidget, TArray<TSharedRef<SWidget>>& OutWidgets)
		{
			OutWidgets.Add(InWidget);
			if (FChildren* Children = InWidget->GetChildren())
			{
				for (int32 Index = 0; Index < Children->Num(); ++Index)
				{
					CollectInto(Children->GetChildAt(Index), OutWidgets);
				}
			}
		}
	}

	void DreamSlatePanel::CollectDescendants(const TSharedRef<SWidget>& InRoot, TArray<TSharedRef<SWidget>>& OutWidgets)
	{
		DreamSlatePanelLocal::CollectInto(InRoot, OutWidgets);
	}

	TSharedPtr<SWidget> DreamSlatePanel::FindDescendant(const TSharedRef<SWidget>& InRoot, TFunctionRef<bool(const TSharedRef<SWidget>&)> InPredicate)
	{
		TArray<TSharedRef<SWidget>> All;
		CollectDescendants(InRoot, All);
		for (const TSharedRef<SWidget>& Widget : All)
		{
			if (InPredicate(Widget))
			{
				return Widget;
			}
		}
		return nullptr;
	}

	bool DreamSlatePanel::IsOfType(const SWidget& InWidget, const TCHAR* InTypeNamePart)
	{
		return InWidget.GetTypeAsString().Contains(InTypeNamePart);
	}

	TSharedPtr<SWidget> DreamSlatePanel::FindTextBlock(const TSharedRef<SWidget>& InRoot, const FString& InText)
	{
		static const FName TextBlockType(TEXT("STextBlock"));
		return FindDescendant(InRoot, [&InText](const TSharedRef<SWidget>& InWidget)
		{
			return InWidget->GetType() == TextBlockType
				&& StaticCastSharedRef<STextBlock>(InWidget)->GetText().ToString().Equals(InText, ESearchCase::CaseSensitive);
		});
	}

	TSharedPtr<SWidget> DreamSlatePanel::FindAncestorOfType(const TSharedRef<SWidget>& InFrom, const TCHAR* InTypeNamePart)
	{
		for (TSharedPtr<SWidget> Walk = InFrom; Walk.IsValid(); Walk = Walk->GetParentWidget())
		{
			if (IsOfType(*Walk, InTypeNamePart))
			{
				return Walk;
			}
		}
		return nullptr;
	}

	TOptional<FVector2D> DreamSlatePanel::CentreOf(const TSharedRef<SWidget>& InWidget)
	{
		// The geometry the widget was painted at, which is the one Slate's hit test grid holds it at.
		const FGeometry& Geometry = InWidget->GetPaintSpaceGeometry();
		const FVector2D LocalSize = Geometry.GetLocalSize();
		if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
		{
			return TOptional<FVector2D>();
		}
		return FVector2D(Geometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5, 0.5)));
	}

	FString DreamSlatePanel::Describe(const TSharedPtr<SWidget>& InWidget)
	{
		if (!InWidget.IsValid())
		{
			return TEXT("no widget");
		}
		const TOptional<FVector2D> Centre = CentreOf(InWidget.ToSharedRef());
		return Centre.IsSet()
			? FString::Printf(TEXT("%s at %s"), *InWidget->GetTypeAsString(), *Centre.GetValue().ToString())
			: FString::Printf(TEXT("%s (never arranged)"), *InWidget->GetTypeAsString());
	}

	bool DreamSlatePanel::DoesHitTestReach(const TSharedRef<SWidget>& InWidget, FString& OutWhatItFound)
	{
		const TOptional<FVector2D> Centre = CentreOf(InWidget);
		if (!Centre.IsSet())
		{
			OutWhatItFound = FString::Printf(TEXT("%s has no painted geometry to aim at"), *Describe(InWidget));
			return false;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		const FWidgetPath UnderPointer = Slate.LocateWindowUnderMouse(Centre.GetValue(), Slate.GetInteractiveTopLevelWindows(),
			/*bIgnoreEnabledStatus*/ false, static_cast<int32>(FSlateApplicationBase::CursorUserIndex));
		if (!UnderPointer.IsValid())
		{
			OutWhatItFound = FString::Printf(TEXT("no window takes input at %s (the one %s is in is not visible there, or not on screen)"),
				*Centre.GetValue().ToString(), *InWidget->GetTypeAsString());
			return false;
		}
		if (UnderPointer.ContainsWidget(&InWidget.Get()))
		{
			return true;
		}
		OutWhatItFound = FString::Printf(TEXT("at %s the hit test answers %s, a path that does not hold %s"),
			*Centre.GetValue().ToString(), *UnderPointer.GetLastWidget()->GetTypeAsString(), *InWidget->GetTypeAsString());
		return false;
	}

	EDreamSlateRoute DreamSlatePanel::ChooseRoute(TConstArrayView<TSharedRef<SWidget>> InWidgets, FString& OutWhy)
	{
		for (const TSharedRef<SWidget>& Widget : InWidgets)
		{
			FString WhatItFound;
			if (!DoesHitTestReach(Widget, WhatItFound))
			{
				OutWhy = FString::Printf(TEXT("along the widget's own path, because Slate's hit test does not reach it: %s"), *WhatItFound);
				return EDreamSlateRoute::WidgetPath;
			}
		}
		OutWhy = TEXT("through Slate's hit test, which reaches every widget the probe acts on");
		return EDreamSlateRoute::HitTest;
	}

	bool DreamSlatePanel::Click(const TSharedRef<SWidget>& InWidget, EDreamSlateRoute InRoute, FString& OutWhyNot)
	{
		using namespace DreamSlatePanelLocal;
		const TOptional<FVector2D> At = CentreOf(InWidget);
		if (!At.IsSet())
		{
			OutWhyNot = FString::Printf(TEXT("%s has no painted geometry to click"), *Describe(InWidget));
			return false;
		}
		FWidgetPath Path;
		// Down to what a hit test at the point would end on: the button inside a menu entry, not the entry's outer block.
		if (InRoute == EDreamSlateRoute::WidgetPath && !PathTo(DreamSlatePanelLocal::HitTargetAt(InWidget, At.GetValue()), Path, OutWhyNot))
		{
			return false;
		}
		// A move first, as a hand arrives before it presses: hover is what some widgets press from.
		Move(InRoute, Path, MakePointerEvent(At.GetValue(), At.GetValue(), TSet<FKey>(), EKeys::Invalid));
		Down(InRoute, Path, MakePointerEvent(At.GetValue(), At.GetValue(), LeftButtonHeld(), EKeys::LeftMouseButton));
		Up(InRoute, Path, MakePointerEvent(At.GetValue(), At.GetValue(), TSet<FKey>(), EKeys::LeftMouseButton));
		return true;
	}

	bool DreamSlatePanel::DragOnto(const TSharedRef<SWidget>& InFrom, const TSharedRef<SWidget>& InTo, EDreamSlateRoute InRoute, int32 InSteps, FString& OutWhyNot)
	{
		return DreamSlatePanelLocal::DragToPoint(InFrom, InTo, CentreOf(InTo), InRoute, InSteps, OutWhyNot);
	}

	bool DreamSlatePanel::DragOntoAt(const TSharedRef<SWidget>& InFrom, const TSharedRef<SWidget>& InTo, const FVector2D& InToLocalPoint,
		EDreamSlateRoute InRoute, int32 InSteps, FString& OutWhyNot)
	{
		return DreamSlatePanelLocal::DragToPoint(InFrom, InTo, PointIn(InTo, InToLocalPoint), InRoute, InSteps, OutWhyNot);
	}

	TOptional<FVector2D> DreamSlatePanel::PointIn(const TSharedRef<SWidget>& InWidget, const FVector2D& InLocalPoint)
	{
		const FGeometry& Geometry = InWidget->GetPaintSpaceGeometry();
		const FVector2D LocalSize = Geometry.GetLocalSize();
		if (LocalSize.X <= 0.0 || LocalSize.Y <= 0.0)
		{
			return TOptional<FVector2D>();
		}
		return FVector2D(Geometry.LocalToAbsolute(InLocalPoint));
	}

	bool DreamSlatePanel::FocusAsKeyboardUser(const TSharedRef<SWidget>& InWidget, FString& OutWhyNot)
	{
		FSlateApplication::Get().SetKeyboardFocus(InWidget, EFocusCause::Navigation);
		if (!GetKeyboardFocus().IsValid())
		{
			OutWhyNot = FString::Printf(TEXT("Slate gave the keyboard focus to nothing when %s was asked to take it"), *Describe(InWidget));
			return false;
		}
		return true;
	}

	bool DreamSlatePanel::TypeCharacters(const FString& InText, FString& OutWhyNot)
	{
		if (!GetKeyboardFocus().IsValid())
		{
			OutWhyNot = TEXT("nothing has the keyboard focus to type into");
			return false;
		}
		for (const TCHAR Character : InText)
		{
			FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Character, FModifierKeysState(), FSlateApplicationBase::CursorUserIndex, /*bInIsRepeat*/ false));
		}
		return true;
	}

	bool DreamSlatePanel::PressKey(const FKey& InKey, const FModifierKeysState& InModifiers, FString& OutWhyNot)
	{
		if (!InKey.IsValid())
		{
			OutWhyNot = TEXT("the key is not a valid key");
			return false;
		}
		uint32 KeyCode = 0;
		uint32 CharacterCode = 0;
		DreamDriverKeys::GetKeyCodes(InKey, KeyCode, CharacterCode);
		const FKeyEvent Event(InKey, InModifiers, FSlateApplicationBase::CursorUserIndex, /*bInIsRepeat*/ false, CharacterCode, KeyCode);
		FSlateApplication::Get().ProcessKeyDownEvent(Event);
		FSlateApplication::Get().ProcessKeyUpEvent(Event);
		return true;
	}

	bool DreamSlatePanel::PressChord(const FKey& InKey, EDreamDriverModifierKeys InModifiers, FString& OutWhyNot)
	{
		if (!InKey.IsValid())
		{
			OutWhyNot = TEXT("the key is not a valid key");
			return false;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		TArray<FKey> Modifiers;
		DreamDriverKeys::GetModifierKeys(InModifiers, Modifiers);
		TArray<FKey> Held;
		const auto Send = [&Slate](const FKey& InSent, const TArray<FKey>& InHeld, bool bInDown)
		{
			uint32 KeyCode = 0;
			uint32 CharacterCode = 0;
			DreamDriverKeys::GetKeyCodes(InSent, KeyCode, CharacterCode);
			const FKeyEvent Event(InSent, DreamDriverKeys::MakeModifierKeysState(InHeld), FSlateApplicationBase::CursorUserIndex,
				/*bInIsRepeat*/ false, CharacterCode, KeyCode);
			if (bInDown)
			{
				Slate.ProcessKeyDownEvent(Event);
			}
			else
			{
				Slate.ProcessKeyUpEvent(Event);
			}
		};
		for (const FKey& Modifier : Modifiers)
		{
			// Held before its own event, as the platform reports a modifier's key-down with the modifier already down.
			Held.Add(Modifier);
			Send(Modifier, Held, true);
		}
		Send(InKey, Held, true);
		Send(InKey, Held, false);
		while (Held.Num() > 0)
		{
			const FKey Modifier = Held.Pop();
			Send(Modifier, Held, false);
		}
		return true;
	}

	void DreamSlatePanel::CollectWindows(TArray<TSharedRef<SWindow>>& OutWindows)
	{
		OutWindows.Reset();
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}
		TArray<TSharedRef<SWindow>> Pending = FSlateApplication::Get().GetTopLevelWindows();
		while (Pending.Num() > 0)
		{
			const TSharedRef<SWindow> Window = Pending.Pop();
			OutWindows.Add(Window);
			Pending.Append(Window->GetChildWindows());
		}
	}

	TSharedPtr<SWidget> DreamSlatePanel::FindInAnyWindow(TFunctionRef<bool(const TSharedRef<SWidget>&)> InPredicate)
	{
		TArray<TSharedRef<SWindow>> Windows;
		CollectWindows(Windows);
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			if (const TSharedPtr<SWidget> Found = FindDescendant(Window, InPredicate))
			{
				return Found;
			}
		}
		return nullptr;
	}

	TSharedPtr<SWidget> DreamSlatePanel::GetKeyboardFocus()
	{
		return FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
	}
}
