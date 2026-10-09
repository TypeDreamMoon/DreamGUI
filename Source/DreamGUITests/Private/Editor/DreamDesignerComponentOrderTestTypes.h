// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamDesignerComponentOrderTestTypes.generated.h"

/** Real initialization code may legally reorder the live components without changing the asset. */
UCLASS(Blueprintable)
class UDreamDesignerComponentOrderTestWidget : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	virtual void NativeOnInitialized() override
	{
		Super::NativeOnInitialized();
		if (UDreamWidgetTree* Tree = GetWidgetTree())
		{
			if (UDreamWidget* Subject = Tree->FindWidgetByVariableName(TEXT("Subject")))
			{
				if (Subject->GetAllComponents().Num() > 1)
				{
					Subject->MoveComponentToIndex(Subject->GetAllComponents().Last(), 0);
				}
			}
		}
	}
};
