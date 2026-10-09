// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/Components/DreamWidget.h"
#include "DreamMenuAnchorProviderReentryTestTypes.generated.h"

/** A real dynamic content provider that cancels its first open through the public API. */
UCLASS()
class UDreamMenuAnchorProviderReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamMenuAnchor> Anchor = nullptr;

	UPROPERTY()
	TObjectPtr<UDreamWidget> Content = nullptr;

	bool bReturnContentOnFirstCall = false;
	int32 ProviderCalls = 0;
	int32 CloseCalls = 0;
	TArray<bool> OpenChanges;

	UFUNCTION()
	UDreamWidget* ProvideContent()
	{
		++ProviderCalls;
		if (ProviderCalls == 1)
		{
			++CloseCalls;
			Anchor->Close();
			return bReturnContentOnFirstCall ? Content.Get() : nullptr;
		}
		return Content.Get();
	}

	UFUNCTION()
	void RecordOpenChanged(bool bInOpen) { OpenChanges.Add(bInOpen); }
};
