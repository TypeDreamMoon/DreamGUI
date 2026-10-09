// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamTabView.h"
#include "DreamTabViewMutationReentryTestTypes.generated.h"

/** Mutates the real tab view once from the public close or generation hook. */
UCLASS()
class UDreamTabViewMutationReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UDreamTabView> View = nullptr;

	bool bCloseAgain = false;
	int32 MutationCount = 0;
	int32 GeneratedCount = 0;
	int32 ValueNotificationCount = 0;
	int32 PublishedIndex = 0;
	int32 InitializedContentCount = 0;
	TWeakObjectPtr<UDreamWidget> InitialContent;
	TWeakObjectPtr<UDreamWidget> AdoptionParent;

	void HandleTemplateInitialized(UDreamUserWidget* InContent)
	{
		++InitializedContentCount;
		if (MutationCount != 0)return;
		++MutationCount;
		InitialContent = InContent;
		if (UDreamWidget* Parent = AdoptionParent.Get())InContent->SetParentBeforeRegister(Parent);
		View->SetTabLabels({FText::AsCultureInvariant(TEXT("New A")), FText::AsCultureInvariant(TEXT("New B")), FText::AsCultureInvariant(TEXT("New C"))});
	}

	UFUNCTION()
	void HandleTabClosed(int32 InIndex)
	{
		if (MutationCount != 0)return;
		++MutationCount;
		if (bCloseAgain)View->CloseTab(InIndex);
		else View->MoveTab(InIndex, 2);
	}

	UFUNCTION()
	void HandleTabGenerated(int32 InIndex, UDreamWidget* InTab)
	{
		++GeneratedCount;
		if (MutationCount != 0)return;
		++MutationCount;
		View->SetTabLabels({FText::AsCultureInvariant(TEXT("New A")), FText::AsCultureInvariant(TEXT("New B")), FText::AsCultureInvariant(TEXT("New C"))});
	}

	UFUNCTION()
	void HandleTabChanged(int32 InIndex)
	{
		if (MutationCount != 0)return;
		++MutationCount;
		PublishedIndex = 2;
		View->SetActiveTabIndexWithoutNotify(2);
	}

	UFUNCTION()
	void HandleValueChanged(int32 InIndex)
	{
		++ValueNotificationCount;
		PublishedIndex = InIndex;
	}
};

/** Uses the class factory's real initialization boundary before the tab content is attached. */
UCLASS()
class UDreamTabViewInitializedReentryTemplate : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	inline static TWeakObjectPtr<UDreamTabViewMutationReentryProbe> ActiveProbe;

	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();
		if (UDreamTabViewMutationReentryProbe* Probe = ActiveProbe.Get())Probe->HandleTemplateInitialized(this);
	}
};
