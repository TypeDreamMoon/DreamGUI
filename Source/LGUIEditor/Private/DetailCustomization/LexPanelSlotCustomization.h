// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class ULexPanelSlot;
class ULexLayoutContainer;
class IDetailCategoryBuilder;

class FLexPanelSlotCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	static void AddSlotProperties(IDetailCategoryBuilder& Category, const TArray<UObject*>& SlotObjects, const ULexLayoutContainer* ParentLayout);
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TArray<TWeakObjectPtr<ULexPanelSlot>> TargetSlots;
};
