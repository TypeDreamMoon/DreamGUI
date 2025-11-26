// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "Layout/Margin.h"
#include "LexLayoutGridContainer.generated.h"

UENUM(BlueprintType)
enum class ELexLayoutGridSizeType:uint8
{
	/**
	 * Auto will use max preferred-size of cell content
	 */
	//Auto,
	
	/**
	 * Fixed pixel value
	 */
	Fixed,
	/**
	 * 
	 */
	Ratio,
};

USTRUCT(BlueprintType)
struct LGUI_API FLexLayoutGridSize
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category = "LGUI")
	ELexLayoutGridSizeType Type = ELexLayoutGridSizeType::Ratio;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (UIMin = "0.0"))
	float FixedValue = 100.0f;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (UIMin = "0.0"))
	float RatioValue = 1.0f;

	FLexLayoutGridSize() {}
	FLexLayoutGridSize(float InFixedValue)
	{
		this->FixedValue = InFixedValue;
		this->Type = ELexLayoutGridSizeType::Fixed;
	}
	bool operator == (const FLexLayoutGridSize& Other)const
	{
		return this->FixedValue == Other.FixedValue
			&& this->RatioValue == Other.RatioValue
			&& this->Type == Other.Type
			;
	}
};

class ULexLayoutGridSelf;

/**
 * Flexible & Responsive grid based layout.
 */
UCLASS( ClassGroup=(LGUI), DisplayName="Grid Container")
class LGUI_API ULexLayoutGridContainer : public ULexLayoutContainer
{
	GENERATED_BODY()

public:
	ULexLayoutGridContainer();

private:
	friend class FUIFlexibleGridLayoutCustomization;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	FMargin Padding;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	FVector2D Spacing;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	TArray<FLexLayoutGridSize> Columns;
	UPROPERTY(EditAnywhere, Category = "LGUI")
	TArray<FLexLayoutGridSize> Rows;

public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* TargetWidget)const override;
	virtual void UpdateLayout()override;

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	FMargin GetPadding()const { return Padding; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	const TArray<FLexLayoutGridSize>& GetColumns()const { return Columns; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	const TArray<FLexLayoutGridSize>& GetRows()const { return Rows; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetRowCount()const { return Rows.Num(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetColumnCount()const { return Columns.Num(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	FVector2D GetSpacing()const { return Spacing; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetPadding(FMargin Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetRows(const TArray<FLexLayoutGridSize>& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetColumns(const TArray<FLexLayoutGridSize>& Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetSpacing(const FVector2D& Value);
};
