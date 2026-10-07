// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "DreamScreenReentryTestTypes.generated.h"

class UDreamScreenUISubsystem;

/** Mutates a page's registration from the public dynamic lifecycle delegates. */
UCLASS()
class UDreamScreenReentryProbe : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY() TObjectPtr<UDreamScreenUISubsystem> Screen;
	UPROPERTY() TObjectPtr<UDreamWidget> Replacement;
	FName PageName;
	bool bArmed = false;
	int32 ShownCount = 0;
	int32 HiddenCount = 0;

	UFUNCTION() void RemoveOnCollapsed(EDreamWidgetVisibility Visibility);
	UFUNCTION() void ReplaceOnCreated(FName Name, UDreamWidget* Page);
	UFUNCTION() void CountShown(FName Name, UDreamWidget* Page);
	UFUNCTION() void CountHidden(FName Name, UDreamWidget* Page);
};

/** Gives the fixture the same visibility binding access as a widget's Blueprint. */
UCLASS()
class UDreamScreenReentryWidget : public UDreamUserWidget
{
	GENERATED_BODY()
public:
	void BindVisibilityProbe(UDreamScreenReentryProbe* Probe)
	{
		OnVisibilityChanged.AddDynamic(Probe, &UDreamScreenReentryProbe::RemoveOnCollapsed);
	}
};
