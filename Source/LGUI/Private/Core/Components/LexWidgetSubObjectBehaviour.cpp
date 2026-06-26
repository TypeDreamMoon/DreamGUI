// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexWidgetSubObjectBehaviour.h"
#include "Core/Components/LexWidget.h"


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

void ULexWidgetSubObjectBehaviour::PostInitProperties()
{
	UObject::PostInitProperties();
}

ULexWidget* ULexWidgetSubObjectBehaviour::GetWidget() const
{
	if (!IsValid(OwnerWidget))
	{
		OwnerWidget = this->GetTypedOuter<ULexWidget>();
	}
	return OwnerWidget;
}

FString ULexWidgetSubObjectBehaviour::GetPathDisplayName(const UObject* StopOuter) const
{
	return GetWidget()->GetPathDisplayName(StopOuter) / this->GetName();
}

