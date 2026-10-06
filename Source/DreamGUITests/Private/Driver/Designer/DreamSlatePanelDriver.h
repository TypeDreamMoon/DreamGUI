// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericApplication.h"   // FModifierKeysState
#include "InputCoreTypes.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

#include "Driver/DreamDriverTypes.h"   // EDreamDriverModifierKeys

class SWidget;
class SWindow;

namespace DreamTests
{
	/**
	 * How input is put into an editor panel -- the designer's hierarchy tree, its details panel, its toolbar -- that is
	 * plain Slate rather than a viewport.
	 *
	 * Both roads go through FSlateApplication's own input functions, the ones its platform message handlers call, so
	 * Slate does everything it does for a hand: it routes the event, runs the replies (capture, focus, drag detection,
	 * drag and drop), and keeps its own pressed-button and focus state. They differ in how Slate is told where the
	 * pointer is.
	 */
	enum class EDreamSlateRoute : uint8
	{
		/**
		 * FSlateApplication::ProcessMouseMoveEvent, ProcessMouseButtonDownEvent and ProcessMouseButtonUpEvent at the
		 * widget's screen position: Slate finds the window under it and hit-tests that window's last painted frame
		 * (LocateWindowUnderMouse), exactly as for a platform message. Only reaches a widget in a window that is
		 * visible, takes input and has been painted with the widget where it is now.
		 */
		HitTest,
		/**
		 * FSlateApplication::RoutePointerMoveEvent, RoutePointerDownEvent and RoutePointerUpEvent along the path to the
		 * widget (FindPathToWidget), carried on down to the leafmost widget under the point that takes hits, which is
		 * where Slate's hit test would have ended: the same routing and replies, the hit test's window search left out.
		 * What the probe falls back to when the hit test does not find the widget -- an editor rendering off screen
		 * keeps its windows out of it.
		 */
		WidgetPath,
	};

	/**
	 * Finding things in a Slate panel and putting a hand's input into it. For probes and tests of the editor's own
	 * panels; the designer viewport has its own driver (FDreamDesignerDriver), which goes through FSceneViewport.
	 *
	 * Every pointer event is the cursor user's, Slate user 0, pointer FSlateApplicationBase::CursorPointerIndex; every
	 * key and character goes to that user's keyboard focus. Nothing here moves the desk's cursor -- but a widget a click
	 * reaches can: SSpinBox's press asks for high-precision mouse movement and its release puts the cursor back where it
	 * was (FReply::SetMousePos), and Slate does both on the platform cursor. Enter a spin box by keyboard
	 * (FocusAsKeyboardUser, then keys) rather than by clicking it.
	 */
	namespace DreamSlatePanel
	{
		/** Every widget under InRoot, InRoot first, depth first through every child slot whether or not it is shown. */
		void CollectDescendants(const TSharedRef<SWidget>& InRoot, TArray<TSharedRef<SWidget>>& OutWidgets);
		/** The first widget under InRoot (InRoot included) InPredicate accepts, depth first. */
		TSharedPtr<SWidget> FindDescendant(const TSharedRef<SWidget>& InRoot, TFunctionRef<bool(const TSharedRef<SWidget>&)> InPredicate);
		/** Whether InWidget's type, as SNew named it (SWidget::GetTypeAsString), contains InTypeNamePart. */
		bool IsOfType(const SWidget& InWidget, const TCHAR* InTypeNamePart);
		/** The first STextBlock under InRoot showing exactly InText. */
		TSharedPtr<SWidget> FindTextBlock(const TSharedRef<SWidget>& InRoot, const FString& InText);
		/** InFrom or the nearest of its parents whose type contains InTypeNamePart. */
		TSharedPtr<SWidget> FindAncestorOfType(const TSharedRef<SWidget>& InFrom, const TCHAR* InTypeNamePart);

