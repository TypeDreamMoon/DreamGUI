// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexWidgetSubObjectBehaviour.generated.h"

class ULexWidget;
/** Base class of UI element that can be renderred by LGUICanvas */
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexWidgetSubObjectBehaviour : public UObject
{
	GENERATED_BODY()

public:
	virtual void BeginPlay(){};
	virtual void EndPlay(){};
	virtual void OnRegister(){};
	virtual void OnUnregister(){};

	virtual void OnParentTransformChanged(){}
	virtual void OnParentDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};
	virtual void OnTransformChanged(){}
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};
	virtual void OnPixelSnappingChanged(){}
	virtual void OnClipDataChanged(){}

	UFUNCTION(BlueprintCallable, Category="LGUI")
	ULexWidget* GetWidget()const;
private:
	mutable TWeakObjectPtr<ULexWidget> CacheWidget;
};
