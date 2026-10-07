// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Interaction/DreamUITooltip.h"
#include "DreamTooltipTeardownReentryTestTypes.generated.h"

/** Runs the same lifetime callback available to an authored custom tooltip. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTooltipTeardownReentryFixture : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	TFunction<void()> DestructCallback;
	inline static TFunction<void()> ConstructCallback;
	inline static TFunction<void(UDreamTooltipTeardownReentryFixture*)> InitializedCallback;

	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();
		TFunction<void(UDreamTooltipTeardownReentryFixture*)> Callback = MoveTemp(InitializedCallback);
		if (Callback)Callback(this);
	}

	virtual void NativeOnConstruct() override
	{
		Super::NativeOnConstruct();
		TFunction<void()> Callback = MoveTemp(ConstructCallback);
		if (Callback)Callback();
	}

	virtual void NativeOnDestruct() override
	{
		Super::NativeOnDestruct();
		TFunction<void()> Callback = MoveTemp(DestructCallback);
		if (Callback)Callback();
	}
};

/** The public tooltip-provider interface can also cancel an in-flight show. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTooltipSourceReentryFixture : public UDreamUIBehaviour, public IDreamUITooltipSourceInterface
{
	GENERATED_BODY()

public:
	TFunction<void()> ProviderCallback;

	virtual TSubclassOf<UDreamUserWidget> GetTooltipWidgetClass_Implementation() override
	{
		TFunction<void()> Callback = MoveTemp(ProviderCallback);
		if (Callback)Callback();
		return nullptr;
	}
};
