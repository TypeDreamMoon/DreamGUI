// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexWidgetSubObjectBehaviour.h"
#include "Core/Components/LexWidget.h"
#include "LGUI.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

void ULexWidgetSubObjectBehaviour::Call_OnRegister()
{
	if (!bIsRegistered)
	{
		bIsRegistered = true;
		OnRegister();
	}
}

void ULexWidgetSubObjectBehaviour::Call_OnUnregister()
{
	if (bIsRegistered)
	{
		bIsRegistered = false;
		OnUnregister();
	}
}

ULexWidget* ULexWidgetSubObjectBehaviour::GetWidget() const
{
	if (!CacheWidget.IsValid())
	{
		CacheWidget = this->GetTypedOuter<ULexWidget>();
	}
	return CacheWidget.Get();
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif