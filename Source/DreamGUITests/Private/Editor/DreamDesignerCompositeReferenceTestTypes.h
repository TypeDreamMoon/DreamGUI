// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamWidget.h"
#include "DreamDesignerCompositeReferenceTestTypes.generated.h"

/** A normal editable value: changing Count must preserve the two unedited references. */
USTRUCT(BlueprintType)
struct FDreamDesignerCompositeReferenceTestValue
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUITest")
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUITest")
	TObjectPtr<UDreamWidget> Widget = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUITest")
	TObjectPtr<UObject> Asset = nullptr;
};

UCLASS(Blueprintable)
class UDreamDesignerCompositeReferenceTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "DreamGUITest")
	FDreamDesignerCompositeReferenceTestValue Value;

	UPROPERTY(EditAnywhere, Category = "DreamGUITest")
	TArray<FDreamDesignerCompositeReferenceTestValue> Values;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> LastWidget = nullptr;

	UFUNCTION()
	void Touch() {}

	UFUNCTION()
	void TakeWidget(UDreamWidget* InWidget) { LastWidget = InWidget; }
};
