// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamScreenReentryTestTypes.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"

void UDreamScreenReentryProbe::RemoveOnCollapsed(EDreamWidgetVisibility Visibility)
{
	if (bArmed && Visibility == EDreamWidgetVisibility::Collapsed)
	{
		bArmed = false;
		Screen->RemoveUI(PageName);
	}
}

void UDreamScreenReentryProbe::ReplaceOnCreated(FName Name, UDreamWidget* Page)
{
	if (bArmed && Name == PageName)
	{
		bArmed = false;
		Screen->RemoveUI(PageName);
		Replacement = Screen->ShowWidgetOfClass(PageName, UDreamUserWidget::StaticClass());
	}
}

void UDreamScreenReentryProbe::CountShown(FName Name, UDreamWidget* Page)
{
	if (Name == PageName) { ++ShownCount; }
}

void UDreamScreenReentryProbe::CountHidden(FName Name, UDreamWidget* Page)
{
	if (Name == PageName) { ++HiddenCount; }
}
