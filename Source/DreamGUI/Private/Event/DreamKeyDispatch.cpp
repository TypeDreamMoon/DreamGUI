// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/Interface/DreamKeyInterface.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"

namespace DreamUIKeyDispatch
{
	namespace
	{
		void CallChannel(UObject* InTarget, UDreamKeyEventData* InEventData)
		{
			switch (InEventData->KeyEventType)
			{
			case EDreamUIKeyEventType::KeyDown:
				IDreamKeyInterface::Execute_OnKeyDown(InTarget, InEventData);
				break;
			case EDreamUIKeyEventType::KeyUp:
				IDreamKeyInterface::Execute_OnKeyUp(InTarget, InEventData);
				break;
			case EDreamUIKeyEventType::KeyChar:
				IDreamKeyInterface::Execute_OnKeyChar(InTarget, InEventData);
				break;
			case EDreamUIKeyEventType::AnalogValueChanged:
				IDreamKeyInterface::Execute_OnAnalogValueChanged(InTarget, InEventData);
				break;
			}
		}
	}

	bool DispatchToWidget(UDreamWidget* InWidget, UDreamKeyEventData* InEventData)
	{
		if (!IsValid(InWidget) || InEventData == nullptr || InEventData->bHandled)
		{
			return InEventData != nullptr && InEventData->bHandled;
		}
		// A snapshot, because a handler is free to destroy the behaviour it is on -- a key that closes
		// the dialog it was typed into is the ordinary case, not the exotic one.
		const TArray<UDreamUIBehaviour*> Components = InWidget->GetAllComponents();
		for (UDreamUIBehaviour* Component : Components)
		{
			if (!IsValid(Component) || !Component->GetClass()->ImplementsInterface(UDreamKeyInterface::StaticClass()))
			{
				continue;
			}
			CallChannel(Component, InEventData);
			if (InEventData->bHandled)
			{
				return true;
			}
		}
		return false;
	}

	bool DispatchBubbling(UDreamWidget* InFocusedWidget, UDreamKeyEventData* InEventData)
	{
		if (InEventData == nullptr)
		{
			return false;
		}
		InEventData->FocusedWidget = InFocusedWidget;
		// A depth guard for the same reason every other walk here has one: a malformed parent chain
		// must fail visibly rather than hang the input thread.
		int32 DepthGuard = 0;
		for (UDreamWidget* Widget = InFocusedWidget; IsValid(Widget) && DepthGuard < 256; Widget = Widget->GetParent(), ++DepthGuard)
		{
			if (DispatchToWidget(Widget, InEventData))
			{
				return true;
			}
		}
		return false;
	}
}
