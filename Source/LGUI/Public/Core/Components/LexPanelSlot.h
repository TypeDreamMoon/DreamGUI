// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexWidgetSubObjectBehaviour.h"
#include "LexPanelSlot.generated.h"

UENUM(BlueprintType)
enum class ELexPanelHorizontalAlignment : uint8
{
	Fill,
	Left,
	Center,
	Right,
};

UENUM(BlueprintType)
enum class ELexPanelVerticalAlignment : uint8
{
	Fill,
	Top,
	Center,
	Bottom,
};

UENUM(BlueprintType)
enum class ELexPanelSizeRule : uint8
{
	Auto,
	Fill,
};

/** UMG-style per-child layout data owned by the child widget. */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew, DisplayName = "Panel Slot")
class LGUI_API ULexPanelSlot : public ULexWidgetSubObjectBehaviour
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPadding, Category = "Slot")
	FMargin Padding;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetHorizontalAlignment, Category = "Slot")
	ELexPanelHorizontalAlignment HorizontalAlignment = ELexPanelHorizontalAlignment::Fill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetVerticalAlignment, Category = "Slot")
	ELexPanelVerticalAlignment VerticalAlignment = ELexPanelVerticalAlignment::Fill;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSizeRule, Category = "Slot")
	ELexPanelSizeRule SizeRule = ELexPanelSizeRule::Auto;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFillWeight, Category = "Slot", meta = (ClampMin = "0.0"))
	float FillWeight = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRow, Category = "Slot", meta = (ClampMin = "0"))
	int32 Row = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColumn, Category = "Slot", meta = (ClampMin = "0"))
	int32 Column = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRowSpan, Category = "Slot", meta = (ClampMin = "1"))
	int32 RowSpan = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColumnSpan, Category = "Slot", meta = (ClampMin = "1"))
	int32 ColumnSpan = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetZOrder, Category = "Slot")
	int32 ZOrder = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetAutoSize, Category = "Slot")
	bool bAutoSize = false;

	UFUNCTION(BlueprintSetter) void SetPadding(FMargin Value);
	UFUNCTION(BlueprintSetter) void SetHorizontalAlignment(ELexPanelHorizontalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetVerticalAlignment(ELexPanelVerticalAlignment Value);
	UFUNCTION(BlueprintSetter) void SetSizeRule(ELexPanelSizeRule Value);
	UFUNCTION(BlueprintSetter) void SetFillWeight(float Value);
	UFUNCTION(BlueprintSetter) void SetRow(int32 Value);
	UFUNCTION(BlueprintSetter) void SetColumn(int32 Value);
	UFUNCTION(BlueprintSetter) void SetRowSpan(int32 Value);
	UFUNCTION(BlueprintSetter) void SetColumnSpan(int32 Value);
	UFUNCTION(BlueprintSetter) void SetZOrder(int32 Value);
	UFUNCTION(BlueprintSetter) void SetAutoSize(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Slot")
	void NotifySlotChanged();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
