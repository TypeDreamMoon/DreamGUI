// Copyright 2025-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexLayout.h"
#include "LexLayoutContainerChildSizeFitter.generated.h"

class ULexWidget;

/**
 * Provide layout info that fit child widget (the first child which NOT IgnoreLayout).
 * This will not set Widget's size directly, it only provides layout info like Width & Height
 */
UCLASS(BlueprintType, DisplayName="LayoutContainer-ChildSizeFitter")
class LGUI_API ULexLayoutContainerChildSizeFitter : public ULexLayoutContainer
{
	GENERATED_BODY()
private:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", Getter=GetFitWidth, Setter=SetFitWidth, meta = (AllowPrivateAccess = true))
	bool bFitWidth = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", Getter=GetFitHeight, Setter=SetFitHeight, meta = (AllowPrivateAccess = true))
	bool bFitHeight = false;

	bool bIsCalculatingSize = false;
	FVector2f CalculatedPreferred;
	void CalculateSize();
public:
	virtual void OnTransformChanged() override;
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange) override;
	virtual void CalculateLayout() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostInitProperties() override;
#endif
	virtual FLexLayoutControlAnchorData GetLayoutControlAnchor(const ULexWidget* Widget)const override;
	virtual FVector2f GetLayoutPreferredSize() override;
	
	UFUNCTION(BlueprintCallable, Category = "Layout")
	bool GetFitWidth()const{return bFitWidth;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	bool GetFitHeight()const{return bFitHeight;}
	
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetFitWidth(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetFitHeight(bool Value);
};
