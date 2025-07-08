// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexWidgetSubObjectBehaviour.h"

#include "Core/Components/LexWidget.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

ULexWidget* ULexWidgetSubObjectBehaviour::GetWidget() const
{
	if (!CacheWidget.IsValid())
	{
		CacheWidget = Cast<ULexWidget>(this->GetOuter());
	}
	return CacheWidget.Get();
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif