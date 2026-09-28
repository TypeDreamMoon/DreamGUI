// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUITextInputTarget.h"

namespace DreamUITextInputRouterLocal
{
	/** Weak, as the field's own record of it always was: a field destroyed mid-edit owns nothing. */
	TWeakObjectPtr<UObject> ActiveTarget;
}

void DreamUITextInputRouter::SetActiveTarget(UObject* InTarget)
{
	if (InTarget != nullptr && !ensureMsgf(InTarget->Implements<UDreamUITextInputTarget>(),
		TEXT("%s claimed the keyboard but does not take text input"), *InTarget->GetPathName()))
	{
		return;
	}
	DreamUITextInputRouterLocal::ActiveTarget = InTarget;
}

void DreamUITextInputRouter::ClearActiveTarget(const UObject* InTarget)
{
	if (DreamUITextInputRouterLocal::ActiveTarget.Get() == InTarget)
	{
		DreamUITextInputRouterLocal::ActiveTarget.Reset();
	}
}

IDreamUITextInputTarget* DreamUITextInputRouter::GetActiveTarget()
{
	return Cast<IDreamUITextInputTarget>(DreamUITextInputRouterLocal::ActiveTarget.Get());
}

bool DreamUITextInputRouter::RouteCharacter(TCHAR InCharacter)
{
	if (IDreamUITextInputTarget* Target = GetActiveTarget())
	{
		return Target->InsertTextCharacter(InCharacter);
	}
	return false;
}
