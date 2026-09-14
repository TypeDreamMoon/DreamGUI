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
	// GetWidget() answers null whenever GetTypedOuter finds no widget -- a sub-object built outside a
	// widget, or one whose outer chain is already coming apart. This function is what the logging and
	// error paths call to name the thing they are complaining about, so it is asked precisely when the
	// hierarchy is in that state, and it dereferenced the answer unguarded.
	const UDreamWidget* Widget = GetWidget();
	return IsValid(Widget)
		? Widget->GetPathDisplayName(StopOuter) / this->GetName()
		: FString::Printf(TEXT("<orphan>/%s"), *this->GetName());
}

