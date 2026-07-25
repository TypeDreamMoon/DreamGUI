#pragma once

#include "Core/LexUIBehaviour.h"
#include "LexWidgetLifecycleTestTypes.generated.h"

class ULexWidget;

UCLASS()
class ULexWidgetHierarchyMutationBehaviour : public ULexUIBehaviour
{
	GENERATED_BODY()

public:
	void Configure(ULexWidget* InWidgetToDetach, ULexWidget* InWidgetToAttach, ULexWidget* InExternalParent)
	{
		WidgetToDetach = InWidgetToDetach;
		WidgetToAttach = InWidgetToAttach;
		ExternalParent = InExternalParent;
	}

protected:
	virtual void OnUnregister() override;

private:
	UPROPERTY()
	TObjectPtr<ULexWidget> WidgetToDetach;

	UPROPERTY()
	TObjectPtr<ULexWidget> WidgetToAttach;

	UPROPERTY()
	TObjectPtr<ULexWidget> ExternalParent;
};
