// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/Components/DreamWidget.h"
#include "UObject/GarbageCollection.h"
#include "DreamMenuAnchorOperationReentryTestTypes.generated.h"

UCLASS()
class UDreamMenuAnchorOperationReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamMenuAnchor> Anchor = nullptr;
	UPROPERTY()
	TObjectPtr<UDreamWidget> FirstContent = nullptr;
	UPROPERTY()
	TObjectPtr<UDreamWidget> NestedContent = nullptr;
	bool bDestroyInProvider = false;
	bool bCollectedDestroyedAnchor = false;
	bool bReopenWhenClosed = false;
	int32 ReopenAttempts = 0;
	int32 ProviderCalls = 0;
	TArray<bool> OpenChanges;

	UFUNCTION()
	UDreamWidget* ProvideContent()
	{
		++ProviderCalls;
		if (ProviderCalls == 1)
		{
			if (bDestroyInProvider)
			{
				const TWeakObjectPtr<UDreamMenuAnchor> DestroyedAnchor(Anchor.Get());
				Anchor->DestroyWidget();
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);
				bCollectedDestroyedAnchor = DestroyedAnchor.GetEvenIfUnreachable() == nullptr;
				return nullptr;
			}
			Anchor->Close();
			Anchor->Open(false);
			return FirstContent.Get();
		}
		return NestedContent.Get();
	}

	UFUNCTION()
	void RecordOpenChanged(bool bInOpen)
	{
		OpenChanges.Add(bInOpen);
		if (!bInOpen && bReopenWhenClosed)
		{
			++ReopenAttempts;
			Anchor->Open(false);
		}
	}
};
