// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUIBehaviour.h"
#include "DreamWidgetLifecycleTestTypes.generated.h"

class UDreamWidget;

UCLASS()
class UDreamWidgetHierarchyMutationBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	void Configure(UDreamWidget* InWidgetToDetach, UDreamWidget* InWidgetToAttach, UDreamWidget* InExternalParent)
	{
		WidgetToDetach = InWidgetToDetach;
		WidgetToAttach = InWidgetToAttach;
		ExternalParent = InExternalParent;
	}

protected:
	virtual void OnUnregister() override;

private:
	UPROPERTY()
	TObjectPtr<UDreamWidget> WidgetToDetach;

	UPROPERTY()
	TObjectPtr<UDreamWidget> WidgetToAttach;

	UPROPERTY()
	TObjectPtr<UDreamWidget> ExternalParent;
};
