// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "Layout/Margin.h"
#include "LexLayoutContainerGrid.generated.h"

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
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0"))
	float FixedValue = 100.0f;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0"))
	float RatioValue = 1.0f;

	FLexLayoutGridSize() {}
	FLexLayoutGridSize(ELexLayoutGridSizeType InType, float InValue)
	{
		this->Type = InType;
		switch (InType)
		{
			case ELexLayoutGridSizeType::Fixed:
			this->FixedValue = InValue;
			break;
			case ELexLayoutGridSizeType::Ratio:
			this->RatioValue = InValue;
			break;
		}
	}
	bool operator == (const FLexLayoutGridSize& Other)const
	{
		return this->FixedValue == Other.FixedValue
			&& this->RatioValue == Other.RatioValue
			&& this->Type == Other.Type
			;
	}
};

class ULexLayoutSelfGrid;

/**
 * Flexible & Responsive grid based layout.
 */
UCLASS(ClassGroup=(LGUI), DisplayName="Responsive Grid (Experimental)", Experimental)
class LGUI_API ULexLayoutContainerGrid : public ULexLayoutContainer
{
	GENERATED_BODY()

public:
	ULexLayoutContainerGrid();

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
	virtual FVector2f GetLayoutPreferredSize() override;
	virtual void CalculateLayout()override;

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
