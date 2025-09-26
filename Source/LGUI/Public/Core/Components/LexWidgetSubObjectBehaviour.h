// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexWidgetSubObjectBehaviour.generated.h"

class ULexWidget;
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexWidgetSubObjectBehaviour : public UObject
{
	GENERATED_BODY()

public:
	void Call_OnRegister();
	void Call_OnUnregister();
	virtual void BeginPlay(){};
	virtual void EndPlay(){};
	
	virtual ULexWidget* GetWidget()const;
protected:
	virtual void OnRegister(){};
	virtual void OnUnregister(){};
private:
	bool bIsRegistered = false;
	mutable TWeakObjectPtr<ULexWidget> CacheWidget;
};
