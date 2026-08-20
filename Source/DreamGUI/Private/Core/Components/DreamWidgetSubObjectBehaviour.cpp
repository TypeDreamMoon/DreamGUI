// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamWidgetSubObjectBehaviour.h"
#include "Core/Components/DreamWidget.h"


void UDreamWidgetSubObjectBehaviour::Call_OnRegister()
{
	if (!bIsRegistered)
	{
		bIsRegistered = true;
		OnRegister();
	}
}

void UDreamWidgetSubObjectBehaviour::Call_OnUnregister()
{
	if (bIsRegistered)
	{
		bIsRegistered = false;
		OnUnregister();
	}
}

void UDreamWidgetSubObjectBehaviour::PostInitProperties()
{
	UObject::PostInitProperties();
}

UDreamWidget* UDreamWidgetSubObjectBehaviour::GetWidget() const
{
	if (!IsValid(OwnerWidget))
	{
		OwnerWidget = this->GetTypedOuter<UDreamWidget>();
	}
	return OwnerWidget;
}

FString UDreamWidgetSubObjectBehaviour::GetPathDisplayName(const UObject* StopOuter) const
{
	return GetWidget()->GetPathDisplayName(StopOuter) / this->GetName();
}

