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
	
	virtual ULexWidget* GetWidget()const;
private:
	mutable TWeakObjectPtr<ULexWidget> CacheWidget;
};