		/** InWidget's middle in absolute (screen) Slate units, as it was last arranged; unset for a widget never arranged. */
		TOptional<FVector2D> CentreOf(const TSharedRef<SWidget>& InWidget);
		/**
		 * Whether Slate's hit test at InWidget's middle reaches InWidget: LocateWindowUnderMouse over the interactive
		 * top-level windows, and InWidget on the path it answers. OutWhatItFound says what it found otherwise.
		 */
		bool DoesHitTestReach(const TSharedRef<SWidget>& InWidget, FString& OutWhatItFound);
		/**
		 * HitTest when the hit test reaches every one of InWidgets, else WidgetPath; OutWhy says which, and what the hit
		 * test found where it fell short, for the probe's report.
		 */
		EDreamSlateRoute ChooseRoute(TConstArrayView<TSharedRef<SWidget>> InWidgets, FString& OutWhy);

		/** The left button pressed and let go on InWidget's middle, after a move there, by InRoute. */
		bool Click(const TSharedRef<SWidget>& InWidget, EDreamSlateRoute InRoute, FString& OutWhyNot);
		/**
		 * The left button pressed on InFrom's middle, carried past Slate's drag trigger distance and on to InTo's middle
		 * in InSteps moves, and let go there, by InRoute: what drags a tree row onto another. Under WidgetPath the moves
		 * that detect the drag are routed along InFrom's path, and the rest -- the drag over its target -- along InTo's.
		 */
		bool DragOnto(const TSharedRef<SWidget>& InFrom, const TSharedRef<SWidget>& InTo, EDreamSlateRoute InRoute, int32 InSteps, FString& OutWhyNot);
		/**
		 * DragOnto, letting go InToLocalPoint into InTo -- Slate units from its top-left corner, as it was painted -- rather
		 * than on its middle. A tree row decides where a drop goes by where the pointer is in it
		 * (STableRow::ZoneFromPointerPosition): its top few units are above it, its bottom few below, the rest onto it.
		 */
		bool DragOntoAt(const TSharedRef<SWidget>& InFrom, const TSharedRef<SWidget>& InTo, const FVector2D& InToLocalPoint,
			EDreamSlateRoute InRoute, int32 InSteps, FString& OutWhyNot);
		/** InLocalPoint into InWidget -- Slate units from its top-left, as it was painted -- in absolute units; unset for a widget never arranged. */
		TOptional<FVector2D> PointIn(const TSharedRef<SWidget>& InWidget, const FVector2D& InLocalPoint);

		/**
		 * Give InWidget the keyboard focus the way a keyboard user's Tab does (EFocusCause::Navigation) -- what a spin box
		 * answers by entering its text mode (SSpinBox::OnFocusReceived). False when Slate gave the focus to nothing.
		 */
		bool FocusAsKeyboardUser(const TSharedRef<SWidget>& InWidget, FString& OutWhyNot);
		/** Each character of InText as FSlateApplication::ProcessKeyCharEvent, to the cursor user's keyboard focus. */
		bool TypeCharacters(const FString& InText, FString& OutWhyNot);
		/** InKey down and up as FSlateApplication::ProcessKeyDownEvent and ProcessKeyUpEvent, with InModifiers held. */
		bool PressKey(const FKey& InKey, const FModifierKeysState& InModifiers, FString& OutWhyNot);
		/**
		 * A chord the way a keyboard sends one: InModifiers' keys down one after another (Shift, Ctrl, Alt, Cmd), each a
		 * key-down of its own carrying the keys held so far; then InKey down and up with all of them held; then the
		 * modifiers up in reverse. Everything through FSlateApplication::ProcessKeyDownEvent and ProcessKeyUpEvent, to the
		 * cursor user's keyboard focus, with the platform's key and character codes.
		 */
		bool PressChord(const FKey& InKey, EDreamDriverModifierKeys InModifiers, FString& OutWhyNot);

		/**
		 * Every window Slate has, the top-level ones and every window under them -- a menu opened as a window of its own
		 * among them (FMenuStack's CreateNewWindow), which no search from the editor's own window reaches.
		 */
		void CollectWindows(TArray<TSharedRef<SWindow>>& OutWindows);
		/** The first widget InPredicate accepts in any of those windows, searched one window at a time, depth first. */
		TSharedPtr<SWidget> FindInAnyWindow(TFunctionRef<bool(const TSharedRef<SWidget>&)> InPredicate);
		/** The cursor user's keyboard focus, or null. */
		TSharedPtr<SWidget> GetKeyboardFocus();
		/** "SSpinBox<NumericType> at (x, y)", for a report. */
		FString Describe(const TSharedPtr<SWidget>& InWidget);
	}
}
