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
				Slate.RoutePointerMoveEvent(InPath, InEvent, /*bIsSynthetic*/ false);
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
				Slate.RoutePointerDownEvent(InPath, InEvent);
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
				Slate.RoutePointerUpEvent(InPath, InEvent);
			}
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
		if (InRoute == EDreamSlateRoute::WidgetPath && !PathTo(InWidget, Path, OutWhyNot))
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
		using namespace DreamSlatePanelLocal;
		FSlateApplication& Slate = FSlateApplication::Get();
		const TOptional<FVector2D> From = CentreOf(InFrom);
		const TOptional<FVector2D> To = CentreOf(InTo);
		if (!From.IsSet() || !To.IsSet())
		{
			OutWhyNot = FString::Printf(TEXT("the drag needs both ends painted: from %s, to %s"), *Describe(InFrom), *Describe(InTo));
			return false;
		}
		FWidgetPath FromPath;
		FWidgetPath ToPath;
		if (InRoute == EDreamSlateRoute::WidgetPath && (!PathTo(InFrom, FromPath, OutWhyNot) || !PathTo(InTo, ToPath, OutWhyNot)))
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
			OutWhyNot = FString::Printf(TEXT("Slate detected no drag from %s after a move of %.1f units"), *Describe(InFrom),
				(Detected - From.GetValue()).Size());
			return false;
		}

		// Over to the target, a step at a time, the last step on its middle -- where a tree row takes a drop onto itself.
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

	TSharedPtr<SWidget> DreamSlatePanel::GetKeyboardFocus()
	{
		return FSlateApplication::IsInitialized() ? FSlateApplication::Get().GetKeyboardFocusedWidget() : nullptr;
	}
}
