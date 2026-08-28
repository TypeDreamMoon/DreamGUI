// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "DreamWidgetBlueprintFactory.generated.h"

/**
 * Creates a UDreamWidgetBlueprint from the content browser.
 *
 * Two pickers, matching UMG's: the parent class (any UDreamUserWidget, so a native or Blueprint base
 * carrying logic and bindings can be derived from) and the root layout container the fresh hierarchy
 * starts with. The root widget itself is always plain -- what varies is the panel on it.
 */
UCLASS()
class UDreamWidgetBlueprintFactory : public UFactory
{
	GENERATED_BODY()

public:
	UDreamWidgetBlueprintFactory();

	// UFactory
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual uint32 GetMenuCategories() const override;
	// End UFactory

	/** Parent class of the created Blueprint. Defaults to UDreamUserWidget when the picker is skipped. */
	UPROPERTY(Transient)
	TObjectPtr<UClass> ParentClass = nullptr;

private:
	UPROPERTY(Transient)
	TObjectPtr<UClass> RootLayoutClass = nullptr;
};
