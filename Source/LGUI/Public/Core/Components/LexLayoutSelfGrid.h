// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutSelfGrid.generated.h"

/**
 * Provide item properties for GridContainer,
 */
UCLASS( ClassGroup=(LGUI), DisplayName="LayoutSelf-Grid", Experimental)
class LGUI_API ULexLayoutSelfGrid : public ULexLayoutSelf
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
private:
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0"))
	int RowIndex = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "1"))
	int RowCount = 1;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0"))
	int ColumnIndex = 0;
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "1"))
	int ColumnCount = 1;
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget) const override;
	virtual FVector2f GetLayoutPreferredSize() override;
	
	void SetSizeByLayoutContainer(FVector2f Value); 

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetRowIndex()const { return RowIndex; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetRowCount()const { return RowCount; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetColumnIndex()const { return ColumnIndex; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetColumnCount()const { return ColumnCount; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetRowIndex(int Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetRowCount(int Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetColumnIndex(int Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetColumnCount(int Value);
};
