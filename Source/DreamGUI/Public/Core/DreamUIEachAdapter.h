// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamWidgetEachBinding.h"
#include "Interaction/UIRecyclableScrollView.h"
#include "DreamUIEachAdapter.generated.h"

class UDreamUserWidget;
class UDreamWidget;

/**
 * The data source an `each` block becomes: it feeds the host's recyclable view its item count and,
 * per cell, applies the block's item bindings -- read the member off the item object, push it
 * through the target's setter, addressed inside the cell's cloned subtree by display name.
 *
 * Owned by the user widget that resolved the binding; one adapter per `each`. Refresh() re-reads
 * the source (function call or array property, both by reflection) and tells the view; the OWNER
 * wires a FieldNotify subscription to call it when the source is a variable that broadcasts.
 */
UCLASS()
class DREAMGUI_API UDreamUIEachAdapter : public UObject, public IUIRecyclableScrollViewDataSource
{
	GENERATED_BODY()

public:
	void Initialize(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding, UUIRecyclableScrollView* InView);

	/** Re-read the source and update the view. Safe to call any time after Initialize. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Each")
	void Refresh();

	const FDreamWidgetEachBinding& GetBinding() const { return Binding; }

	virtual int32 GetItemCount_Implementation() override { return Items.Num(); }
	virtual void InitOnCreate_Implementation(UDreamUIBehaviour* Component) override {}
	virtual void BeforeSetCell_Implementation() override {}
	virtual void SetCell_Implementation(UDreamUIBehaviour* Component, int32 Index) override;
	virtual void AfterSetCell_Implementation() override {}

private:
	void FetchItems();

	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> Owner;

	UPROPERTY(Transient)
	TObjectPtr<UUIRecyclableScrollView> View;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> Items;

	FDreamWidgetEachBinding Binding;
	bool bViewInitialized = false;
};
